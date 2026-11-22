// Standalone white-box tests of the actual server implementation, not a queue copy.
// Keep the production entry point out of this test executable.
#define main ReceiveQueueTestServerMain
#include "../Server/server.cpp"
#undef main
#include <future>
#include <type_traits>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::fflush(stderr);
        std::_Exit(1); // Fail boundedly even if a worker is stuck holding a lock.
    }
}

template<class T> void Ready(std::future<T>& task) {
    Check(task.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "worker did not finish within five seconds");
}

ReceiveEvent InputEvent(std::uint64_t connection, std::uint32_t sequence) {
    Packet packet;
    packet.header.packetType = PacketType::CS_INPUT;
    packet.header.matchId = 17;
    packet.payload = InputPayload(sequence % 2 ? kInputRight : kInputNone,
                                  sequence, sequence % 3 == 0);
    packet.header.packetSize = static_cast<std::uint32_t>(kHeaderSize + packet.payload.size());
    return {connection, IoStatus::Progress, std::move(packet)};
}

void CheckInput(const ReceiveEvent& event, std::uint64_t connection, std::uint32_t sequence) {
    const auto expected = InputEvent(connection, sequence);
    Check(event.connection == connection && event.status == IoStatus::Progress &&
          event.packet.header.packetType == expected.packet.header.packetType &&
          event.packet.header.matchId == expected.packet.header.matchId &&
          event.packet.header.packetSize == expected.packet.header.packetSize &&
          event.packet.payload == expected.packet.payload,
          "input event lost, duplicated, reordered or corrupted");
}

void LockLifetime() {
    static_assert(!std::is_copy_constructible<CriticalSection>::value, "must not copy");
    static_assert(!std::is_move_constructible<CriticalSection>::value, "must not move");
    CriticalSection section;
    try {
        CriticalSectionLock lock(section);
        throw 7;
    } catch (int) {}
    auto acquired = std::async(std::launch::async, [&] {
        CriticalSectionLock lock(section);
        return true;
    });
    Ready(acquired);
    Check(acquired.get(), "exception path failed to release the lock");
    std::puts("PASS: critical section RAII releases across exception unwinding");
}

void ConcurrentProducers() {
    ReceiveQueue queue;
    std::atomic<bool> stop{false};
    std::promise<void> start;
    const auto go = start.get_future().share();
    constexpr unsigned producers = 4, perProducer = 2048;
    std::vector<std::future<void>> tasks;
    for (unsigned id = 1; id <= producers; ++id) {
        tasks.push_back(std::async(std::launch::async, [&, id] {
            go.wait();
            for (unsigned n = 1; n <= perProducer; ++n)
                Check(queue.Push(InputEvent(id, n), stop), "unexpected canceled push");
        }));
    }
    start.set_value();
    unsigned seen[producers] = {};
    unsigned total = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (total < producers * perProducer && Clock::now() < deadline) {
        auto batch = queue.Take();
        Check(batch.size() <= 256, "queue capacity exceeded");
        for (const auto& event : batch) {
            Check(event.connection >= 1 && event.connection <= producers, "invalid connection");
            const auto index = static_cast<unsigned>(event.connection - 1);
            CheckInput(event, event.connection, ++seen[index]);
            ++total;
        }
        std::this_thread::yield();
    }
    Check(total == producers * perProducer, "concurrent events missing");
    for (auto& task : tasks) { Ready(task); task.get(); }
    for (unsigned count : seen) Check(count == perProducer, "producer event count mismatch");
    Check(queue.Take().empty(), "extra events after producers finished");
    std::puts("PASS: four concurrent producers, 8192 exact events, per-connection FIFO and integrity");
}

void FullQueue() {
    ReceiveQueue queue;
    std::atomic<bool> stop{false};
    for (unsigned n = 1; n <= 256; ++n)
        Check(queue.Push(InputEvent(1, n), stop), "queue fill failed");
    std::promise<void> started;
    auto pushing = std::async(std::launch::async, [&] {
        started.set_value();
        return queue.Push(InputEvent(1, 257), stop);
    });
    started.get_future().wait();
    Check(pushing.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
          "full queue must backpressure its producer");
    // Must acquire the lock while the producer is waiting; watchdog detects deadlock.
    auto batch = queue.Take();
    Check(batch.size() == 256, "full queue lost data or exceeded its limit");
    unsigned n = 0;
    for (const auto& event : batch) CheckInput(event, 1, ++n);
    Ready(pushing);
    Check(pushing.get(), "producer did not resume after draining");
    batch = queue.Take();
    Check(batch.size() == 1, "resumed push duplicated or lost");
    CheckInput(batch.front(), 1, 257);

    for (n = 1; n <= 256; ++n) Check(queue.Push(InputEvent(2, n), stop), "refill failed");
    std::promise<void> cancelStarted;
    auto canceled = std::async(std::launch::async, [&] {
        cancelStarted.set_value();
        return queue.Push(InputEvent(2, 257), stop);
    });
    cancelStarted.get_future().wait();
    Check(canceled.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
          "cancel test must start with a blocked producer");
    stop.store(true);
    Ready(canceled);
    Check(!canceled.get(), "stop must cancel a full-queue push");
    batch = queue.Take();
    Check(batch.size() == 256, "cancel modified queued events");
    n = 0;
    for (const auto& event : batch) CheckInput(event, 2, ++n);
    Check(!queue.Push(InputEvent(2, 258), stop) && queue.Take().empty(),
          "stopped queue accepted an event");
    std::puts("PASS: capacity 256, unlocked backpressure, resume, stop and no partial canceled push");
}

