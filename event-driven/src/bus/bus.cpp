#include "bus/bus.hpp"

#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <trantor/utils/Logger.h>

#include "events.hpp"

using namespace std::chrono_literals;

namespace {
std::unique_ptr<RdKafka::Conf> config(std::initializer_list<std::pair<const char*, std::string>> props) {
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    std::string err;
    for (const auto& [key, value] : props)
        if (conf->set(key, value, err) != RdKafka::Conf::CONF_OK)
            throw std::runtime_error(std::string("kafka config ") + key + ": " + err);
    return conf;
}

int ms(std::chrono::milliseconds d) { return static_cast<int>(d.count()); }
}  // namespace

void Producer::Reports::dr_cb(RdKafka::Message& message) {
    if (message.err() == RdKafka::ERR_NO_ERROR) return;
    failed.fetch_add(1);
    LOG_WARN << "kafka: event to " << message.topic_name() << " not delivered: " << message.errstr();
}

Producer::Producer(const std::string& brokers) {
    auto conf = config({
        {"bootstrap.servers", brokers},
        {"enable.idempotence", "true"},                 // retries never duplicate or reorder an event
        {"linger.ms", "5"},                             // batch what arrives within 5 ms into one request
        {"queue.buffering.max.messages", "1000000"},    // ~30 s of scans if the broker goes away
    });
    std::string err;
    if (conf->set("dr_cb", &reports_, err) != RdKafka::Conf::CONF_OK) throw std::runtime_error(err);
    producer_.reset(RdKafka::Producer::create(conf.get(), err));
    if (!producer_) throw std::runtime_error("kafka producer: " + err);
    poller_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) producer_->poll(100);
    });
}

Producer::~Producer() {
    if (producer_->flush(5000) != RdKafka::ERR_NO_ERROR)
        LOG_WARN << "kafka: " << producer_->outq_len() << " events not delivered at shutdown";
    poller_.request_stop();
    poller_.join();
}

bool Producer::publish(const std::string& topic, const std::string& key, const std::string& value) {
    const auto err = producer_->produce(topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                                        const_cast<char*>(value.data()), value.size(), key.data(), key.size(),
                                        0, nullptr);
    return err == RdKafka::ERR_NO_ERROR;
}

bool Producer::flush(std::chrono::milliseconds timeout) {
    const bool drained = producer_->flush(ms(timeout)) == RdKafka::ERR_NO_ERROR;
    return reports_.failed.exchange(0) == 0 && drained;
}

Consumer::Consumer(const std::string& brokers, const std::string& group, const std::string& topic) {
    auto conf = config({
        {"bootstrap.servers", brokers},
        {"group.id", group},
        {"enable.auto.commit", "false"},     // offsets move only on commit()
        {"auto.offset.reset", "earliest"},   // a new group starts at the oldest event
    });
    std::string err;
    consumer_.reset(RdKafka::KafkaConsumer::create(conf.get(), err));
    if (!consumer_) throw std::runtime_error("kafka consumer: " + err);
    if (auto e = consumer_->subscribe({topic}); e != RdKafka::ERR_NO_ERROR)
        throw std::runtime_error("kafka subscribe " + topic + ": " + RdKafka::err2str(e));
}

Consumer::~Consumer() { consumer_->close(); }

std::vector<std::string> Consumer::poll(size_t max, std::chrono::milliseconds wait) {
    std::vector<std::string> values;
    int timeout = ms(wait);   // wait for the first event only; then take what is already here
    while (values.size() < max) {
        std::unique_ptr<RdKafka::Message> m(consumer_->consume(timeout));
        if (m->err() == RdKafka::ERR__TIMED_OUT) break;
        if (m->err() != RdKafka::ERR_NO_ERROR) {
            LOG_WARN << "kafka consume: " << m->errstr();
            break;
        }
        values.emplace_back(static_cast<const char*>(m->payload()), m->len());
        timeout = 0;
    }
    return values;
}

void Consumer::commit() {
    // NO_OFFSET: nothing consumed since the last commit.
    if (auto e = consumer_->commitSync(); e != RdKafka::ERR_NO_ERROR && e != RdKafka::ERR__NO_OFFSET)
        LOG_WARN << "kafka commit: " << RdKafka::err2str(e) << " (the batch will be delivered again)";
}

std::string latestEvent(const std::string& brokers, const std::string& topic, std::chrono::milliseconds timeout) {
    auto conf = config({
        {"bootstrap.servers", brokers},
        {"group.id", "latest-event-readers"},   // required by the client; assign() never joins the group
        {"enable.auto.commit", "false"},
        {"enable.partition.eof", "true"},       // tells us when we have read everything there is
    });
    std::string err;
    std::unique_ptr<RdKafka::KafkaConsumer> c(RdKafka::KafkaConsumer::create(conf.get(), err));
    if (!c) throw std::runtime_error("kafka consumer: " + err);
    std::unique_ptr<RdKafka::TopicPartition> start(
        RdKafka::TopicPartition::create(topic, 0, RdKafka::Topic::OFFSET_BEGINNING));
    c->assign({start.get()});

    std::optional<std::string> latest;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        std::unique_ptr<RdKafka::Message> m(c->consume(200));
        if (m->err() == RdKafka::ERR_NO_ERROR) latest.emplace(static_cast<const char*>(m->payload()), m->len());
        if (m->err() == RdKafka::ERR__PARTITION_EOF && latest) break;   // read to the end, have one
        if (std::chrono::steady_clock::now() > deadline) {
            c->close();
            throw std::runtime_error("no event on " + topic + " after " + std::to_string(timeout.count()) + " ms");
        }
    }
    c->close();
    return *latest;
}

void publishCatalog(const std::string& brokers, const Catalog& catalog) {
    Producer p(brokers);
    if (!p.publish(topic::kCatalog, "items", toJson(catalogJson(catalog))) || !p.flush(30s))
        throw std::runtime_error("kafka at " + brokers + " did not store the catalog");
}

Catalog fetchCatalog(const std::string& brokers) {
    auto body = parseJson(latestEvent(brokers, topic::kCatalog, 30s));
    if (!body) throw std::runtime_error("catalog.items event is not JSON");
    return catalogFromJson(*body);
}
