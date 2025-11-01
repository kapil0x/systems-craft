# Phases to Crafts Mapping

**Last Updated:** 2025-10-26

This document provides the authoritative mapping between **Phases** (optimization steps) and **Crafts** (complete system components) in the Systems Craft tutorial series.

## Key Concepts

- **Craft** = A complete system component (e.g., Ingestion Service, Message Queue)
- **Phase** = An optimization or implementation step within a craft
- **Each craft starts with Phase 1** for clarity and consistency

## Master Mapping Table

| Craft | Component | Total Phases | Time Investment | Status |
|-------|-----------|--------------|-----------------|--------|
| **Craft #0** | Complete PoC | N/A (standalone) | 2-3 hours | ✅ Complete |
| **Craft #1** | Metrics Ingestion | 8 phases | 8-12 hours | ✅ Phase 1-7 Complete<br/>🚧 Phase 8 Partial |
| **Craft #2** | Message Queue | 3 phases | 8-12 hours | 📝 Designed |
| **Craft #3** | Storage Engine | 4 phases (est.) | 10-15 hours | 📝 Planned |
| **Craft #4** | Query Engine | 4 phases (est.) | 8-12 hours | 📝 Planned |
| **Craft #5** | Alerting System | 4 phases (est.) | 6-10 hours | 📝 Planned |

---

## Craft #0: Complete Monitoring Platform PoC

**Location:** `phase0/`

**What it is:** All 5 components in 600 lines - a working end-to-end system

**Purpose:**
- See the complete architecture before optimizing
- Understand how distributed systems components connect
- Validate the system design with a simple implementation

**Components included:**
1. Ingestion API (single-threaded HTTP)
2. In-memory queue (thread-safe buffer)
3. Storage consumer (background file writer)
4. Query API (full file scan)
5. Alerting engine (polling-based)

**Performance:** ~50-100 RPS (intentionally simple)

**Time:** 2-3 hours

**Tutorial:** [phase0/README.md](../phase0/README.md)

---

## Craft #1: Metrics Ingestion Service

**Location:** `src/` (implementation) + `craft1/` (documentation)

**What it is:** High-performance HTTP ingestion API optimized through 8 progressive phases

**Learning approach:** Measure → Identify bottleneck → Optimize → Measure again

### Phase 1: Threading per Request
- **Bottleneck:** Blocking accept() on single thread
- **Solution:** Spawn thread per connection
- **Result:** 88% success @ 20 clients
- **Learning:** Basic concurrency, thread creation overhead

### Phase 2: Async I/O (Producer-Consumer)
- **Bottleneck:** Blocking file writes in request thread
- **Solution:** Background writer thread with queue
- **Result:** 66% success @ 50 clients
- **Learning:** Producer-consumer pattern, I/O decoupling

### Phase 3: JSON Parsing Optimization
- **Bottleneck:** O(n²) string operations in JSON parser
- **Solution:** Single-pass parser with O(n) complexity
- **Result:** 80% success @ 100 clients, 2.73ms latency
- **Learning:** Algorithm optimization, profiling hot paths

### Phase 4: Per-Client Mutex Pools
- **Bottleneck:** Global mutex contention in rate limiting
- **Solution:** Hash-based per-client mutex sharding
- **Result:** Incremental improvement
- **Learning:** Lock granularity, contention reduction

### Phase 5: Thread Pool Architecture
- **Bottleneck:** Unlimited thread creation under load
- **Solution:** Fixed-size thread pool with work queue
- **Result:** 100% success @ 100 clients, 0.65ms latency
- **Learning:** Resource pooling, bounded concurrency

### Phase 6: Lock-Free Ring Buffers
- **Bottleneck:** Mutex overhead in metrics collection
- **Solution:** Lock-free concurrent ring buffer
- **Result:** Eliminated collection overhead
- **Learning:** Lock-free data structures, memory ordering

