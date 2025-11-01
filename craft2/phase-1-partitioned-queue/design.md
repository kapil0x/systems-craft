# Phase 9: File-Based Partitioned Message Queue

**Date:** 2025-10-12
**Status:** 🔜 Design Phase
**Learning Goal:** Understand message queue fundamentals - partitioning, offsets, producer/consumer patterns

---

## Executive Summary

Phase 9 introduces a **file-based partitioned message queue** that decouples the ingestion service from downstream processing. This phase teaches the core concepts behind distributed message queues like Kafka before migrating to production systems in Phase 11.

**What We're Building:**
- Partitioned file-based queue (hash-based partitioning by client_id)
- Producer interface integrated into ingestion service
- Consumer service that reads and processes messages
- Offset tracking for reliable message delivery

**Why Build This (vs using Kafka directly):**
- Understand partitioning, offsets, and durability from first principles
- See exactly how message ordering and parallel processing work
- Appreciate Kafka's optimizations when we migrate in Phase 11

---

## Architecture Overview

### Current System (Phase 8)

```
┌──────────────┐    ┌────────────────────────┐    ┌──────────────┐
│  HTTP Client │───>│  Ingestion Service     │───>│ metrics.jsonl│
│              │    │  (epoll event loop)    │    │  (file)      │
└──────────────┘    └────────────────────────┘    └──────────────┘

Problem: Tightly coupled - ingestion must wait for disk writes
```

### Phase 9 System (Decoupled with Queue)

```
┌──────────────┐    ┌─────────────────────┐    ┌──────────────────────┐
│  HTTP Client │───>│ Ingestion Service   │───>│  Partitioned Queue   │
│              │    │   (Producer)        │    │                      │
└──────────────┘    └─────────────────────┘    │  queue/              │
                                                │  ├─ partition-0/     │
                                                │  ├─ partition-1/     │
                                                │  ├─ partition-2/     │
                                                │  └─ partition-3/     │
                                                └──────────────────────┘
                                                          ↓
                                                ┌──────────────────────┐
                                                │  Consumer Service    │
                                                │  (reads & processes) │
                                                └──────────────────────┘

Benefits:
- Ingestion can write fast, consumer processes at its own pace
- Multiple consumers can read from different partitions in parallel
- System keeps working even if consumer is down (buffering)
```

---

## Core Concepts (Learning Focus)

### 1. Partitioning

**What:** Split messages across multiple independent queues (partitions)

**Why:**
- **Parallelism:** Multiple consumers can process different partitions simultaneously
- **Ordering:** Messages from same client stay in order (within partition)
- **Load balancing:** Distribute load evenly across partitions

**Strategy:** Hash-based partitioning
```
partition = hash(client_id) % num_partitions

Example with 4 partitions:
- client_abc → hash → 3421 % 4 = 1 → partition-1
- client_xyz → hash → 8847 % 4 = 3 → partition-3
- client_abc (again) → always partition-1 (deterministic!)
```

**Key Insight:** Same as Kafka's default partitioning strategy (ADR-004)

---

### 2. Offsets

**What:** Sequential position number for each message in a partition

**Why:**
- **Tracking:** Know which messages have been processed
- **Resumability:** If consumer crashes, resume from last committed offset
- **Idempotency:** Don't process same message twice

**Example:**
```
partition-0/
├── 00000000000001.msg  ← offset 1
├── 00000000000002.msg  ← offset 2
├── 00000000000003.msg  ← offset 3
└── offset.txt          ← next write: 4

Consumer tracks:
- Last read offset: 2
- Last committed offset: 2 (safe to restart from here)
```

---

### 3. Producer/Consumer Pattern

**Producer (Ingestion Service):**
- Writes messages to queue
- Returns immediately (async)
- Doesn't wait for consumer

**Consumer (New Service):**
- Reads messages from queue
- Processes at its own pace
- Commits offset after successful processing