// Real loopback TCP connections, with ephemeral ports to avoid the live suite's 9000.
struct LoopbackClient {
    SOCKET peer = INVALID_SOCKET;
    std::unique_ptr<Client> receiver;
    LoopbackClient(ReceiveQueue& queue, std::uint64_t id) {
        const SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
        Check(listener != INVALID_SOCKET, "listener socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
              listen(listener, 1) == 0, "loopback listen");
        int size = sizeof(address);
        Check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port");
        peer = socket(AF_INET, SOCK_STREAM, 0);
        Check(peer != INVALID_SOCKET &&
              connect(peer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "connect");
        const SOCKET accepted = accept(listener, nullptr, nullptr);
        closesocket(listener);
        Check(accepted != INVALID_SOCKET && SetNonblocking(accepted), "accept");
        const DWORD timeout = 3000;
        Check(setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO,
                        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0, "send timeout");
        receiver.reset(new Client(accepted, id, queue));
        receiver->StartReceiver();
    }
    ~LoopbackClient() {
        receiver.reset(); // Stop and join before the queue can be destroyed.
        closesocket(peer);
    }
    void SendInputs(unsigned count) {
        for (unsigned n = 1; n <= count; ++n) {
            const auto event = InputEvent(receiver->connection, n);
            PendingSend packet;
            Check(packet.Prepare(PacketType::CS_INPUT, 17, event.packet.payload), "encode input");
            Check(packet.Flush(peer).status == IoStatus::Complete, "send input");
        }
    }
};

void ConcurrentReceivers() {
    ReceiveQueue queue;
    LoopbackClient first(queue, 101), second(queue, 102);
    std::promise<void> start;
    const auto go = start.get_future().share();
    auto sendFirst = std::async(std::launch::async, [&] { go.wait(); first.SendInputs(1024); });
    auto sendSecond = std::async(std::launch::async, [&] { go.wait(); second.SendInputs(1024); });
    start.set_value();
    unsigned seen[2] = {};
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (seen[0] + seen[1] < 2048 && Clock::now() < deadline) {
        for (const auto& event : queue.Take()) {
            Check(event.connection == 101 || event.connection == 102, "receiver identity changed");
            const unsigned index = static_cast<unsigned>(event.connection - 101);
            CheckInput(event, event.connection, ++seen[index]);
        }
        std::this_thread::yield();
    }
    Ready(sendFirst); sendFirst.get(); Ready(sendSecond); sendSecond.get();
    Check(seen[0] == 1024 && seen[1] == 1024, "TCP inputs missing or duplicated");
    Check(shutdown(first.peer, SD_SEND) == 0 && shutdown(second.peer, SD_SEND) == 0, "send EOF");
    bool closed[2] = {};
    const auto closeDeadline = Clock::now() + std::chrono::seconds(5);
    while (!(closed[0] && closed[1]) && Clock::now() < closeDeadline) {
        for (const auto& event : queue.Take()) {
            Check(event.connection == 101 || event.connection == 102, "EOF identity");
            const unsigned index = static_cast<unsigned>(event.connection - 101);
            Check(event.status == IoStatus::Closed && !closed[index], "extra input or duplicate EOF");
            closed[index] = true;
        }
        std::this_thread::yield();
    }
    Check(closed[0] && closed[1], "EOF missing");
    first.receiver.reset(); second.receiver.reset();
    Check(queue.Take().empty(), "extra events after receiver join");
    std::puts("PASS: two simultaneous TCP senders and actual receive threads, 2048 exact inputs and EOFs");
}

void FullQueueReceiverShutdown() {
    // Recreate and destroy the real queue/critical section with two active receivers.
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        ReceiveQueue queue;
        std::atomic<bool> stop{false};
        for (unsigned n = 1; n <= 256; ++n)
            Check(queue.Push(InputEvent(1, n), stop), "shutdown queue fill");
        {
            LoopbackClient first(queue, 201), second(queue, 202);
            first.SendInputs(1); second.SendInputs(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            // No consumer frees capacity. Client destructors must stop and join anyway.
        }
        const auto batch = queue.Take();
        Check(batch.size() == 256, "stopping receivers modified the full queue");
        unsigned n = 0;
        for (const auto& event : batch) CheckInput(event, 1, ++n);
    }
    std::puts("PASS: repeated full-queue receiver stop/join before critical section destruction");
}
} // namespace

int main() {
    // Also bounds synchronous Take/destruction if a lock regression causes a deadlock.
    std::promise<void> finished;
    auto done = finished.get_future();
    std::thread watchdog([&] {
        Check(done.wait_for(std::chrono::seconds(45)) == std::future_status::ready,
              "synchronization test timed out (possible deadlock)");
    });
    WSADATA wsa;
    Check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "WSAStartup");
    LockLifetime();
    ConcurrentProducers();
    FullQueue();
    ConcurrentReceivers();
    FullQueueReceiverShutdown();
    WSACleanup();
    finished.set_value();
    watchdog.join();
    std::puts("All receive queue synchronization tests passed.");
}
