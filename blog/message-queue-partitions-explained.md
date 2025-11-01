# Message Queue Partitions: From Directories to Distributed Systems

**Understanding how message queues work by building one from scratch**

---

## The Question That Started It All

> "So each of the partitions are directory inside queue?"

This seemingly simple question leads us down a fascinating path - from file system basics to distributed systems coordination. Let's explore how message queues actually work under the hood.

---

## Yes, Partitions Are Just Directories!

When building a message queue from scratch, the simplest approach is to use the file system itself:

```
queue/
├── partition-0/                    ← Directory for partition 0
│   ├── 00000000000001.msg         ← Message at offset 1
│   ├── 00000000000002.msg         ← Message at offset 2
│   ├── 00000000000003.msg         ← Message at offset 3
│   └── offset.txt                  ← Tracks next write: 4
│
├── partition-1/                    ← Directory for partition 1
│   ├── 00000000000001.msg
│   ├── 00000000000002.msg
│   └── offset.txt
│
├── partition-2/                    ← Directory for partition 2
│   └── ...
│
└── partition-3/                    ← Directory for partition 3
    └── ...
```

**Why directories work perfectly:**
- **Isolation:** Each partition is independent (separate directory = separate lock)
- **Parallelism:** Multiple producers/consumers can work on different partitions simultaneously
- **Ordering:** Messages from same client always go to same partition (deterministic hashing)
- **Simple debugging:** You can `ls queue/partition-0/` and see exactly what messages are there

This isn't just a learning exercise - it mirrors how Kafka organizes topics internally, just using the filesystem instead of custom storage!

---

## How Partitioning Works

### Producer Side: Hash-Based Routing

```cpp
// Client "client_abc" sends a message
int partition = hash("client_abc") % 4;  // Let's say = 2

// Write to queue/partition-2/00000000000005.msg
queue_->produce("client_abc", message);
```

**Key principle:** Same client always maps to same partition (deterministic hashing)

### Consumer Side: Parallel Processing

```cpp
// Consumer spawns 4 threads, one per partition
// Thread 0 reads from queue/partition-0/
// Thread 1 reads from queue/partition-1/
// Thread 2 reads from queue/partition-2/
// Thread 3 reads from queue/partition-3/
```

Each thread reads messages independently, enabling true parallelism.

---

## The Offset Tracking Challenge

### What Are Offsets?

Offsets are sequential position numbers for each message in a partition. They enable:
- **Tracking:** Know which messages have been processed
- **Resumability:** If consumer crashes, resume from last committed offset
- **Idempotency:** Don't process same message twice

### Where Are Offsets Stored?

This is where it gets interesting - offsets are **shared across consumers in the same consumer group**:

```
consumer_offsets/
└── storage-writer/              ← Consumer group name
    ├── partition-0.offset       ← SHARED by all consumers in group
    ├── partition-1.offset       ← SHARED by all consumers in group
    ├── partition-2.offset
    └── partition-3.offset
```

**Example flow:**
```
Consumer 1 reads partition-0, offset 5
Consumer 1 commits: storage-writer/partition-0.offset = 5

If Consumer 1 crashes:
Consumer 2 can take over partition-0
Consumer 2 reads: storage-writer/partition-0.offset = 5
Consumer 2 resumes from offset 6 ✅
```

---

## Consumer Groups: The Scale-Out Pattern

### Single Consumer (Simple)

```
One consumer process reads ALL 4 partitions
├── Thread 0 → partition-0
├── Thread 1 → partition-1
├── Thread 2 → partition-2
└── Thread 3 → partition-3

No locks needed between consumer threads - static partition assignment
```

**Wait, multiple threads but no locks?** Yes! Here's why this works:

```cpp
class QueueConsumer {
private:
    std::vector<uint64_t> read_offsets_;  // [5, 8, 3, 12]

public:
    void consume_partition(int partition) {
        // Each thread ONLY touches read_offsets_[partition]
        uint64_t next = read_offsets_[partition] + 1;  // Thread 0 uses [0], Thread 1 uses [1]
        // ... read message ...
        read_offsets_[partition] = next;  // Different indices = different memory addresses
    }
};
```