**Decoupling Benefits:**
- Producer doesn't block on slow processing
- Consumer can lag without affecting ingestion
- Easy to add more consumers

---

## Detailed Design

### Component 1: PartitionedQueue (Producer Side)

**Purpose:** Write interface for the queue - ingestion service writes here

**Class Definition:**
```cpp
class PartitionedQueue {
private:
    std::string base_path_;              // "queue/"
    int num_partitions_;                 // Default: 4
    std::vector<std::mutex> mutexes_;    // One per partition (parallel writes)
    std::vector<uint64_t> offsets_;      // Next write offset per partition

public:
    // Initialize queue directory structure
    PartitionedQueue(const std::string& path, int num_partitions);

    // Write message to appropriate partition
    // Returns: partition number and offset where written
    std::pair<int, uint64_t> produce(const std::string& key,
                                      const std::string& message);

    // Determine partition for a key
    int get_partition(const std::string& key) const;

    // Load offsets from disk on startup
    void load_offsets();

    // Update offset tracking file
    void update_offset_file(int partition, uint64_t offset);
};
```

**Key Methods:**

**1. Constructor - Initialize Queue**
```cpp
PartitionedQueue::PartitionedQueue(const std::string& path, int num_partitions)
    : base_path_(path), num_partitions_(num_partitions) {

    // Create directory structure
    std::filesystem::create_directories(base_path_);

    for (int i = 0; i < num_partitions_; i++) {
        std::string partition_path = base_path_ + "/partition-" + std::to_string(i);
        std::filesystem::create_directories(partition_path);

        // Initialize mutex and offset for this partition
        mutexes_.emplace_back();
        offsets_.push_back(0);
    }

    // Load existing offsets from disk
    load_offsets();
}
```

**2. Produce - Write Message**
```cpp
std::pair<int, uint64_t> PartitionedQueue::produce(
    const std::string& key,
    const std::string& message) {

    // 1. Determine partition using hash
    int partition = get_partition(key);

    // 2. Lock only this partition (allows parallel writes to other partitions)
    std::lock_guard<std::mutex> lock(mutexes_[partition]);

    // 3. Get next offset
    uint64_t offset = ++offsets_[partition];

    // 4. Write message to file
    std::string filename = base_path_ + "/partition-" + std::to_string(partition)
                         + "/" + format_offset(offset) + ".msg";

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    file << message;
    file.flush();

    // 5. Ensure durability (fsync)
    int fd = fileno(file);
    fsync(fd);

    // 6. Update offset tracking file
    update_offset_file(partition, offsets_[partition]);

    return {partition, offset};
}
```

**3. Get Partition - Hash-Based Routing**
```cpp
int PartitionedQueue::get_partition(const std::string& key) const {
    // Use std::hash for deterministic partitioning
    std::hash<std::string> hasher;
    size_t hash_value = hasher(key);
    return hash_value % num_partitions_;
}
```

**Helper Functions:**
```cpp
// Format offset as zero-padded string: 1 → "00000000000001"
std::string format_offset(uint64_t offset) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(20) << offset;
    return oss.str();
}

// Update offset tracking file for a partition
void PartitionedQueue::update_offset_file(int partition, uint64_t offset) {
    std::string offset_file = base_path_ + "/partition-" + std::to_string(partition)
                            + "/offset.txt";
    std::ofstream file(offset_file);
    file << offset;
}

// Load offsets from disk on startup
void PartitionedQueue::load_offsets() {
    for (int i = 0; i < num_partitions_; i++) {
        std::string offset_file = base_path_ + "/partition-" + std::to_string(i)
                                + "/offset.txt";
        std::ifstream file(offset_file);
        if (file.is_open()) {
            file >> offsets_[i];
        } else {
            offsets_[i] = 0;  // Start from 0 if no offset file
        }
    }
}
```

---

### Component 2: QueueConsumer (Consumer Side)

