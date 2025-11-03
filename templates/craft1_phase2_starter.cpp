/**
 * Craft #1 - Phase 2 Starter Template
 * Goal: Implement async I/O with producer-consumer pattern
 *
 * Previous (Phase 1): ~200 RPS with threads but file I/O blocks
 * Target (Phase 2): ~500+ RPS with async writes
 *
 * What you'll learn:
 * - Producer-consumer pattern
 * - Thread-safe queues
 * - Background writer threads
 * - Decoupling I/O from request handling
 */

#include <iostream>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <atomic>

// Thread-safe queue for async writes
class AsyncQueue {
private:
    std::queue<std::string> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};

public:
    // Producer: Push metric to queue (non-blocking)
    void push(const std::string& metric) {
        // TODO(human): Implement thread-safe push
        //
        // Steps:
        // 1. Lock the mutex
        // 2. Push metric to queue_
        // 3. Notify one waiting consumer (cv_.notify_one())
        //
        // Why notify? The consumer thread is waiting for data.
        // When you push, wake it up to process.

    }

    // Consumer: Pop metric from queue (blocks if empty)
    bool pop(std::string& metric) {
        std::unique_lock<std::mutex> lock(mutex_);

        // TODO(human): Wait for data or shutdown signal
        //
        // Use cv_.wait() with a condition (lambda):
        // cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        //
        // This blocks until:
        // - Queue has data (!queue_.empty()), OR
        // - Shutdown signal received (shutdown_)

        if (shutdown_ && queue_.empty()) {
            return false;  // No more data, exit consumer thread
        }

        // TODO(human): Pop from queue
        //
        // Steps:
        // 1. Get front element: metric = queue_.front()
        // 2. Remove it: queue_.pop()
        // 3. Return true (data available)

        return true;
    }

    // Signal shutdown (called when server stops)
    void shutdown() {
        // TODO(human): Set shutdown flag and wake all consumers
        //
        // Steps:
        // 1. Lock mutex
        // 2. Set shutdown_ = true
        // 3. Notify all waiting threads: cv_.notify_all()

    }
};

// Background writer thread
void writer_thread(AsyncQueue& queue, const std::string& filename) {
    std::cout << "[Writer] Thread started\n";

    // TODO(human): Implement batch writing
    //
    // Goal: Pull metrics from queue and write to file in batches
    //
    // Approach:
    // 1. Open file in append mode (outside the loop)
    // 2. Loop while data available:
    //    a. Pop metric from queue (blocks if empty)
    //    b. Write to file
    //    c. Optional: Flush periodically (every N writes for performance)
    // 3. Close file when done
    //
    // Why batching? Opening/closing file per write is slow.
    // Keep file open and flush periodically instead.

    std::ofstream file(filename, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "[Writer] Failed to open file\n";
        return;
    }

    std::string metric;
    int write_count = 0;

    while (queue.pop(metric)) {
        // TODO(human): Write metric to file
        // file << metric << "\n";

        write_count++;

        // TODO(human): Flush every 100 writes (performance optimization)
        // if (write_count % 100 == 0) {
        //     file.flush();
        // }
    }

    file.flush();
    file.close();
    std::cout << "[Writer] Wrote " << write_count << " metrics\n";
}

// Handle incoming request (producer)
void handle_request(AsyncQueue& queue, const std::string& json_data) {
    // TODO(human): Push to async queue instead of writing directly
    //
    // Before (Phase 1): Blocked on file write (slow)
    // After (Phase 2): Push to queue and return immediately (fast)
    //
    // This decouples request handling from I/O latency

    queue.push(json_data);
    // Request handling done! Writer thread handles actual I/O
}

int main() {
    std::cout << "=== Craft #1 Phase 2: Async I/O ===" << std::endl;

    AsyncQueue async_queue;

    // TODO(human): Start background writer thread
    //
    // This thread runs in the background, pulling from queue and writing
    // to file. It's the "consumer" in producer-consumer pattern.

    std::thread writer(/* TODO: call writer_thread with queue and filename */);

    // Simulate 200 concurrent requests
    const int num_requests = 200;
    std::vector<std::thread> request_threads;

    std::string sample_json = R"({"timestamp":"2025-01-01T12:00:00Z","name":"cpu","value":75.5})";

    for (int i = 0; i < num_requests; i++) {
        // TODO(human): Spawn thread to handle each request
        // request_threads.emplace_back(handle_request, std::ref(async_queue), sample_json);
    }

    // Wait for all requests to finish
    for (auto& t : request_threads) {
        // TODO(human): Join request threads
        // t.join();
    }

    std::cout << "All requests submitted to queue\n";

    // TODO(human): Signal shutdown and wait for writer to finish
    //
    // Steps:
    // 1. Call async_queue.shutdown() to signal no more data
    // 2. Join writer thread to wait for it to finish
    //
    // This ensures all queued data is written before program exits

    // async_queue.shutdown();
    // writer.join();

    std::cout << "\nNext: Benchmark to measure improvement" << std::endl;
    std::cout << "Expected: ~500+ RPS (vs ~200 RPS from Phase 1)" << std::endl;
    std::cout << "Why? Request handling no longer blocks on file I/O" << std::endl;

    return 0;
}

/**
 * Testing Instructions:
 *
 * 1. Compile:
 *    g++ -std=c++17 -pthread craft1_phase2_starter.cpp -o phase2
 *
 * 2. Run:
 *    ./phase2
 *
 * 3. Check output file:
 *    cat metrics.jsonl | wc -l
 *    (should show 200 lines)
 *
 * 4. Benchmark:
 *    Run with 50 clients, 10 requests each
 *    Expected: ~500+ RPS
 *
 * Key Learning:
 * - Producer-consumer decouples fast operations (network) from slow (disk)
 * - Request threads (producers) don't wait for I/O
 * - Writer thread (consumer) batches writes for efficiency
 * - This is the foundation of async systems (like Node.js, Go channels, Kafka)
 *
 * Trade-offs:
 * - Pro: Higher throughput, better latency
 * - Con: More memory (queue holds pending writes)
 * - Con: Potential data loss on crash (queue in-memory)
 * - Next phase will address memory usage with better data structures
 */
