#include "frame.h"
#include "frame_queue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace nexus::wire;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "invalid local input accepted");
}

void golden_and_fragmentation() {
    const std::string payload("a\0z", 3);
    const unsigned char golden[] = {'N', 'X', 'P', 0, 0, 1, 0, 1, 0, 0, 0, 3, 'a', 0, 'z'};
    const std::string bytes(reinterpret_cast<const char*>(golden), sizeof golden);
    check(encode(diagnostic, payload) == bytes, "wire bytes/endian mismatch");
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        Decoder decoder;
        const auto a = decoder.consume(std::string_view(bytes).substr(0, split));
        const auto b = decoder.consume(std::string_view(bytes).substr(split));
        check(a.consumed + b.consumed == bytes.size(), "split consumed count");
        check(b.status == DecodeStatus::ready, "fragmented frame not ready");
        check(decoder.consume(bytes).consumed == 0, "ready decoder swallowed next frame");
        auto frame = decoder.take();
        check(frame && frame->type == diagnostic && frame->payload == payload, "binary payload changed");
        check(!decoder.take(), "frame delivered twice");
        check(decoder.buffered_bytes() == 0, "decoder did not reset");
        check(decoder.finish().error == DecodeError::none, "clean EOF rejected");
    }
    const auto two = bytes + encode(diagnostic, {});
    Decoder decoder;
    const auto first = decoder.consume(two);
    check(first.consumed == bytes.size(), "coalesced frame overconsumed");
    check(decoder.take()->payload == payload, "first coalesced frame");
    const auto second = decoder.consume(std::string_view(two).substr(first.consumed));
    check(second.status == DecodeStatus::ready && second.consumed == header_size, "empty frame framing");
    check(decoder.take()->payload.empty(), "empty payload changed");

    const std::string maximum(max_payload, '\xff');
    const auto big = encode(diagnostic, maximum);
    for (char byte : big) {
        const auto result = decoder.consume(std::string_view(&byte, 1));
        check(result.consumed == 1 && result.status != DecodeStatus::invalid, "bytewise maximum rejected");
        check(decoder.buffered_bytes() <= header_size + max_payload, "decoder exceeded bound");
    }
    check(decoder.take()->payload == maximum, "maximum payload changed");
    rejects([] { encode(0, "x"); });
    rejects([] { encode(65535, "x"); });
    rejects([] { encode(diagnostic, std::string(max_payload + 1, 'x')); });
}

void malformed_and_eof() {
    const auto valid = encode(diagnostic, "payload");
    struct Mutation { std::size_t offset; unsigned char byte; DecodeError error; };
    const Mutation mutations[] = {
        {0, 'M', DecodeError::magic}, {1, 'Y', DecodeError::magic},
        {2, 'Q', DecodeError::magic}, {3, 1, DecodeError::magic},
        {4, 1, DecodeError::version}, {5, 2, DecodeError::version},
        {6, 1, DecodeError::type}, {7, 0, DecodeError::type},
        {7, 2, DecodeError::type}, {8, 255, DecodeError::length},
        {9, 255, DecodeError::length}
    };
    for (const auto& mutation : mutations) {
        auto bad = valid;
        bad[mutation.offset] = static_cast<char>(mutation.byte);
        Decoder decoder;
        const auto result = decoder.consume(bad + valid);
        check(result.status == DecodeStatus::invalid && result.error == mutation.error, "malformed header accepted");
        check(result.consumed == header_size && decoder.buffered_bytes() == header_size, "bad header consumed body");
        check(decoder.consume(valid).consumed == 0 && !decoder.take(), "invalid decoder resynchronized");
        check(decoder.finish().error == mutation.error, "EOF hid protocol error");
    }
    auto oversized = encode(diagnostic, std::string(max_payload, 'x')).substr(0, header_size);
    oversized[11] = 1; // 65537, one over the maximum; no body needed to reject it.
    Decoder large;
    check(large.consume(oversized).error == DecodeError::length, "maximum plus one accepted");
    for (std::size_t size = 1; size < valid.size(); ++size) {
        Decoder decoder;
        check(decoder.consume(std::string_view(valid).substr(0, size)).status == DecodeStatus::need_more, "truncated frame ready");
        check(decoder.finish().error == DecodeError::truncated, "truncated EOF accepted");
        check(decoder.consume(valid).consumed == 0, "EOF failure not terminal");
    }
    Decoder complete;
    complete.consume(valid);
    check(complete.finish().status == DecodeStatus::ready, "EOF lost ready frame");
    check(complete.take()->payload == "payload", "EOF changed frame");
}

