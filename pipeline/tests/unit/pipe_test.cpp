#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

#include "analytics/pipe.hpp"

TEST_CASE("pipe delivers values in FIFO order") {
    Pipe<int> p;
    p.push(1);
    p.push(2);
    p.push(3);
    REQUIRE(p.pop() == 1);
    REQUIRE(p.pop() == 2);
    REQUIRE(p.pop() == 3);
}

TEST_CASE("a closed pipe still drains what was pushed, then pops nullopt") {
    Pipe<int> p;
    p.push(7);
    p.close();
    REQUIRE(p.pop() == 7);
    REQUIRE_FALSE(p.pop().has_value());
}

TEST_CASE("pop blocks until another thread pushes or closes") {
    Pipe<int> p;
    std::vector<int> got;
    std::jthread consumer([&] {
        while (auto v = p.pop()) got.push_back(*v);
    });
    for (int i = 0; i < 1000; ++i) p.push(i);
    p.close();
    consumer.join();

    REQUIRE(got.size() == 1000);
    for (int i = 0; i < 1000; ++i) REQUIRE(got[i] == i);
}

TEST_CASE("popAll takes everything queued in one call; empty only once closed and drained") {
    Pipe<int> p;
    p.push(1);
    p.push(2);
    p.push(3);
    REQUIRE(p.popAll() == std::vector<int>{1, 2, 3});

    p.push(4);
    p.close();
    REQUIRE(p.popAll() == std::vector<int>{4});
    REQUIRE(p.popAll().empty());
}
