# Craft #1: Metrics Ingestion Service

**Component:** High-performance HTTP API for metrics ingestion
**Learning Goal:** Build and optimize a production-grade ingestion service from first principles
**Total Time:** 8-12 hours across 8 phases
**Final Performance:** 2,253 RPS sustained, 100% reliability, 0.25ms p50 latency

---

## Overview

Craft #1 teaches you how to build a high-performance HTTP server through **systematic measurement and optimization**. You'll start with a simple threaded server and progressively optimize it through 8 phases, learning core systems engineering principles along the way.

**Key Learning:** Measure first, optimize second. Every phase targets a measured bottleneck, not theoretical improvements.

---

## Architecture Evolution

### Starting Point (Phase 1)
```
HTTP Request → accept() blocks → spawn thread → process → response
```
- **Performance:** 88% success @ 20 clients
- **Bottleneck:** Blocking accept(), thread creation overhead

### Final State (Phase 7)
```
HTTP Request → epoll/kqueue → thread pool → process → persistent connection → response
         ↓                           ↓
  Keep-Alive enabled          Lock-free metrics
```
- **Performance:** 2,253 RPS sustained, 100% success
- **Optimizations:** Event-driven I/O, connection pooling, lock-free data structures

---

## Phase Breakdown

### Phase 1: Threading per Request ✅

**Location:** `src/` (baseline implementation)

**Bottleneck Identified:**
- Single-threaded `accept()` loop blocks on each connection
- Sequential processing limits concurrency

**Solution Implemented:**
- Spawn new thread for each incoming connection
- Threads handle request parsing, processing, and response

**Results:**
- **Success Rate:** 88% @ 20 clients
- **Latency:** N/A (baseline)
- **Key Learning:** Thread-per-connection model works at small scale but doesn't scale

**Limitations:**
- Thread creation is expensive (~1ms overhead)
- Memory overhead: ~8MB stack per thread
- Kernel scheduler overhead with many threads

**Next Bottleneck:** Blocking file I/O in request thread

---

### Phase 2: Async I/O (Producer-Consumer Pattern) ✅

**Bottleneck Identified:**
- File writes block request processing thread
- Each request waits for disk I/O to complete

**Solution Implemented:**
- Producer-consumer pattern with background writer thread
- Lock-protected queue buffers metrics between ingestion and file writing
- Request threads return immediately after queuing

**Results:**
- **Success Rate:** 66% @ 50 clients
- **Latency:** N/A
- **Key Learning:** Decoupling I/O from request processing improves throughput

**Implementation Details:**
```cpp
// Request thread (producer)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    metrics_queue.push(metric);
}
send_response(200, "OK");

// Background thread (consumer)
while (running) {
    Metric m = queue.pop();
    write_to_file(m);  // Blocking I/O isolated here
}
```

**Limitations:**
- Still spawning thread per connection
- Mutex contention on shared queue
- No backpressure mechanism

**Next Bottleneck:** JSON parsing overhead (O(n²) string operations)

---

### Phase 3: JSON Parsing Optimization ✅

**Documentation:** [phase-3-json-parsing/results.md](phase-3-json-parsing/results.md)

**Bottleneck Identified:**
- Multiple `string::find()` calls per JSON field
- O(n²) complexity due to repeated string scanning
- Profiler showed 40% time in JSON parsing

**Solution Implemented:**
- Single-pass character iteration (O(n))
- Direct numeric parsing with `strtod()`
- Pre-allocated buffers to reduce memory allocations

**Results:**
- **Success Rate:** 80.2% @ 100 clients
- **Latency:** 2.73ms average
- **Key Learning:** Algorithm optimization > micro-optimizations

**Before (Multi-Pass):**
```cpp
size_t name_start = json.find("\"name\":\"") + 8;
size_t name_end = json.find("\"", name_start);
std::string name = json.substr(name_start, name_end - name_start);
```