### Phase 7: HTTP Keep-Alive ✅
- **Bottleneck:** TCP handshake overhead per request
- **Solution:** Persistent connections with keep-alive
- **Result:** **2,253 RPS sustained, 100% success, 0.25ms p50**
- **Learning:** Connection pooling, HTTP protocol optimization
- **Documentation:** [docs/phase7_keep_alive_results.md](phase7_keep_alive_results.md)

### Phase 8: Event-Driven I/O (epoll/kqueue) 🚧
- **Bottleneck:** Thread-per-connection scalability limit
- **Solution:** Event loop with epoll/kqueue for 10,000+ connections
- **Result:** In progress (partial implementation in `src/event_loop.cpp`)
- **Learning:** Event-driven programming, reactor pattern
- **Target:** 20,000+ RPS, 10,000+ concurrent connections
- **Documentation:** [phase8_design.md](../phase8_design.md)

**Final Performance (Phase 7):**
- **Throughput:** 2,253 RPS sustained
- **Reliability:** 100% success rate
- **Latency:** 0.25ms p50, 0.65ms p99
- **Concurrency:** 100+ concurrent clients

**Time Investment:** 8-12 hours total across all phases

---

## Craft #2: Distributed Message Queue

**Location:** `craft2/` (to be created from `docs/phases/phase-9` and `phase-10`)

**What it is:** Kafka-like message queue with partitioning, consumer groups, and coordination

**Learning approach:** Build file-based version first, then understand why distributed consensus matters

### Phase 1: File-Based Partitioned Queue 📝
- **Goal:** Understand partitioning, offsets, producer/consumer patterns
- **Implementation:**
  - Hash-based partitioning by client_id
  - Producer interface (write to partition files)
  - Consumer interface (read with offset tracking)
  - Offset durability with fsync
- **Expected:** ~800 RPS (fsync bottleneck)
- **Learning:** Why Kafka batches writes, partitioning strategies
- **Design doc:** Currently at `docs/phases/phase-9/message-queue/design.md`
- **New location:** `craft2/phase-1-partitioned-queue/design.md`

### Phase 2: Consumer Coordination 📝
- **Goal:** Understand consumer groups, rebalancing, failure detection
- **Implementation:**
  - Consumer groups with file-based locks (flock)
  - Dynamic partition assignment (round-robin, range strategies)
  - Heartbeat-based failure detection
  - Rebalancing protocol with generation numbers
- **Expected:** <100ms rebalance latency
- **Learning:** Coordination primitives, split-brain prevention
- **Design doc:** Currently at `docs/phases/phase-10/consumer-coordination/design.md`
- **New location:** `craft2/phase-2-consumer-coordination/design.md`

### Phase 3: Distributed Coordination 📝
- **Goal:** Scale beyond single machine with ZooKeeper/Raft
- **Implementation:**
  - ZooKeeper integration for coordination
  - Ephemeral nodes for automatic cleanup
  - Watches for event-driven rebalancing
  - Multi-machine deployment
- **Expected:** Network-wide coordination, automatic failover
- **Learning:** Distributed consensus, CAP theorem trade-offs
- **New location:** `craft2/phase-3-distributed-coordination/design.md`

**Target Performance:**
- **Throughput:** 1M+ messages/sec
- **Durability:** Replication across multiple brokers
- **Scalability:** Horizontal scaling with partition distribution

**Time Investment:** 8-12 hours total

---

## Craft #3: Time-Series Storage Engine

**Location:** `craft3/` (to be created)

**What it is:** InfluxDB-like time-series database with compression and indexing

### Phase 1: Basic LSM Tree Storage 📝
- **Goal:** Understand write-optimized storage
- **Implementation:**
  - Memtable for in-memory writes
  - Write-ahead log (WAL) for durability
  - SSTable format for disk persistence
  - Compaction strategy (leveled or size-tiered)
- **Learning:** LSM trees, write amplification, read amplification

### Phase 2: Gorilla Compression 📝
- **Goal:** Achieve 10:1 compression for time-series data
- **Implementation:**
  - Delta-of-delta timestamp encoding
  - XOR-based value compression
  - Block-based storage format
