#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <librdkafka/rdkafkacpp.h>

#include "catalog.hpp"

// Thin wrappers over librdkafka (the Kafka client). The services talk to each other only through
// these: a service publishes events to a topic, and whoever cares reads that topic.

// Publishes events. publish() never blocks: librdkafka queues the event, and its own threads send
// queued events to the broker in batches and retry until the broker acknowledges them.
class Producer {
public:
    explicit Producer(const std::string& brokers);
    ~Producer();   // waits up to 5 s for queued events to reach the broker
    // Queues one event. False if the local queue is full (broker unreachable for a long time).
    bool publish(const std::string& topic, const std::string& key, const std::string& value);
    // Blocks until the broker has acknowledged every queued event. True if all of them were
    // stored; false if any failed or `timeout` passed first. Reports on the events published
    // since the previous flush().
    bool flush(std::chrono::milliseconds timeout);
private:
    struct Reports : RdKafka::DeliveryReportCb {
        std::atomic<int> failed{0};
        void dr_cb(RdKafka::Message& message) override;
    };
    Reports reports_;
    std::unique_ptr<RdKafka::Producer> producer_;
    std::jthread poller_;   // serves delivery reports while nobody is in flush()
};

// Reads one topic as a member of consumer group `group`. Kafka stores how far each group has read
// (its offset), so a restarted service resumes where it left off and a new group starts from the
// oldest event. The offset moves only on commit(): call it after handling a batch, so a crash
// before then means the batch is delivered again (at least once).
class Consumer {
public:
    Consumer(const std::string& brokers, const std::string& group, const std::string& topic);
    ~Consumer();
    // Up to `max` event values, waiting at most `wait` for the first one. Empty if none arrived.
    std::vector<std::string> poll(size_t max, std::chrono::milliseconds wait);
    // Saves the group's offset past every event poll() has returned.
    void commit();
private:
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
};

// The newest event on (partition 0 of) `topic`, read from the start of the topic without joining
// a group. Waits for one to be published; throws std::runtime_error after `timeout`.
std::string latestEvent(const std::string& brokers, const std::string& topic, std::chrono::milliseconds timeout);

// Catalog service, at startup: publishes the whole catalog to catalog.items and waits for the
// broker to store it. Throws if the broker does not acknowledge within 30 s.
void publishCatalog(const std::string& brokers, const Catalog& catalog);
// Every other service, at startup: the newest catalog.items event (waits up to 30 s for the
// catalog service to publish one).
Catalog fetchCatalog(const std::string& brokers);