**Memory layout - why it's thread-safe:**
```
read_offsets_ array in memory:
┌──────────────────────────────────────────────┐
│ [0]: 5    [1]: 8    [2]: 3    [3]: 12       │
│  ↑         ↑         ↑         ↑             │
│ Thread 0  Thread 1  Thread 2  Thread 3      │
│  ONLY      ONLY      ONLY      ONLY         │
└──────────────────────────────────────────────┘

Each thread owns exclusive array index = no race condition!
```

**Key principle:** Static partition assignment at startup means:
- Thread 0 always reads partition-0 (never changes)
- Thread 1 always reads partition-1 (never changes)
- No overlap in memory access = no coordination needed
- Each thread writes to different offset file on disk

### Multiple Consumers (Distributed)

```
Consumer Group "storage-writer" with 2 consumer processes:

Consumer Process 1:
├── Thread → partition-0
└── Thread → partition-1

Consumer Process 2:
├── Thread → partition-2
└── Thread → partition-3

NEEDS: Partition assignment coordination!
```

### Partition Assignment Rules

The number of consumers vs partitions determines assignment:

```
4 partitions, 1 consumer:   Consumer reads [0,1,2,3]
4 partitions, 2 consumers:  C1=[0,1], C2=[2,3]
4 partitions, 4 consumers:  C1=[0], C2=[1], C3=[2], C4=[3]
4 partitions, 5 consumers:  C5 is IDLE (more consumers than partitions)
```

### The Golden Rule

**One partition can only be read by ONE consumer at a time.**

```
❌ NEVER: Multiple consumers reading same partition
Partition-0 ← Consumer A (offset 5)
           ← Consumer B (offset 8)  ❌ RACE CONDITION!

✅ ALWAYS: One partition = one consumer (exclusive)
Partition-0 ← Consumer A only
Partition-1 ← Consumer B only
```

This prevents duplicate processing and race conditions.

---

## Do We Need Locking?

**Short answer: YES, for distributed consumers.**

### Phase 1: Single Consumer (No Locks Needed)

When you have only one consumer process, no coordination is needed - it owns all partitions exclusively.

### Phase 2: Multiple Consumers (Locks Required)

When scaling to multiple consumer processes, you need **partition assignment locks**:

```cpp
class DistributedQueueConsumer {
private:
    std::set<int> assigned_partitions_;
    std::string lock_dir_ = "consumer_locks/" + consumer_group_;

public:
    // Try to claim partitions
    void claim_partitions() {
        for (int i = 0; i < num_partitions_; i++) {
            std::string lock_file = lock_dir_ + "/partition-"
                                   + std::to_string(i) + ".lock";

            // Try to create exclusive lock file
            int fd = open(lock_file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
            if (fd >= 0) {
                // Write our consumer ID
                write(fd, consumer_id_.c_str(), consumer_id_.size());
                close(fd);

                assigned_partitions_.insert(i);  // ✅ We own this partition
            }
            // else: another consumer owns it, skip
        }
    }

    // Release partitions on shutdown
    void release_partitions() {
        for (int partition : assigned_partitions_) {
            std::string lock_file = lock_dir_ + "/partition-"
                                   + std::to_string(partition) + ".lock";
            std::filesystem::remove(lock_file);
        }
    }
};
```

### Directory Structure with Locking

```
consumer_locks/
└── storage-writer/              ← Consumer group
    ├── partition-0.lock         ← Contains "consumer-A-pid-12345"
    ├── partition-1.lock         ← Contains "consumer-A-pid-12345"
    ├── partition-2.lock         ← Contains "consumer-B-pid-67890"
    └── partition-3.lock         ← Contains "consumer-B-pid-67890"

consumer_offsets/
└── storage-writer/              ← Shared offsets (no locks needed for reads)
    ├── partition-0.offset
    ├── partition-1.offset
    ├── partition-2.offset
    └── partition-3.offset
```

**Why lock files work:**
- `O_CREAT | O_EXCL` ensures atomic lock acquisition
- Lock file contains consumer ID for debugging
- Consumer releases locks on graceful shutdown
- Stale locks can be detected (check if PID is alive)

---

## Key Design Insights