**After (Single-Pass):**
```cpp
// Parse entire JSON in one pass
for (size_t i = 0; i < len; i++) {
    switch (state) {
        case EXPECT_NAME_KEY:
            if (json[i] == '"' && matches_at(json, i+1, "name")) {
                state = EXPECT_NAME_VALUE;
            }
            break;
        // ... handle all states in single pass
    }
}
```

**Limitations:**
- Still creating thread per connection (resource exhaustion at scale)
- Global rate limiting mutex causes contention

**Next Bottleneck:** Thread creation limits, mutex contention

---

### Phase 4: Per-Client Mutex Pools ✅

**Bottleneck Identified:**
- Global rate limiting mutex serializes all requests
- Profiler showed mutex wait time at 15-20%

**Solution Implemented:**
- Hash-based sharding: `client_id → hash → mutex_pool[hash % pool_size]`
- 64 mutexes instead of 1 (reduces contention by 64x)

**Results:**
- **Success Rate:** Incremental improvement
- **Key Learning:** Lock granularity matters - fine-grained locks reduce contention

**Implementation:**
```cpp
const int MUTEX_POOL_SIZE = 64;
std::array<std::mutex, MUTEX_POOL_SIZE> rate_limit_mutexes;

int get_mutex_index(const std::string& client_id) {
    return std::hash<std::string>{}(client_id) % MUTEX_POOL_SIZE;
}

// In rate_limit check:
int idx = get_mutex_index(client_id);
std::lock_guard<std::mutex> lock(rate_limit_mutexes[idx]);
// ... check rate limit ...
```

**Limitations:**
- Unbounded thread creation still a problem
- Memory exhaustion at 150+ concurrent clients

**Next Bottleneck:** Thread management overhead

---

### Phase 5: Thread Pool Architecture ✅

**Bottleneck Identified:**
- Unlimited thread spawning causes resource exhaustion
- Thread creation overhead: ~1ms per connection
- Context switching overhead with 500+ threads

**Solution Implemented:**
- Fixed-size thread pool (16 worker threads)
- Bounded work queue (1024 tasks)
- Worker threads process tasks from queue

**Results:**
- **Success Rate:** 100% @ 100 clients
- **Latency:** 0.65ms average
- **Key Learning:** Resource pooling provides bounded concurrency and predictable performance

**Architecture:**
```
Accept Loop → Enqueue Task → [Work Queue] → Worker Thread Pool → Process
                                              (16 threads)
```

**Implementation:**
```cpp
class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<Task> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;

public:
    ThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back([this] {
                while (true) {
                    Task task = dequeue();
                    task();  // Execute
                }
            });
        }
    }
};
```

**Limitations:**
- Metrics collection still uses mutex (lock overhead)
- Not yet using event-driven I/O for network

**Next Bottleneck:** Lock overhead in hot path

---

### Phase 6: Lock-Free Ring Buffers ✅

**Bottleneck Identified:**
- Mutex acquisition in metrics collection hot path
- ~5-10% CPU time spent in mutex operations
- Serialization point that doesn't scale

**Solution Implemented:**
- Lock-free concurrent ring buffer using atomic operations
- CAS (compare-and-swap) for slot reservation
- Memory ordering constraints for correctness

**Results:**
- **Overhead:** Eliminated metrics collection bottleneck
- **Key Learning:** Lock-free data structures avoid serialization but require careful memory ordering

**Implementation Highlights:**
```cpp
template<typename T, size_t Size>
class LockFreeRingBuffer {
    std::array<T, Size> buffer;
    std::atomic<size_t> write_pos{0};
    std::atomic<size_t> read_pos{0};

public:
    bool try_push(const T& item) {
        size_t current_write = write_pos.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % Size;

        // Check if buffer is full
        if (next_write == read_pos.load(std::memory_order_acquire)) {
            return false;  // Full
        }

        // Write item
        buffer[current_write] = item;

        // Update write position
        write_pos.store(next_write, std::memory_order_release);
        return true;
    }
};
```

**Key Concepts:**
- **memory_order_acquire/release:** Ensures proper visibility of writes across threads
- **CAS loops:** Retry on contention instead of blocking
- **False sharing:** Pad atomic variables to cache line boundaries