void queue_limits() {
    rejects([] { FrameQueue queue(0, 12); });
    rejects([] { FrameQueue queue(4097, 12); });
    rejects([] { FrameQueue queue(1, 11); });
    rejects([] { FrameQueue queue(1, std::numeric_limits<std::size_t>::max()); });
    FrameQueue queue(2, 27);
    check(queue.try_pop().status == QueueStatus::empty, "new queue not empty");
    check(queue.try_push(diagnostic, "abc") == QueueStatus::ok, "push failed");
    check(queue.try_push(diagnostic, "d") == QueueStatus::full, "byte bound exceeded");
    check(queue.try_push(diagnostic, {}) == QueueStatus::ok, "exact byte budget rejected");
    check(queue.try_push(diagnostic, {}) == QueueStatus::full, "count bound exceeded");
    check(queue.try_pop().frame->payload == "abc", "FIFO failed");
    check(queue.try_push(diagnostic, "xyz") == QueueStatus::ok, "capacity not returned");
    queue.close();
    queue.close();
    check(queue.try_push(diagnostic, {}) == QueueStatus::closed, "closed push accepted");
    check(queue.try_pop().frame->payload.empty(), "close lost queued frame");
    check(queue.try_pop().frame->payload == "xyz", "ring wrap corrupted order");
    check(queue.try_pop().status == QueueStatus::closed, "closed queue not drained");
    FrameQueue invalid(1, header_size + max_payload);
    check(invalid.try_push(2, "x") == QueueStatus::invalid, "queue accepted unknown type");
    check(invalid.try_push(diagnostic, std::string(max_payload + 1, 'x')) == QueueStatus::invalid, "queue accepted oversized frame");
    std::string source = "original";
    check(invalid.try_push(diagnostic, source) == QueueStatus::ok, "copy push failed");
    source[0] = 'X';
    check(invalid.try_pop().frame->payload == "original", "queue retained caller data");
}

void queue_concurrency() {
    constexpr unsigned producers = 4, per_producer = 3000;
    FrameQueue queue(7, 7 * (header_size + 8));
    std::atomic<bool> failed{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::vector<std::thread> workers;
    for (unsigned producer = 0; producer < producers; ++producer) {
        workers.emplace_back([&, producer] {
            for (unsigned sequence = 0; sequence < per_producer;) {
                const auto payload = std::to_string(producer) + ":" + std::to_string(sequence);
                const auto status = queue.try_push(diagnostic, payload);
                if (status == QueueStatus::ok) ++sequence;
                else if (status != QueueStatus::full && status != QueueStatus::busy) { failed = true; return; }
                if (std::chrono::steady_clock::now() > deadline) { failed = true; return; }
                std::this_thread::yield();
            }
        });
    }
    unsigned next[producers] = {};
    unsigned received = 0;
    while (received < producers * per_producer && std::chrono::steady_clock::now() < deadline) {
        auto result = queue.try_pop();
        if (result.status == QueueStatus::ok) {
            const auto& payload = result.frame->payload;
            const auto producer = static_cast<unsigned>(payload[0] - '0');
            if (producer >= producers || payload != std::to_string(producer) + ":" + std::to_string(next[producer]++)) {
                failed = true;
                break;
            }
            ++received;
        } else if (result.status != QueueStatus::empty && result.status != QueueStatus::busy) {
            failed = true;
            break;
        }
        std::this_thread::yield();
    }
    queue.close();
    for (auto& worker : workers) worker.join();
    check(!failed && received == producers * per_producer, "concurrent queue lost/reordered frames or stalled");
    check(queue.try_pop().status == QueueStatus::closed, "concurrent queue not empty after drain");
}

void queue_close_race() {
    FrameQueue queue(8, 8 * header_size);
    std::atomic<bool> started{false}, saw_closed{false}, failed{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::thread producer([&] {
        while (std::chrono::steady_clock::now() < deadline) {
            const auto status = queue.try_push(diagnostic, {});
            if (status == QueueStatus::closed) { saw_closed = true; return; }
            if (status == QueueStatus::ok) started = true;
            else if (status != QueueStatus::busy && status != QueueStatus::full) { failed = true; return; }
            std::this_thread::yield();
        }
        failed = true;
    });
    while (!started && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    queue.close();
    producer.join();
    check(started && saw_closed && !failed, "producer did not observe concurrent close");
    unsigned drained = 0;
    for (;;) {
        auto result = queue.try_pop();
        if (result.status == QueueStatus::closed) break;
        check(result.status == QueueStatus::ok && ++drained <= 8, "close race corrupted queue");
    }
    check(drained > 0, "close discarded accepted frames");
}
}

int main() {
    try {
        golden_and_fragmentation();
        malformed_and_eof();
        queue_limits();
        queue_concurrency();
        queue_close_race();
        std::cout << "Wire framing, hostile input, queue bounds and concurrent FIFO checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