**Purpose:** Read interface for the queue - processes messages

**Class Definition:**
```cpp
struct Message {
    int partition;
    uint64_t offset;
    std::string data;
};

class QueueConsumer {
private:
    std::string queue_path_;              // "queue/"
    std::string consumer_group_;          // "storage-writer" (for offset tracking)
    int num_partitions_;
    std::vector<uint64_t> read_offsets_;  // Last read offset per partition
    bool running_;

public:
    QueueConsumer(const std::string& queue_path,
                  const std::string& consumer_group,
                  int num_partitions);

    // Start consuming (spawns threads for each partition)
    void start();

    // Stop consuming gracefully
    void stop();

    // Process single partition (runs in thread)
    void consume_partition(int partition);

    // Read next message from partition
    std::optional<Message> read_next(int partition);

    // Commit offset after successful processing
    void commit_offset(int partition, uint64_t offset);

    // Load committed offsets from disk
    void load_offsets();
};
```

**Key Methods:**

**1. Constructor - Initialize Consumer**
```cpp
QueueConsumer::QueueConsumer(const std::string& queue_path,
                             const std::string& consumer_group,
                             int num_partitions)
    : queue_path_(queue_path),
      consumer_group_(consumer_group),
      num_partitions_(num_partitions),
      running_(false) {

    // Initialize read offsets
    read_offsets_.resize(num_partitions_, 0);

    // Create consumer offset directory
    std::string offset_dir = "consumer_offsets/" + consumer_group_;
    std::filesystem::create_directories(offset_dir);

    // Load committed offsets from disk
    load_offsets();
}
```

**2. Start - Main Consumer Loop**
```cpp
void QueueConsumer::start() {
    running_ = true;

    std::cout << "Starting consumer with " << num_partitions_ << " partitions...\n";

    // Spawn one thread per partition (simple parallelism)
    std::vector<std::thread> threads;
    for (int i = 0; i < num_partitions_; i++) {
        threads.emplace_back([this, i]() {
            consume_partition(i);
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
}

void QueueConsumer::stop() {
    running_ = false;
}
```

**3. Consume Partition - Process Messages**
```cpp
void QueueConsumer::consume_partition(int partition) {
    std::cout << "Consumer thread for partition " << partition << " started\n";

    while (running_) {
        auto msg = read_next(partition);

        if (msg) {
            // Process message (Phase 9: just log it)
            std::cout << "[Partition " << msg->partition
                      << " | Offset " << msg->offset << "] "
                      << msg->data.substr(0, 100)  // First 100 chars
                      << (msg->data.size() > 100 ? "..." : "") << "\n";

            // Commit offset (mark as processed)
            commit_offset(partition, msg->offset);

        } else {
            // No new messages, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "Consumer thread for partition " << partition << " stopped\n";
}
```

**4. Read Next - Fetch Message from Disk**
```cpp
std::optional<Message> QueueConsumer::read_next(int partition) {
    uint64_t next_offset = read_offsets_[partition] + 1;

    // Build filename
    std::string filename = queue_path_ + "/partition-" + std::to_string(partition)
                         + "/" + format_offset(next_offset) + ".msg";

    // Check if file exists
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;  // No message yet (caught up to producer)
    }

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Update read offset
    read_offsets_[partition] = next_offset;

    return Message{partition, next_offset, content};
}
```

**5. Commit Offset - Save Progress**
```cpp
void QueueConsumer::commit_offset(int partition, uint64_t offset) {
    std::string offset_file = "consumer_offsets/" + consumer_group_
                            + "/partition-" + std::to_string(partition) + ".offset";

    std::ofstream file(offset_file);
    file << offset;
    file.flush();
}

void QueueConsumer::load_offsets() {
    for (int i = 0; i < num_partitions_; i++) {
        std::string offset_file = "consumer_offsets/" + consumer_group_
                                + "/partition-" + std::to_string(i) + ".offset";

        std::ifstream file(offset_file);
        if (file.is_open()) {
            file >> read_offsets_[i];
            std::cout << "Loaded offset for partition " << i
                      << ": " << read_offsets_[i] << "\n";
        }
    }
}
```

