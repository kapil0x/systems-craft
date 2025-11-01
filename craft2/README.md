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

### Phase 1: File-Based Partitioned Queue 📝

**Documentation:** [phase-1-partitioned-queue/design.md](phase-1-partitioned-queue/design.md)

**Goal:** Understand message queue fundamentals - partitioning, offsets, producer/consumer patterns

**Time:** 3-4 hours
**Status:** 📝 Design complete, implementation pending

---

### Phase 2: Consumer Coordination 📝

**Documentation:** [phase-2-consumer-coordination/design.md](phase-2-consumer-coordination/design.md)

**Goal:** Understand consumer groups, rebalancing, and failure detection

**Time:** 3-4 hours
**Status:** 📝 Design complete, implementation pending

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
