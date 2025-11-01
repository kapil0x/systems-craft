# Phase 8: Kernel Tuning & epoll/kqueue for Scalability

**Date:** 2025-10-05
**Current State:** 2,000 RPS @ 99.5% success
**Goal:** 10,000+ RPS @ 99%+ success

---

## Bottleneck Identified

### The Real Limit: Kernel Listen Queue (somaxconn=128)

**Evidence:**
```bash
ulimit -n → 1,048,575 (FDs NOT the limit!)
sysctl kern.ipc.somaxconn → 128 (THIS is the limit!)
```

**What's Happening:**
- Server code requests `listen(fd, 1024)`
- But kernel silently caps it at `somaxconn=128`
- When 500 clients connect simultaneously:
  - First 128 enter queue
  - Remaining 372 get refused
  - As we accept(), new ones enter
  - But under load, queue fills faster than we drain

**Performance Cliff:**
- 200 clients: 99.51% success (queue mostly manages)
- 500 clients: 64.08% success (queue overwhelmed)

---

## Phase 8 Strategy: Multi-Pronged Optimization

We'll tackle this with THREE complementary optimizations:

### 1. ⚡ Increase Kernel Listen Queue (Quick Win)

**Change:**
```bash
sudo sysctl -w kern.ipc.somaxconn=4096
```

**Expected Impact:**
- Immediate capacity increase
- Can queue 4096 pending connections (vs 128)
- Should handle 500+ concurrent clients easily

**Limitation:**
- Only delays the problem
- Still have fixed limit
- Need better solution for true scalability

---

### 2. 🚀 Implement epoll/kqueue Event-Driven I/O (Major Upgrade)

**Current Architecture:**
```
Accept Loop Thread → blocks on accept()
Worker Threads (16) → handle requests
```

**Problem:**
- `accept()` is blocking
- One thread dedicated just to accepting
- Wastes CPU cycles waiting

**New Architecture (kqueue for macOS):**
```
Main Event Loop → kqueue() monitors multiple FDs simultaneously
  ├─ Listen socket becomes readable → accept() connections
  ├─ Client sockets become readable → read() requests
  └─ Process non-blocking → delegate to thread pool only for CPU work
```

**Why kqueue/epoll?**
- **Scalability:** Handle 10,000+ connections with ONE thread
- **Efficiency:** OS notifies us only when work is ready
- **Non-blocking:** No wasted cycles waiting
- **Industry Standard:** How Redis, nginx, Node.js scale

**Implementation Plan:**
```cpp
// Pseudocode
int kq = kqueue();

// Register listen socket
struct kevent change;
EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
kevent(kq, &change, 1, NULL, 0, NULL);

while (true) {
    struct kevent events[MAX_EVENTS];
    int nev = kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);

    for (int i = 0; i < nev; i++) {
        if (events[i].ident == listen_fd) {
            // New connection ready
            int client = accept(listen_fd, ...);
            // Add client to kqueue for reading
            EV_SET(&change, client, EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(kq, &change, 1, NULL, 0, NULL);
        } else {
            // Client data ready
            int client_fd = events[i].ident;
            // Read and process (delegate to thread pool for heavy work)
            thread_pool.enqueue([client_fd]() {
                handle_request(client_fd);
            });
        }
    }
}
```

---

### 3. 🎯 Batch Processing (Optimization)

**Idea:** When queue has multiple events, process in batch

```cpp
// Instead of:
for (each event) {
    thread_pool.enqueue(event);  // Lock/unlock per event
}

// Do:
std::vector<Task> batch;
for (each event) {
    batch.push_back(event);
}
thread_pool.enqueue_batch(batch);  // Single lock for entire batch
```

**Expected gain:** 20-30% throughput improvement

---

## Implementation Priority

### Phase 8A: Quick Win (30 minutes)
- Increase kernel `somaxconn` limit
- Re-test with 500, 1000 clients
- Document improvement

**Expected:** 5,000-8,000 RPS

### Phase 8B: Major Upgrade (2-4 hours)
- Implement kqueue event loop
- Integrate with existing thread pool
- Non-blocking I/O for network operations

**Expected:** 20,000+ RPS

### Phase 8C: Optimization (1 hour)
- Batch processing
- Fine-tune event loop parameters
- Profile and optimize hot paths

**Expected:** 30,000+ RPS

---

## Technical Details: kqueue vs Current Approach

### Current (Blocking I/O + Threads):
```
Scaling model: 1 thread per connection (roughly)
Cost per connection: ~8MB stack + scheduling overhead
Max connections: Limited by threads (~1000s)
CPU efficiency: Poor (threads block waiting)
```

### With kqueue (Event-Driven):
```
Scaling model: 1 event loop, N worker threads
Cost per connection: ~4KB state
Max connections: Tens of thousands
CPU efficiency: Excellent (only work when data ready)
```

---

## Learning Opportunity

This phase introduces **event-driven programming** - a fundamental pattern in high-performance systems.

**Key Concepts:**
1. **Level-triggered vs Edge-triggered events**
2. **Non-blocking I/O**
3. **Reactor pattern**
4. **How nginx/Redis achieve C10K problem solution**

---

## Success Metrics

### Phase 8A (Kernel Tuning):
- [ ] 500 clients: 95%+ success
- [ ] 1000 clients: 90%+ success
- [ ] 5,000-8,000 RPS sustained

### Phase 8B (kqueue):
- [ ] 1000 clients: 99%+ success
- [ ] 5000 clients: 95%+ success
- [ ] 20,000+ RPS sustained
- [ ] <1ms latency at 10,000 RPS

### Phase 8C (Optimized):
- [ ] 10,000 clients: 99%+ success
- [ ] 30,000+ RPS sustained
- [ ] CPU usage <80% at max load

---

## Questions to Explore

1. **Why does kqueue scale better than threads?**
   - Memory: 4KB vs 8MB per connection
   - Context switching: Minimal vs heavy
   - Notification: OS tells us when ready (vs polling)

2. **When should we still use threads?**
   - CPU-intensive work (JSON parsing, validation)
   - Blocking operations (if any)
   - Parallel processing

3. **What's the theoretical limit?**
   - Network bandwidth
   - CPU for parsing/processing
   - Memory for connection state
   - Disk I/O for metrics writing

---

## Next Steps

Ready to implement Phase 8A (kernel tuning)?

```bash
# 1. Increase kernel limit
sudo sysctl -w kern.ipc.somaxconn=4096

# 2. Re-test
./measure_rps.sh

# 3. Compare before/after
# Before: 500 clients = 64% success
# After:  500 clients = ??% success
```

Let's start with the quick win and measure the improvement!