---

### Component 3: Integration with Ingestion Service

**Modify ingestion_service.cpp:**

```cpp
class IngestionService {
private:
    // ... existing members ...
    std::unique_ptr<PartitionedQueue> queue_;  // NEW: queue instead of file

public:
    IngestionService(int port, int num_partitions = 4)
        : /* existing init */,
          queue_(std::make_unique<PartitionedQueue>("queue", num_partitions)) {
        // ... existing initialization ...
    }

    void handle_metrics_request(int client_fd, const HTTPRequest& request) {
        // ... existing: parse JSON, rate limiting, validation ...

        // NEW: Write to queue instead of JSONL file
        std::string client_id = request.get_header("Authorization");
        std::string message = serialize_metrics(metrics);  // JSON string

        try {
            auto [partition, offset] = queue_->produce(client_id, message);

            // Optional: include partition/offset in response
            std::string response = format_json_response({
                {"status", "ok"},
                {"partition", partition},
                {"offset", offset}
            });

            send_response(client_fd, 200, response);

        } catch (const std::exception& e) {
            std::cerr << "Queue write failed: " << e.what() << "\n";
            send_response(client_fd, 500, "Internal error");
        }
    }
};
```

---

## Directory Structure

```
metricstream/
├── queue/                              ← NEW: Queue storage
│   ├── partition-0/
│   │   ├── 00000000000001.msg
│   │   ├── 00000000000002.msg
│   │   ├── 00000000000003.msg
│   │   └── offset.txt                  (next write: 4)
│   ├── partition-1/
│   │   ├── 00000000000001.msg
│   │   └── offset.txt
│   ├── partition-2/
│   │   └── ...
│   └── partition-3/
│       └── ...
├── consumer_offsets/                   ← NEW: Consumer progress tracking
│   └── storage-writer/                 (consumer group name)
│       ├── partition-0.offset          (last committed: 3)
│       ├── partition-1.offset
│       ├── partition-2.offset
│       └── partition-3.offset
├── src/
│   ├── partitioned_queue.h             ← NEW
│   ├── partitioned_queue.cpp           ← NEW
│   ├── queue_consumer.h                ← NEW
│   ├── queue_consumer.cpp              ← NEW
│   ├── ingestion_service.cpp           (modified)
│   └── consumer_main.cpp               ← NEW: Consumer binary
└── metrics.jsonl                       (keep for comparison)
```

---

## Message Format

**File naming:** `{20-digit-zero-padded-offset}.msg`
- Example: `00000000000001.msg`, `00000000000002.msg`

**File content:** JSON array of metrics (same as current JSONL format)
```json
{
  "timestamp": "2025-10-12T14:32:10.123Z",
  "client_id": "client_abc",
  "metrics": [
    {"name": "cpu_usage", "value": 75.5, "type": "gauge"},
    {"name": "memory_used", "value": 8192, "type": "gauge"}
  ]
}
```

**Why one file per message:**
- Clear message boundaries (no parsing ambiguity)
- Easy to implement (no complex file format)
- Atomic writes (file creation is atomic)
- Simple to debug (can inspect individual files)

**Trade-off:** More files = more inodes used (acceptable for learning phase)

---

## Error Handling & Edge Cases

### 1. Producer Crashes Mid-Write

**Scenario:** Ingestion service crashes while writing message file

**Solution:**
- File write is atomic at filesystem level (appears complete or not at all)
- Offset is only updated AFTER successful write + fsync
- Consumer skips incomplete files (no matching offset number)

### 2. Consumer Crashes Mid-Processing

**Scenario:** Consumer crashes after reading but before committing offset

