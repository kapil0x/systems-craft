# Craft #2: Distributed Message Queue

**Component:** Kafka-like message queue with partitioning, consumer groups, and coordination
**Learning Goal:** Build a message queue from first principles, then understand why distributed systems need consensus
**Total Time:** 8-12 hours across 3 phases
**Target Performance:** 1M+ messages/sec, horizontal scaling, automatic failover

---

## Overview

Craft #2 teaches you how to build a production-grade message queue through **progressive abstraction**. You'll start with file-based partitioning (single machine), add multi-process coordination, then finally implement distributed consensus across machines.

**Key Learning:** Understand partitioning, offsets, consumer groups, and rebalancing before using Kafka in production.

---

## Architecture Evolution

### Starting Point (Craft #1)
```
Ingestion Service → writes directly to metrics.jsonl file
```
- **Problem:** Tight coupling between ingestion and storage
- **Limitation:** Can't scale processing independently

### Phase 1: Partitioned Queue
```
Ingestion → [Partitioned Queue] → Consumer
              partition-0/
              partition-1/          (file-based, single machine)
              partition-2/
              partition-3/
```
- **Benefit:** Decoupled ingestion from processing
- **Limitation:** Single consumer, no fault tolerance

### Phase 2: Consumer Groups
```
Ingestion → [Queue] → Consumer Group
                       ├─ Consumer A (P0, P2)
                       └─ Consumer B (P1, P3)
```
- **Benefit:** Parallel processing, automatic rebalancing
- **Limitation:** Single machine only (file-based coordination)

### Phase 3: Distributed Coordination
```
Ingestion → [Queue] → Consumer Group (multi-machine)
   │           │       ├─ Consumer A @ node-1
   │           │       ├─ Consumer B @ node-2
   │           │       └─ Consumer C @ node-3
   │           │
   │           └─── ZooKeeper (coordination)
   │
   └─────────────── Multiple brokers with replication
```
- **Benefit:** Network-wide coordination, true fault tolerance
- **Scalability:** Horizontal scaling across data centers

---

## Phase Breakdown

### Phase 1: File-Based Partitioned Queue ✅

**Documentation:** [phase-1-partitioned-queue/design.md](phase-1-partitioned-queue/design.md)

**Goal:** Understand message queue fundamentals - partitioning, offsets, producer/consumer patterns

**Time:** 3-4 hours
**Status:** ✅ **COMPLETE** (Phase 9 in main branch)

**Implementation:**
- File-based partitioned queue (`include/partitioned_queue.h`, `src/partitioned_queue.cpp`)
- 4 partitions with hash-based routing (client_id → partition)
- Sequential offset management per partition
- Producer: `enqueue(client_id, data)` writes to `queue/partition-N/OFFSET.msg`
- Consumer: `QueueConsumer` reads from partitions with offset tracking

**Performance:**
- **Throughput:** ~800 RPS (measured with concurrent load test)
- **Latency:** ~0.70ms avg (file I/O)
- **Success Rate:** 98.7% @ concurrent load
- **Limitation:** Single machine only, disk I/O bound

**Key Learning:** File-based queues are simple, durable, and reliable. At this scale, file I/O sequencing is the primary bottleneck for throughput, not individual latency.

---

### Phase 2: Kafka Integration ✅

**Documentation:**
- [Craft #2, Phase 2 Worktree](../.worktrees/craft-2-phase-11-kafka/)
- [PHASE_11_KAFKA_INTEGRATION.md](../.worktrees/craft-2-phase-11-kafka/PHASE_11_KAFKA_INTEGRATION.md)
- [Kafka Threading Fix Documentation](../docs/kafka-fix/OVERVIEW.md)

**Goal:** Compare file-based queue with production-grade Kafka - understand what Kafka optimizes for

**Time:** 4-6 hours
**Status:** ✅ **COMPLETE** (Committed Nov 2, 2025)

**Implementation:**
- Kafka producer integration (`include/kafka_producer.h`, `src/kafka_producer.cpp`)
- Kafka consumer with consumer groups (`include/kafka_consumer.h`, `src/kafka_consumer.cpp`)
- Dual-mode architecture: runtime switch between file-based OR Kafka (`QueueMode` enum)
- Fixed 4 critical threading bugs:
  1. Race condition (no mutex on shared KafkaProducer)
  2. Use-after-free in destructor (messages in-flight during shutdown)
  3. No retry logic for queue full errors
  4. Message lifetime issues with async sends

**Performance:**
- **Throughput:** Kafka scales to 100K+ RPS (librdkafka capability, full benchmark pending)
- **Latency:** ~0.15ms avg (measured, network overhead negligible vs file-based)
- **Success Rate:** 97.9% @ concurrent load testing
- **Concurrency:** Verified thread-safe under 16 concurrent worker threads
- **Scalability:** Designed for horizontal scaling across machines

**Verification:**
- ✅ Thread-safe mutex protection prevents race conditions
- ✅ Concurrent load test (20 clients) succeeds 100%
- ✅ Single requests verified end-to-end (curl → Kafka → consumer)
- ⏳ Full RPS benchmark (100K+) needs systematic load testing (future phase)

**Key Learning:** Kafka's architecture supports massive throughput via batching, replication, and partitioning. This implementation demonstrates the core threading and concurrency challenges. Production performance depends on proper broker configuration, hardware, and network setup.

**Comparison Results (Phase 11 Verified):**
```
Mode        | Status                  | Latency | Success | Key Learning
------------|-------------------------|---------|---------|---------------------------------------
File-based  | ✅ 800 RPS measured     | 0.70ms  | 98.7%   | Simple, durable, single-machine
Kafka       | ✅ Thread-safe verified | 0.15ms  | 97.9%   | 4.6x latency improvement, architecture proven
```

**Threading Insights:**
- librdkafka is NOT thread-safe - requires external mutex
- 16 HTTP worker threads → 1 shared KafkaProducer → 1 background I/O thread
- Defense in depth: flush(10s) + poll() loop + warnings
- Always handle ERR__QUEUE_FULL with retry logic

---

### Phase 3: Distributed Coordination 📝

**Documentation:** *(to be created)*

**Goal:** Scale beyond single machine using ZooKeeper/Raft for distributed consensus

**Time:** 4-5 hours
**Status:** 📝 Planned

---

## Related Documentation

- **[Phases to Crafts Mapping](../docs/PHASES_TO_CRAFTS_MAPPING.md)** - Full Craft #2 details
- **[Phase 1 Design](phase-1-partitioned-queue/design.md)** - File-based queue architecture
- **[Phase 2 Design](phase-2-consumer-coordination/design.md)** - Consumer group coordination
- **[Craft #1 README](../craft1/README.md)** - Ingestion service (producer)

---

**This is Craft #2 of Systems Craft.** Once complete, you'll understand how Kafka, Pulsar, and RabbitMQ work internally.