**Limitations:**
- TCP connection overhead still present (new connection per request)

**Next Bottleneck:** TCP handshake overhead

---

### Phase 7: HTTP Keep-Alive ✅ **[CURRENT BEST]**

**Documentation:** [../docs/phase7_keep_alive_results.md](../docs/phase7_keep_alive_results.md)

**Bottleneck Identified:**
- TCP handshake overhead: 1-2ms per request
- Connection setup cost amortized across multiple requests
- Listen backlog (10) too small for concurrent bursts

**Solution Implemented:**
- HTTP/1.1 persistent connections (Connection: keep-alive)
- Socket timeout (60 seconds idle)
- Increased listen backlog to 1024
- Request loop per connection instead of close-after-response

**Results:**
- **Success Rate:** 100% @ 100 clients
- **Throughput:** 2,253 RPS sustained
- **Latency:** 0.25ms p50, 0.65ms p99
- **Key Learning:** Connection pooling eliminates per-request overhead

**Before (One Request Per Connection):**
```cpp
void handle_client(int client_fd) {
    read_request(client_fd);
    process_request();
    send_response(client_fd);
    close(client_fd);  // Connection closed
}
```

**After (Persistent Connections):**
```cpp
void handle_client(int client_fd) {
    // Set 60-second socket timeout
    struct timeval timeout = {60, 0};
    setsockopt(client_fd, SO_RCVTIMEO, &timeout);

    while (true) {
        if (!read_request(client_fd)) break;  // Timeout or close
        process_request();
        send_response(client_fd, "Connection: keep-alive");
        // Connection stays open for next request
    }
    close(client_fd);
}
```

**Performance Improvements:**
- Eliminated 99% of TCP handshakes for multi-request sessions
- 4x improvement in per-request latency (2ms → 0.25ms)
- 100% success rate vs 46-51% in Phase 6

**Configuration Changes:**
```cpp
// Increased listen backlog
listen(server_fd, 1024);  // Was: listen(server_fd, 10)

// Added socket timeout
struct timeval tv = {60, 0};  // 60 seconds
setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

**Limitations:**
- Still limited by thread pool size (16 threads)
- Each persistent connection holds a thread
- Can't efficiently handle 10,000+ concurrent connections

**Next:** Phase 8 will address this with event-driven I/O

---

### Phase 8: Event-Driven I/O (epoll/kqueue) 🚧

**Documentation:** [../phase8_design.md](../phase8_design.md)
**Implementation:** `src/event_loop.cpp` (partial)

**Bottleneck Identified:**
- Thread-per-connection limits scalability (16 threads = 16 concurrent connections)
- Thread context switching overhead
- Can't handle 10,000+ concurrent clients (C10K problem)

**Solution Proposed:**
- Event loop using epoll (Linux) or kqueue (macOS)
- Single thread monitors multiple file descriptors
- Non-blocking I/O with OS-level event notifications
- Thread pool only for CPU-bound work (parsing, processing)

**Architecture:**
```
             ┌─── Listen Socket (EVFILT_READ)
             │
kqueue() ────┼─── Client Socket 1 (EVFILT_READ)
             │
             ├─── Client Socket 2 (EVFILT_READ)
             │
             └─── ... (10,000+ sockets)

Event Loop (1 thread) → Thread Pool (CPU work) → Response
```

**Expected Results:**
- **Throughput:** 20,000+ RPS
- **Concurrency:** 10,000+ simultaneous connections
- **Latency:** <1ms p99
- **Memory:** 4KB per connection (vs 8MB per thread)

**Implementation Status:**
- ✅ Event loop structure created
- ✅ kqueue integration added
- 🚧 Integration with thread pool incomplete
- 🚧 Not tested under load

**Key Concepts:**
- **Level-triggered vs edge-triggered:** How events are reported
- **Reactor pattern:** Event loop dispatches to handlers
- **Non-blocking I/O:** Operations return immediately (EAGAIN/EWOULDBLOCK)

**Blocking I/O (Phase 7):**
```cpp
// Blocks until data available
read(client_fd, buffer, size);
```

**Non-blocking I/O (Phase 8):**
```cpp
// Set non-blocking
fcntl(client_fd, F_SETFL, O_NONBLOCK);

