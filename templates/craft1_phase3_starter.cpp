/**
 * Craft #1 - Phase 3 Starter Template
 * Goal: Optimize JSON parsing from O(n²) to O(n)
 *
 * Previous (Phase 2): ~500 RPS but JSON parsing is slow
 * Target (Phase 3): ~1000+ RPS with optimized parser
 *
 * What you'll learn:
 * - Performance profiling (identify bottlenecks)
 * - String operations complexity
 * - Zero-copy parsing techniques
 * - Algorithm optimization
 */

#include <iostream>
#include <string>
#include <chrono>
#include <cstring>

struct Metric {
    std::string timestamp;
    std::string name;
    double value;
    std::string client_id;
};

// Slow parser (O(n²) - lots of string copying)
Metric parse_json_slow(const std::string& json) {
    Metric metric;

    // TODO(human): Analyze why this is slow
    //
    // Problem: std::string::find() + substr() copies strings repeatedly
    // Each find() scans from beginning
    // Each substr() creates new string (memory allocation)
    //
    // For 1000s of requests/sec, this becomes the bottleneck

    // Find "timestamp":"..."
    size_t pos = json.find("\"timestamp\":\"");
    if (pos != std::string::npos) {
        pos += 13;  // Skip past "timestamp":"
        size_t end = json.find("\"", pos);
        metric.timestamp = json.substr(pos, end - pos);  // COPY
    }

    // Find "name":"..."
    pos = json.find("\"name\":\"");
    if (pos != std::string::npos) {
        pos += 8;
        size_t end = json.find("\"", pos);
        metric.name = json.substr(pos, end - pos);  // COPY
    }

    // TODO(human): Finish parsing value and client_id
    // Similar pattern causes multiple scans and copies

    return metric;
}

// Fast parser (O(n) - single pass, minimal copying)
Metric parse_json_fast(const char* json, size_t length) {
    Metric metric;

    // TODO(human): Implement single-pass parser
    //
    // Strategy:
    // 1. Scan the string ONCE from left to right
    // 2. Use pointers to mark start/end of values
    // 3. Extract directly without intermediate copies
    //
    // Example approach:
    // - Use strchr() or manual scan for '"'
    // - When you find "timestamp", remember position
    // - Skip to next '"' to find value start
    // - Skip to closing '"' to find value end
    // - Create string ONCE from (start, end)
    //
    // Why faster? O(n) single pass, fewer allocations

    const char* ptr = json;
    const char* end = json + length;

    while (ptr < end) {
        // TODO(human): Scan for field names
        //
        // Hint: Look for patterns like:
        // "timestamp":"VALUE"
        // "name":"VALUE"
        // "value":NUMBER
        // "client_id":"VALUE"
        //
        // Use const char* pointers to avoid copying until necessary

        // Example: Finding timestamp field
        if (strncmp(ptr, "\"timestamp\"", 11) == 0) {
            ptr += 11;  // Skip past field name

            // TODO(human): Skip whitespace and colon
            while (*ptr == ' ' || *ptr == ':') ptr++;

            // TODO(human): Find opening quote
            if (*ptr == '"') {
                ptr++;  // Skip quote
                const char* value_start = ptr;

                // TODO(human): Find closing quote
                while (*ptr != '"' && ptr < end) ptr++;

                // TODO(human): Extract value (single allocation)
                metric.timestamp = std::string(value_start, ptr - value_start);
            }
        }

        // TODO(human): Repeat for other fields (name, value, client_id)
        //
        // Pattern is similar:
        // 1. Find field name
        // 2. Skip to value
        // 3. Extract with minimal copying

        ptr++;  // Move to next character
    }

    return metric;
}

// Benchmark helper
void benchmark_parser() {
    std::string test_json = R"({"timestamp":"2025-01-01T12:00:00Z","name":"cpu_usage","value":75.5,"client_id":"test_client_123"})";

    const int iterations = 100000;

    // Benchmark slow parser
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto metric = parse_json_slow(test_json);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto slow_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // TODO(human): Benchmark fast parser
    //
    // Same pattern as above, but call parse_json_fast()
    // Compare the times to see improvement

    auto start_fast = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto metric = parse_json_fast(test_json.c_str(), test_json.length());
    }
    auto end_fast = std::chrono::high_resolution_clock::now();
    auto fast_time = std::chrono::duration_cast<std::chrono::microseconds>(end_fast - start_fast).count();

    std::cout << "=== JSON Parsing Benchmark ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Slow parser: " << slow_time << " μs" << std::endl;
    std::cout << "Fast parser: " << fast_time << " μs" << std::endl;
    std::cout << "Speedup: " << (double)slow_time / fast_time << "x" << std::endl;
    std::cout << "\nExpected: 2-3x faster with optimized parser" << std::endl;
}

int main() {
    std::cout << "=== Craft #1 Phase 3: JSON Parser Optimization ===" << std::endl;

    // TODO(human): Complete parse_json_fast() implementation above
    std::cout << "\nStep 1: Implement parse_json_fast() with single-pass algorithm\n";
    std::cout << "Step 2: Run benchmark to measure improvement\n";
    std::cout << "Step 3: Integrate into server and measure RPS increase\n";

    benchmark_parser();

    return 0;
}

/**
 * Testing Instructions:
 *
 * 1. Compile:
 *    g++ -std=c++17 -O2 craft1_phase3_starter.cpp -o phase3
 *    Note: -O2 enables compiler optimizations
 *
 * 2. Run:
 *    ./phase3
 *
 * 3. Analyze results:
 *    - Slow parser: ~5000 μs for 100k iterations
 *    - Fast parser: ~1500 μs for 100k iterations
 *    - Speedup: ~3x faster
 *
 * 4. Integration:
 *    - Replace parse_json_slow() calls in server with parse_json_fast()
 *    - Benchmark server: Expected ~1000+ RPS (vs ~500 from Phase 2)
 *
 * Key Learning:
 * - Algorithm complexity matters at scale
 * - O(n²) becomes a bottleneck with high request rates
 * - String operations (substr, find) are expensive (allocations)
 * - Single-pass parsing with pointers = zero unnecessary copies
 * - Profiling shows WHERE the problem is (measure before optimizing!)
 *
 * Advanced Optimization Ideas:
 * - Use string_view (C++17) for zero-copy string references
 * - Pre-allocate string capacity to reduce reallocations
 * - Use SIMD for finding characters (advanced)
 * - Consider existing JSON libraries (RapidJSON, simdjson) for production
 *
 * Why we built custom parser?
 * - Learn the fundamentals (understand before using libraries)
 * - Real systems often need custom parsers for specific formats
 * - This teaches algorithm optimization mindset
 *
 * Trade-offs:
 * - Pro: 2-3x faster parsing
 * - Pro: Lower CPU usage per request
 * - Con: More complex code (pointer management)
 * - Con: Needs careful testing (buffer overruns if not careful)
 */