### 1. File-Based Partitioning Uses Directories as Isolation Boundaries

Just like how Unix processes use separate file descriptors, each partition directory holds:
- Message files (numbered by offset)
- Offset tracking file (next write position)
- Independent mutex for parallel writes

### 2. Consumer Groups Solve the "Who Reads What" Problem

The pattern is:
- **One partition = one consumer (exclusive)**
- **One consumer = multiple partitions (allowed)**

This prevents duplicate processing while enabling parallelism - the same pattern Kafka uses for scale-out architecture.

### 3. Incremental Complexity

The progression teaches incrementally:
- **Phase 1:** Single consumer (learn basics - partitioning, offsets, decoupling)
- **Phase 2:** Multiple consumers (add distributed locking)
- **Phase 3:** Kafka (production-ready with replication, compression, zero-copy)

This mirrors how real systems evolve - start simple, add complexity only when needed.

---

## Comparison Table

| Aspect | Single Consumer | Multiple Consumers (Distributed) |
|--------|-----------------|----------------------------------|
| **Consumers per group** | 1 process | Multiple processes |
| **Partitions per consumer** | All (0-3) | Subset (assigned dynamically) |
| **Partition exclusivity** | N/A (one consumer) | File-based locks |
| **Offset storage** | Local (could be in-memory) | Shared directory (must persist) |
| **Coordination needed** | No | Yes (partition assignment) |
| **Failure handling** | Restart reads all partitions | Other consumers take over partitions |

---

## What Kafka Adds

When you move from file-based queues to Kafka, you get:

### Performance Optimizations
- **Batching and compression:** LZ4, Snappy for throughput
- **Zero-copy transfers:** `sendfile()` system call avoids user-space copying
- **Page cache optimization:** Sequential disk writes rival memory performance

### Reliability Features
- **Replication:** Data replicated across brokers (durability without fsync)
- **Leader election:** Automatic failover when broker fails
- **Exactly-once semantics:** Transactions prevent duplicate processing

### Operational Features
- **Consumer groups:** Automatic partition rebalancing
- **Monitoring:** Built-in metrics and JMX integration
- **Multi-datacenter replication:** MirrorMaker for disaster recovery

But the **core concepts remain the same**:
- Hash-based partitioning
- Offset-based tracking
- Producer/consumer decoupling
- Partition exclusivity per consumer

---

## Practical Takeaways

**If you're building a message queue:**

1. **Start with directories** - File system provides isolation, atomicity, and persistence for free
2. **Use deterministic hashing** - Same key always maps to same partition for ordering guarantees
3. **One partition, one consumer** - Never let multiple consumers read the same partition simultaneously
4. **Share offsets, not partitions** - Consumer group members coordinate via shared offset storage
5. **Add locks only when scaling** - Single consumer needs no coordination overhead

**If you're using Kafka:**

Understanding these fundamentals helps you:
- Debug partition assignment issues
- Optimize consumer group sizing (consumers ≤ partitions)
- Reason about ordering guarantees (per-partition, not global)
- Troubleshoot offset management problems
- Design partition keys for even load distribution

---

## Conclusion

Message queue partitions aren't magic - they're just directories with clever coordination patterns. By building from first principles, you understand:

- Why partitioning enables parallelism
- How offsets enable resumable processing
- Why one partition can only have one consumer
- When you need distributed locking
- What Kafka actually optimizes

The beauty of systems engineering is that complex distributed systems are built from simple primitives. Directories, files, and locks - combined with the right design patterns - create robust, scalable message queues.

Next time you configure Kafka partitions or debug consumer lag, you'll know exactly what's happening under the hood.

---

## Further Reading

- **Kafka Documentation:** [Partitions and Consumer Groups](https://kafka.apache.org/documentation/#intro_concepts_and_terms)
- **Log-Structured Merge Trees:** Understanding sequential write performance
- **Consensus Algorithms:** How Kafka uses ZooKeeper/KRaft for coordination
- **Systems Design:** Building reliable distributed systems from first principles

---

*This blog post is based on a hands-on systems engineering tutorial - building a metrics ingestion system from scratch to understand production infrastructure. The progression from file-based queues to Kafka mirrors how real systems evolve in practice.*
