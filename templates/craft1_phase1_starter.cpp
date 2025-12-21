/**
 * Craft #1 - Phase 1 Starter Template
 * Goal: Add threading per request (handle concurrent requests)
 *
 * Baseline: ~50 RPS with single-threaded processing
 * Target: ~200+ RPS with thread-per-request
 *
 * What you'll learn:
 * - std::thread basics
 * - Thread management and join()
 * - Why threading improves throughput
 */

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <mutex>

// Global mutex for thread-safe file writing
std::mutex file_mutex;

// Process a single metric (called per request)
void handle_metric(const std::string& json_data) {
    // TODO(human): Parse JSON data
    // Extract: timestamp, name, value, client_id
    //
    // Hint: For Phase 1, you can use a simple string find/substr approach
    // Later phases will optimize this parser
    //
    // Example input: {"timestamp":"2025-01-01T12:00:00Z","name":"cpu","value":75.5,"client_id":"client1"}

    // TODO(human): Write to file (thread-safe)
    //
    // Remember: Multiple threads will call this simultaneously
    // Use file_mutex to protect the write
    //
    // Steps:
    // 1. Lock file_mutex
    // 2. Open metrics.jsonl in append mode
    // 3. Write the json_data + newline
    // 4. Close file (or flush)
    // 5. Unlock happens automatically when lock_guard goes out of scope

    std::cout << "[Thread " << std::this_thread::get_id() << "] Processing metric\n";
}

// Main server loop (simplified for learning)
int main() {
    std::cout << "=== Craft #1 Phase 1: Thread-per-Request ===" << std::endl;
    std::cout << "Simulating 100 concurrent requests..." << std::endl;

    const int num_requests = 100;
    std::vector<std::thread> threads;

    // TODO(human): Create a thread for each request
    //
    // Goal: Handle multiple requests concurrently
    //
    // Steps:
    // 1. Loop num_requests times
    // 2. For each request, create a std::thread that calls handle_metric()
    // 3. Store each thread in the threads vector
    //
    // Example thread creation:
    //   threads.emplace_back(handle_metric, json_data);

    // Create sample JSON data for testing
    std::string sample_json = R"({"timestamp":"2025-01-01T12:00:00Z","name":"cpu_usage","value":75.5,"client_id":"test"})";

    for (int i = 0; i < num_requests; i++) {
        // TODO(human): Spawn thread here
        // Uncomment and complete:
        // threads.emplace_back(/* your code here */);
    }

    // TODO(human): Wait for all threads to complete
    //
    // Steps:
    // 1. Loop through all threads
    // 2. Call join() on each thread to wait for completion
    //
    // Why join()? Without it, main() exits before threads finish,
    // causing undefined behavior (crash or incomplete work)

    for (auto& thread : threads) {
        // TODO(human): Join thread here
        // Uncomment:
        // thread.join();
    }

    std::cout << "All requests processed!" << std::endl;
    std::cout << "\nNext: Run the benchmark to measure RPS improvement" << std::endl;
    std::cout << "Expected: ~200+ RPS (vs ~50 RPS baseline)" << std::endl;

    return 0;
}

/**
 * Testing Instructions:
 *
 * 1. Compile:
 *    g++ -std=c++17 -pthread craft1_phase1_starter.cpp -o phase1
 *
 * 2. Run:
 *    ./phase1
 *
 * 3. Benchmark (after implementing the HTTP server wrapper):
 *    Open website/benchmark.html
 *    Server URL: http://localhost:8080
 *    Run with 20 clients, 10 requests each
 *
 * 4. Expected Results:
 *    - Baseline (single-thread): ~50 RPS
 *    - Phase 1 (thread-per-request): ~200 RPS
 *    - Improvement: 4x throughput increase
 *
 * Key Learning:
 * - Threading allows concurrent processing
 * - CPU can handle multiple requests simultaneously
 * - But: Creating threads has overhead (Phase 2 will optimize with thread pool)
 * - Mutex contention on file I/O becomes the next bottleneck
 */