// Returns immediately
ssize_t n = read(client_fd, buffer, size);
if (n == -1 && errno == EAGAIN) {
    // No data ready, try again later
    register_for_read_event(client_fd);
}
```

**Next Steps:**
1. Complete integration with existing ingestion service
2. Add connection state management
3. Load testing at 1000, 5000, 10000 clients
4. Document results

**Target Performance:**
- 20,000-30,000 RPS sustained
- 99%+ success @ 10,000 concurrent clients
- <1ms latency p99

---

## Performance Summary

| Phase | Optimization | Success Rate | Latency | Key Learning |
|-------|-------------|--------------|---------|--------------|
| **1** | Threading | 88% @ 20 clients | Baseline | Concurrency basics |
| **2** | Async I/O | 66% @ 50 clients | - | Producer-consumer pattern |
| **3** | JSON parsing | 80% @ 100 clients | 2.73ms | Algorithm optimization |
| **4** | Mutex pools | Incremental | - | Lock granularity |
| **5** | Thread pool | 100% @ 100 clients | 0.65ms | Resource pooling |
| **6** | Lock-free | 100% @ 100 clients | 0.65ms | Lock-free data structures |
| **7** | Keep-Alive | **100% @ 100 clients** | **0.25ms** | **Connection pooling** |
| **8** | Event loop | Target: 10K clients | <1ms | Event-driven I/O |

**Current Production-Ready State:** Phase 7 (2,253 RPS, 100% reliability)

---

## Key Learnings Across All Phases

### 1. Measure Before Optimizing
- Use profilers (perf, Instruments) to find actual bottlenecks
- Don't optimize based on assumptions
- Document baseline before each phase

### 2. Scalability Patterns
- **Concurrency:** Thread pool > unlimited threads
- **I/O:** Async > blocking
- **Data structures:** Lock-free > mutexes (in hot paths)
- **Connections:** Persistent > per-request

### 3. Trade-offs
- **Thread pool size:** Too small → poor concurrency, too large → context switching
- **Lock granularity:** Coarse → contention, fine → complexity
- **Buffer size:** Small → frequent I/O, large → memory usage

### 4. Systems Engineering Mindset
- Start simple, optimize based on measurements
- Each optimization introduces complexity
- Know when to stop (diminishing returns)

---

## Implementation Reference

**Main Files:**
- `src/main.cpp` - Entry point
- `src/http_server.cpp` - HTTP protocol handling, persistent connections
- `src/ingestion_service.cpp` - Request processing, rate limiting, JSON parsing
- `src/thread_pool.cpp` - Fixed-size worker pool
- `src/event_loop.cpp` - Event-driven I/O (Phase 8, partial)

**Testing:**
- `load_test.cpp` - Concurrent connection testing
- `load_test_persistent.cpp` - Persistent connection testing
- `performance_test.sh` - Systematic load testing script

---

## Next Steps

### To Complete Craft #1:
1. Finish Phase 8 implementation (event loop integration)
2. Load test at 10,000 concurrent clients
3. Document Phase 8 results
4. Create consolidated tutorial covering all phases

### To Start Craft #2:
Once you understand high-performance ingestion, move to **Craft #2: Message Queue** to learn how to buffer and distribute these metrics across consumers.

---

## Related Documentation

- **[Phase 7 Results](../docs/phase7_keep_alive_results.md)** - Detailed analysis of HTTP Keep-Alive optimization
- **[Phase 8 Design](../phase8_design.md)** - Event-driven I/O architecture
- **[Performance Results](../performance_results.txt)** - Raw benchmark data
- **[Architecture](../docs/ARCHITECTURE.md)** - Overall system design
- **[Capacity Planning](../docs/CAPACITY_PLANNING.md)** - Scaling analysis

---

**Total Time Investment:** 8-12 hours
**Current Status:** Phase 7 complete, Phase 8 in progress
**Production Ready:** Yes (Phase 7 achieves production-grade performance)
