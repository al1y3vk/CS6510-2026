// Needs the Kafka broker on 127.0.0.1:9092 (`docker compose up -d broker`), or KAFKA_BROKERS.
// Every test uses its own new topic and group, so tests never see each other's events.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "bus/bus.hpp"

using namespace std::chrono_literals;

namespace {
std::string brokers() {
    const char* env = std::getenv("KAFKA_BROKERS");
    return env ? env : "127.0.0.1:9092";
}

std::string unique(const std::string& prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    return prefix + "-" + std::to_string(rng());
}

// Polls until `n` events arrived or 20 s passed (joining a group takes a few seconds).
std::vector<std::string> pollN(Consumer& c, size_t n) {
    std::vector<std::string> got;
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (got.size() < n && std::chrono::steady_clock::now() < deadline)
        for (auto& v : c.poll(n - got.size(), 200ms)) got.push_back(std::move(v));
    return got;
}

// A topic exists once something was published to it (the broker auto-creates topics).
void publishAll(const std::string& topic, const std::vector<std::string>& values) {
    Producer p(brokers());
    for (const auto& v : values) REQUIRE(p.publish(topic, "k", v));
    REQUIRE(p.flush(10s));
}
}  // namespace

TEST_CASE("a new group reads a topic's events from the oldest, in order", "[bus]") {
    const auto topic = unique("test.order");
    publishAll(topic, {"a", "b", "c"});

    Consumer c(brokers(), unique("g"), topic);
    REQUIRE(pollN(c, 3) == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(c.poll(10, 500ms).empty());
}

TEST_CASE("events polled but not committed are delivered again to the group", "[bus]") {
    const auto topic = unique("test.commit");
    const auto group = unique("g");
    publishAll(topic, {"a", "b"});
    {
        Consumer first(brokers(), group, topic);
        REQUIRE(pollN(first, 2) == std::vector<std::string>{"a", "b"});
        first.commit();
        publishAll(topic, {"c"});
        REQUIRE(pollN(first, 1) == std::vector<std::string>{"c"});
        // no commit: the consumer "crashes" after handling c
    }
    Consumer second(brokers(), group, topic);
    REQUIRE(pollN(second, 1) == std::vector<std::string>{"c"});
}

TEST_CASE("latestEvent returns the newest event on the topic", "[bus]") {
    const auto topic = unique("test.latest");
    publishAll(topic, {"v1", "v2"});

    REQUIRE(latestEvent(brokers(), topic, 20s) == "v2");
}

TEST_CASE("latestEvent gives up on a topic nobody publishes to", "[bus]") {
    publishAll(unique("test.other"), {"x"});   // broker is up; the topic below just stays empty

    REQUIRE_THROWS_AS(latestEvent(brokers(), unique("test.empty"), 2s), std::runtime_error);
}