- **Learning:** Time-series characteristics, compression algorithms

### Phase 3: Tag-Based Indexing 📝
- **Goal:** Fast lookups by metric name and tags
- **Implementation:**
  - Inverted index for tag queries
  - Bloom filters for negative lookups
  - Tag cardinality optimization
- **Learning:** Index structures, query optimization

### Phase 4: Time-Range Query Optimization 📝
- **Goal:** Sub-second queries for 24-hour ranges
- **Implementation:**
  - Block-level time indexes
  - Parallel query execution
  - Query result caching (LRU)
- **Learning:** Query planning, parallelization strategies

**Target Performance:**
- **Writes:** 1M+ writes/sec
- **Compression:** 10:1 ratio
- **Queries:** <1 second for 24-hour range

**Time Investment:** 10-15 hours total

---

## Craft #4: Query & Aggregation Engine

**Location:** `craft4/` (to be created)

**What it is:** PromQL-like query processor with aggregations and optimizations

### Phase 1: Query Parser & AST 📝
- **Goal:** Parse query language into executable form
- **Implementation:**
  - Lexer and tokenizer
  - Recursive descent parser
  - Abstract syntax tree (AST) representation
- **Learning:** Language design, parsing techniques

### Phase 2: Execution Planning 📝
- **Goal:** Optimize query execution
- **Implementation:**
  - Query optimizer (predicate pushdown, filter reordering)
  - Physical execution plans
  - Cost-based optimization
- **Learning:** Query optimization, database internals

### Phase 3: Parallel Aggregation 📝
- **Goal:** Fast aggregations over large datasets
- **Implementation:**
  - Map-reduce style aggregations
  - Thread pool for parallel processing
  - Memory-efficient streaming aggregates
- **Learning:** Parallel algorithms, memory management

### Phase 4: Result Caching & Optimization 📝
- **Goal:** Improve repeated query performance
- **Implementation:**
  - Query result cache (LRU eviction)
  - Partial result reuse
  - Materialized views for common queries
- **Learning:** Caching strategies, invalidation policies

**Target Performance:**
- **Query speed:** 100M+ data points in <1 second
- **Aggregations:** avg, sum, min, max, percentiles
- **Concurrency:** Multiple queries in parallel

**Time Investment:** 8-12 hours total

---

## Craft #5: Alerting & Notification System

**Location:** `craft5/` (to be created)

**What it is:** PagerDuty-like alerting platform with rules and notifications

### Phase 1: Event-Driven Rule Evaluation 📝
- **Goal:** Real-time alert detection
- **Implementation:**
  - Rule DSL and parser
  - Streaming evaluation engine
  - Threshold and anomaly detection
- **Learning:** Event processing, rule engines

### Phase 2: Alert State Machine 📝
- **Goal:** Manage alert lifecycle
- **Implementation:**
  - State transitions (pending → firing → resolved)
  - Deduplication and grouping
  - Silencing and inhibition rules
- **Learning:** State machines, alert management

### Phase 3: Multi-Channel Notifications 📝
- **Goal:** Deliver alerts reliably
- **Implementation:**
  - Email, Slack, PagerDuty integrations
  - Retry logic and delivery guarantees
  - Rate limiting per channel
- **Learning:** Integration patterns, reliability

### Phase 4: Escalation Policies 📝
- **Goal:** Route alerts to right people
- **Implementation:**
  - Multi-tier escalation paths
  - On-call scheduling integration
  - Alert routing rules
- **Learning:** Operational workflows, scheduling

**Target Performance:**
- **Rule evaluation:** 10K+ rules/sec
- **Detection latency:** <1 minute
- **Delivery:** 99.99% success rate

**Time Investment:** 6-10 hours total

---

## Learning Path Recommendations

### For Beginners
1. **Start:** Craft #0 (Complete PoC) - See the big picture
2. **Then:** Craft #1, Phase 1-3 - Learn basics of optimization
3. **Skip ahead:** Craft #2, Phase 1 - Understand message queues
4. **Come back:** Craft #1, Phase 4-8 - Deep optimization techniques

