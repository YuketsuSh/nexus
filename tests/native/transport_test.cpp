#include "transport.h"
#include "socket_platform.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace nexus;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> void until(Predicate predicate, int seconds = 5) {
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    while (!predicate()) {
        check(Clock::now() < end, "test deadline exceeded");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::uint16_t listening(net::Transport& transport) {
    until([&] { return transport.snapshot().state != net::State::starting; });
    check(transport.snapshot().state == net::State::listening, "listener failed");
    return transport.snapshot().port;
}
void connected(net::Transport& a, net::Transport& b) {
    until([&] { return a.snapshot().state == net::State::connected && b.snapshot().state == net::State::connected; });
}
void send(net::Transport& transport, const std::string& bytes) {
    until([&] {
        const auto status = transport.send(bytes);
        check(status == wire::QueueStatus::ok || status == wire::QueueStatus::busy || status == wire::QueueStatus::full, "send rejected");
        return status == wire::QueueStatus::ok;
    });
}
std::string receive(net::Transport& transport) {
    std::optional<wire::Frame> frame;
    until([&] {
        auto result = transport.receive();
        check(result.status == wire::QueueStatus::ok || result.status == wire::QueueStatus::empty || result.status == wire::QueueStatus::busy, "receive closed early");
        frame = std::move(result.frame);
        return frame.has_value();
    });
    return std::move(frame->payload);
}

void exchange() {
    net::Transport server(true, "127.0.0.1", 0);
    net::Transport client(false, "127.0.0.1", listening(server));
    connected(server, client);
    for (const auto& payload : {std::string{}, std::string("x\0y", 3), std::string(wire::max_payload, '\xff')}) {
        send(client, payload);
        check(receive(server) == payload, "client-to-server payload corrupted");
        send(server, payload);
        check(receive(client) == payload, "server-to-client payload corrupted");
    }
    for (unsigned i = 0; i < 32; ++i) {
        send(client, "client:" + std::to_string(i));
        send(server, "server:" + std::to_string(i));
    }
    for (unsigned i = 0; i < 32; ++i) {
        check(receive(server) == "client:" + std::to_string(i), "client FIFO failed");
        check(receive(client) == "server:" + std::to_string(i), "server FIFO failed");
    }
    check(client.send(std::string(wire::max_payload + 1, 'x')) == wire::QueueStatus::invalid, "oversized send admitted");
    client.close();
    client.close();
    until([&] { return server.snapshot().state == net::State::closed; });
    check(server.snapshot().reason == net::Reason::peer_closed, "peer EOF not reported");
    check(client.send("x") == wire::QueueStatus::closed, "send after close accepted");
}

void raw_connect(net::platform::OwnedSocket& socket, std::uint16_t port) {
    socket.reset(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    check(socket.value != net::platform::invalid, "raw socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    check(::connect(socket.value, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0, "raw connect failed");
}
void raw_send(net::platform::Socket socket, const std::string& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto count = net::platform::send(socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent));
        check(count > 0, "raw send failed");
        sent += static_cast<std::size_t>(count);
    }
}

void hostile_peers() {
    const auto valid = wire::encode(wire::diagnostic, "fragmented");
    auto bad_magic = valid; bad_magic[0] = 'Z';
    auto bad_size = valid; bad_size[8] = '\xff';
    for (const auto& bad : {bad_magic, bad_size, valid.substr(0, 5), valid.substr(0, 14)}) {
        net::Transport server(true, "127.0.0.1", 0);
        net::platform::OwnedSocket raw;
        raw_connect(raw, listening(server));
        raw_send(raw.value, bad);
        raw.reset();
        until([&] { return server.snapshot().state == net::State::failed; });
        check(server.snapshot().reason == net::Reason::protocol, "hostile stream not rejected");
    }
    net::Transport server(true, "127.0.0.1", 0);
    net::platform::OwnedSocket raw;
    raw_connect(raw, listening(server));
    for (char byte : valid) {
        raw_send(raw.value, std::string(1, byte));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(receive(server) == "fragmented", "network fragmentation failed");
    raw_send(raw.value, valid + valid);
    check(receive(server) == "fragmented" && receive(server) == "fragmented", "coalescing failed");
    raw_send(raw.value, "N");
    until([&] { return server.snapshot().state == net::State::failed; }, 13);
    check(server.snapshot().reason == net::Reason::timeout, "incomplete frame did not time out");
}

void failures_and_cleanup() {
    net::Transport listener(true, "127.0.0.1", 0);
    const auto port = listening(listener);
    net::Transport collision(true, "127.0.0.1", port);
    until([&] { return collision.snapshot().state == net::State::failed; });
    check(collision.snapshot().reason == net::Reason::bind, "bind collision not reported");
    listener.close();
    net::Transport refused(false, "127.0.0.1", port);
    until([&] { return refused.snapshot().state == net::State::failed; });
    check(refused.snapshot().reason == net::Reason::connect, "refused connection not reported");
    net::Transport invalid(false, "localhost", port);
    until([&] { return invalid.snapshot().state == net::State::failed; });
    check(invalid.snapshot().reason == net::Reason::address, "DNS accidentally enabled");
    for (int cycle = 0; cycle < 20; ++cycle) {
        net::Transport server(true, "127.0.0.1", 0);
        net::Transport client(false, "127.0.0.1", listening(server));
        connected(server, client);
        bool full = false;
        const std::string large(wire::max_payload, 's');
        for (unsigned count = 0; count < 1000 && !full; ++count) {
            const auto status = client.send(large);
            full = status == wire::QueueStatus::full;
            check(full || status == wire::QueueStatus::ok || status == wire::QueueStatus::busy, "backpressure send failed");
        }
        check(full, "send queue did not apply backpressure");
        const auto start = Clock::now();
        client.close();
        server.close();
        check(Clock::now() - start < std::chrono::seconds(2), "cleanup waited for peer/timeout");
    }
    net::Transport idle(true, "127.0.0.1", 0);
    listening(idle);
    const auto start = Clock::now();
    idle.close();
    check(Clock::now() - start < std::chrono::seconds(2), "idle listener could not cancel");
}

void stalled_consumer() {
    net::Transport server(true, "127.0.0.1", 0);
    net::Transport client(false, "127.0.0.1", listening(server));
    connected(server, client);
    const std::string large(wire::max_payload, 'q');
    bool backpressure = false;
    until([&] {
        if (server.snapshot().state == net::State::failed) return true;
        const auto status = client.send(large);
        backpressure = backpressure || status == wire::QueueStatus::full;
        check(status == wire::QueueStatus::ok || status == wire::QueueStatus::full ||
            status == wire::QueueStatus::busy || status == wire::QueueStatus::closed,
            "stalled consumer unexpected send result");
        return false;
    }, 16);
    check(server.snapshot().reason == net::Reason::timeout, "stalled receive queue did not expire");
    check(backpressure, "stalled peer never applied backpressure");
    unsigned drained = 0;
    for (;;) {
        auto result = server.receive();
        if (result.status == wire::QueueStatus::closed) break;
        check(result.status == wire::QueueStatus::ok && result.frame->payload == large,
            "terminal queue lost admitted data");
        ++drained;
        check(drained <= net::Transport::queue_bytes / (wire::header_size + wire::max_payload),
            "receive queue exceeded byte bound");
    }
    check(drained > 0, "terminal queue did not preserve admitted data");
}
}

int main() {
    try {
        net::platform::Runtime runtime;
        check(runtime.ok, "socket runtime unavailable");
        exchange();
        hostile_peers();
        failures_and_cleanup();
        stalled_consumer();
        std::cout << "TCP exchange, hostile peers, backpressure and worker cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