**Solution:**
- On restart, consumer loads last committed offset
- Re-reads and re-processes last message (at-least-once delivery)
- Downstream must be idempotent (processing same message twice is ok)

### 3. Disk Full

**Scenario:** No space left for new messages

**Solution:**
- Producer catches exception on write failure
- Returns 503 Service Unavailable to client
- Client can retry later (with exponential backoff)

### 4. Partition Skew (Hot Clients)

**Scenario:** One client sends 10x more data than others

**Solution:**
- Accept it for Phase 9 (learning focus)
- Monitor partition sizes (add observability)
- Phase 11 (Kafka) will show how to handle this better

---

## Performance Characteristics

### Expected Throughput

**Producer (Ingestion Service):**
- Bottleneck: `fsync()` per message (~5ms on HDD, ~0.1ms on SSD)
- Estimated: ~200 writes/sec per partition (with fsync)
- With 4 partitions: ~800 writes/sec total

**Optimization opportunity (Phase 9B):** Batch writes
- Accumulate 10 messages, write in one `fsync()`
- Estimated: 2,000+ writes/sec

**Consumer:**
- Bottleneck: File open/read per message
- Estimated: ~1,000 reads/sec per partition
- Not a bottleneck (producer slower due to fsync)

### Comparison to Phase 8

| Metric | Phase 8 (JSONL) | Phase 9 (Queue) | Change |
|--------|-----------------|-----------------|--------|
| Write latency | ~1ms (append) | ~1-5ms (fsync) | Slower |
| Throughput | 145K RPS | ~800 RPS | **Much slower** |
| Decoupling | Tight coupling | Decoupled | ✅ Better |
| Parallel processing | Sequential | Parallel (4 partitions) | ✅ Better |

**Note:** Phase 9 trades raw throughput for architectural benefits (decoupling, ordering, resumability)

---

## Learning Outcomes

By the end of Phase 9, you will understand:

1. **Why partitioning matters:**
   - Enables parallel processing
   - Preserves ordering per key
   - Load balancing

2. **How offsets work:**
   - Sequential message tracking
   - Resumable processing
   - At-least-once delivery semantics

3. **Producer/Consumer pattern:**
   - Decoupling benefits
   - Async processing
   - Buffering under load

4. **Trade-offs:**
   - Durability (fsync) vs throughput
   - Simplicity (one file per message) vs efficiency
   - File-based vs in-memory queues

5. **What Kafka solves:**
   - Batching and compression (throughput)
   - Replication (durability without fsync)
   - Network distribution (scalability)
   - Zero-copy (efficiency)

---

## Testing Plan

### Unit Tests

**1. PartitionedQueue Tests:**
```cpp
TEST(PartitionedQueue, DeterministicPartitioning) {
    // Same key always goes to same partition
    PartitionedQueue queue("test_queue", 4);
    int p1 = queue.get_partition("client_abc");
    int p2 = queue.get_partition("client_abc");
    EXPECT_EQ(p1, p2);
}

TEST(PartitionedQueue, WriteAndVerify) {
    // Write message and verify file exists
    PartitionedQueue queue("test_queue", 4);
    auto [partition, offset] = queue.produce("key1", "test_message");

    std::string filename = "test_queue/partition-" + std::to_string(partition)
                         + "/" + format_offset(offset) + ".msg";
    EXPECT_TRUE(std::filesystem::exists(filename));
}
```

**2. QueueConsumer Tests:**
```cpp
TEST(QueueConsumer, ReadMessage) {
    // Write message via producer, read via consumer
    PartitionedQueue queue("test_queue", 4);
    queue.produce("key1", "test_message");

    QueueConsumer consumer("test_queue", "test_group", 4);
    auto msg = consumer.read_next(0);

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->data, "test_message");
}

TEST(QueueConsumer, OffsetCommit) {
    // Commit offset and verify persistence
    QueueConsumer consumer("test_queue", "test_group", 4);
    consumer.commit_offset(0, 42);

    // Create new consumer, should load offset 42
    QueueConsumer consumer2("test_queue", "test_group", 4);
    // Verify read_offsets_[0] == 42
}
```