### For Intermediate Engineers
1. **Start:** Craft #0 (review architecture)
2. **Focus:** Craft #1, Phase 5-8 - Advanced concurrency patterns
3. **Build:** Craft #2, Phase 1-2 - Distributed coordination
4. **Explore:** Craft #3, Phase 1-2 - Storage engine internals

### For Advanced Engineers
1. **Skim:** Craft #0 and Craft #1 (review implementations)
2. **Deep dive:** Craft #2, Phase 3 - Distributed consensus
3. **Design:** Craft #3, Phase 3-4 - Query optimization
4. **Architecture:** Design Craft #6 (Distributed Tracing) or Craft #7 (Multi-Region Replication)

---

## Terminology Guidelines

### When Writing Documentation

✅ **Correct:**
- "In Craft #1, Phase 3, we optimized JSON parsing..."
- "Craft #2 (Message Queue) consists of 3 phases..."
- "Phase 7 of Craft #1 achieved 2,253 RPS..."

❌ **Avoid:**
- "In Phase 9, we built a message queue..." (ambiguous - which craft?)
- "After Phase 15..." (unclear which craft this belongs to)

### When Referencing Code

Use craft-specific paths:
- `craft1/phase-7-keep-alive/results.md`
- `craft2/phase-1-partitioned-queue/design.md`
- `craft3/phase-2-compression/implementation.cpp`

### In Git Branches

Use descriptive names:
- `craft1/phase8-event-loop`
- `craft2/phase1-queue-implementation`
- `craft2/phase2-consumer-groups`

---

## Migration Notes

### Old Naming → New Naming

| Old Reference | New Reference | Status |
|--------------|---------------|---------|
| Phase 0 | Craft #0 | ✅ Already clear |
| Phase 1-8 | Craft #1, Phase 1-8 | ✅ Clarified |
| Phase 9 | Craft #2, Phase 1 | 🔄 Needs update |
| Phase 10 | Craft #2, Phase 2 | 🔄 Needs update |
| Phase 11 (future) | Craft #2, Phase 3 | 📝 Planned |

### Files to Update

- [x] Create this mapping document
- [ ] Update `README.md` with craft/phase terminology
- [ ] Update `CLAUDE.md` with craft/phase structure
- [ ] Move `docs/phases/phase-9/` → `craft2/phase-1-partitioned-queue/`
- [ ] Move `docs/phases/phase-10/` → `craft2/phase-2-consumer-coordination/`
- [ ] Create `craft1/README.md` (consolidate phases 1-8)
- [ ] Create `craft2/README.md` (overview of message queue)
- [ ] Create placeholder READMEs for craft3, craft4, craft5

---

## Contributing New Phases

When adding a new phase to an existing craft:

1. **Create phase directory:** `craftN/phase-X-descriptive-name/`
2. **Add design doc:** `design.md` (before implementation)
3. **Implement:** Put code in appropriate `src/` subdirectory
4. **Document results:** `results.md` (after implementation)
5. **Update craft README:** Add phase to craft overview
6. **Update this mapping:** Add phase to master table

When adding a new craft:

1. **Create craft directory:** `craftN/`
2. **Add craft README:** Overview of all phases
3. **Create phase-1 subdirectory:** Start with Phase 1
4. **Update main README:** Add craft to learning path
5. **Update this mapping:** Add craft to master table

---

## Questions?

- **"Why restart numbering for each craft?"** → Clarity and consistency. Each craft is a fresh component.
- **"Can I skip phases?"** → Yes, but understand dependencies. Some phases build on previous work.
- **"Can phases be done in parallel?"** → Generally no within a craft (sequential optimization), but yes across crafts (independent components).
- **"How long does the complete series take?"** → ~50-70 hours for all 5 crafts if done thoroughly.

---

**Next:** See [README.md](../README.md) for the complete learning path and [CLAUDE.md](../CLAUDE.md) for AI agent guidance.