### Integration Tests

**1. End-to-End Flow:**
```bash
# Start consumer in background
./metricstream_consumer &

# Send metrics via ingestion service
curl -X POST http://localhost:8080/metrics \
  -H "Authorization: client_123" \
  -d '{"metrics":[{"name":"cpu","value":75.5}]}'

# Verify consumer logs show the message
# Verify offset files updated
```

**2. Crash Recovery:**
```bash
# Send 10 messages
for i in {1..10}; do
  curl -X POST http://localhost:8080/metrics ...
done

# Kill consumer after 5 messages processed
kill -9 $CONSUMER_PID

# Restart consumer
./metricstream_consumer &

# Verify it resumes from offset 6 (or re-processes 5)
```

### Performance Tests

**1. Producer Throughput:**
```bash
# Measure writes/sec with fsync
./load_test 8080 100 100  # 100 clients, 100 requests each

# Expected: ~800 RPS (fsync bottleneck)
# Compare to Phase 8: 145K RPS
```

**2. Consumer Lag:**
```bash
# Producer: Send 1000 messages quickly
# Consumer: Monitor offset progress
# Measure: Time to catch up (lag)
```

---

## Implementation Phases

### Phase 9A: Producer (2-3 hours)

**Tasks:**
1. Create `PartitionedQueue` class
2. Implement `produce()`, `get_partition()`, offset tracking
3. Integrate with `IngestionService`
4. Unit tests for partitioning and writes
5. Manual testing: verify file creation

**Success Criteria:**
- [ ] Messages written to correct partition
- [ ] Offset tracking works
- [ ] Integration test: HTTP → queue → files

---

### Phase 9B: Consumer (2-3 hours)

**Tasks:**
1. Create `QueueConsumer` class
2. Implement `read_next()`, `consume_partition()`, offset commits
3. Build `consumer_main.cpp` binary
4. Unit tests for reading and offset tracking
5. Integration test: producer → consumer flow

**Success Criteria:**
- [ ] Consumer reads messages in order
- [ ] Offset commits persist across restarts
- [ ] Multiple partitions processed in parallel

---

### Phase 9C: Testing & Documentation (1-2 hours)

**Tasks:**
1. End-to-end integration tests
2. Performance benchmarking
3. Compare to Phase 8 JSONL approach
4. Document results in `results.md`
5. Create learning exercises for tutorial

**Success Criteria:**
- [ ] All tests passing
- [ ] Performance benchmarks documented
- [ ] Learning materials ready

---

## Migration Path to Kafka (Phase 11 Preview)

**What stays the same:**
- Partitioning strategy (hash-based on client_id)
- Offset concept (Kafka uses same model)
- Producer/consumer roles

**What Kafka adds:**
- Network distribution (multiple brokers)
- Replication (durability without fsync)
- Compression (LZ4, Snappy)
- Zero-copy transfers (sendfile)
- Consumer groups (automatic partition assignment)
- Exactly-once semantics (transactions)

**Code changes needed (Phase 11):**
```cpp
// Producer (using librdkafka)
rd_kafka_produce(topic, partition, message, len, key, key_len, nullptr);

// Consumer (using librdkafka)
rd_kafka_message_t* msg = rd_kafka_consumer_poll(consumer, timeout);
// Process msg->payload
rd_kafka_commit(consumer, nullptr, 0);  // Commit offset
```

---

## Next Steps

Ready to implement Phase 9A (Producer)?

**Suggested workflow:**
1. Create git worktree: `phase-9-message-queue`
2. Implement `PartitionedQueue` class with TDD
3. Integrate with ingestion service
4. Test with existing load test tool
5. Document results
