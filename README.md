# Systems Craft: Build a Complete Monitoring Platform

**Engineer production systems, not toy examples.** Systems Craft teaches you distributed systems architecture through building a real-world monitoring platform (like Datadog, Prometheus, or New Relic). You'll master core engineering concepts—concurrency, async I/O, message queuing, time-series storage, query optimization, and alerting—by shipping working code against real constraints.

Why a complete monitoring platform? Because it forces you to learn everything: **ingestion performance (Craft #1), message queuing (Craft #2), storage engines (Craft #3), query optimization (Craft #4), and alerting systems (Craft #5)**. Each component teaches different distributed systems principles. Together, they teach you system design.

> **The Future of Engineering:** Learn to design distributed systems AND coordinate AI agents to build them—practicing the exact skills you'll need when every engineer manages teams of AI.

## 🚀 Fastest Way to Start (30 Seconds with Docker)

**No C++ compiler needed. No dependencies. Just run:**

```bash
# Start the complete monitoring platform
docker-compose up

# Server running on http://localhost:8080
# Open website/benchmark.html to test performance
```

That's it! You now have a working monitoring system. See [DOCKER_QUICKSTART.md](DOCKER_QUICKSTART.md) for details.

**Next:** Open `website/architecture.html` to understand what you just ran, then start optimizing with Craft #1.

---

## 🎯 Learning Path

### **Start Here: Craft #0 (2-3 hours)** ✅
Build a complete working monitoring platform in an afternoon:
- All 5 components: ingestion, queue, storage, querying, alerting
- See the complete architecture before optimizing
- Understand how distributed systems connect
- **[→ Start with Phase 0 PoC](phase0/README.md)**

### **Then Optimize: Craft #1 (8-12 hours)** ✅
Optimize metrics ingestion from 200 → ~2k RPS:
- HTTP server from raw sockets
- Thread pool architecture
- Lock-free ring buffers
- Custom JSON parser (zero dependencies)
- **[→ Build Craft #1: Metrics Ingestion](docs/CRAFT1.md)**

### **Then Build: Craft #2 (6-8 hours)** 🚀
Learn distributed message queues:

- **[Craft #2: Distributed Message Queue](craft2/README.md)** - Build file-based queue, then integrate Kafka

### **Coming Soon: Crafts #3-5**
Build remaining components production-grade:

- **[Craft #3: Time-Series Storage Engine](craft3/README.md)** - InfluxDB-like database
- **[Craft #4: Query & Aggregation Engine](craft4/README.md)** - PromQL-like query processor
- **[Craft #5: Alerting & Notification System](craft5/README.md)** - PagerDuty-like alerting

## 🏗️ Complete System Architecture

By the end, you'll have built this complete distributed system:

```
Applications → [Craft #1: Ingestion] → [Craft #2: Queue] → [Craft #3: Storage]
                                                                    ↓
              [Craft #5: Alerting] ← [Craft #4: Query Engine]
```

**Start with Craft #0 to see it all working end-to-end (2-3 hours), then optimize each component.**

## 💡 What Makes This Different

### 1. Start with the Big Picture
Most tutorials dive into details. **Systems Craft starts with a working proof-of-concept (Craft #0).**

In 2-3 hours, you build all 5 components working together. When you optimize ingestion (Craft #1), you understand it feeds a queue (Craft #2). When you build storage (Craft #3), you know how queries (Craft #4) will read from it.

**This is how senior engineers think—big picture first, then optimize.**

### 2. Service Decomposition & Team Coordination
In production, each craft would be its own service owned by a separate team:
- **Ingestion Team** maintains the ingestion API
- **Queue Team** operates the message queue
- **Storage Team** manages the time-series database
- **Query Team** owns the aggregation engine
- **Alerting Team** runs notifications

You'll learn to:
- Define service boundaries and APIs
- Design contracts between components
- Enable parallel development (perfect for AI agents!)
- Coordinate across team/service boundaries

### 3. The Future: AI Coordination Skills
In 2-3 years, every engineer will coordinate multiple AI agents to build systems. You won't write every line—you'll orchestrate agents like a tech lead manages a team.

**Critical skills for this future:**
- **Decomposition:** Break systems into independent services
- **API Design:** Define contracts so agents work in parallel
- **Coordination:** Manage multiple agents building different components
- **Code Review:** Review AI-generated code for design and performance
- **Architecture:** Make high-level decisions while agents handle implementation

**Systems Craft teaches you both:** distributed systems design AND the decomposition/coordination skills to work with AI at scale.

### 4. Learn by Measuring
Every optimization is driven by measurement:
- Measure baseline performance
- Identify the bottleneck
- Implement optimization
- Measure improvement
- Document learning

**See [performance_results.txt](performance_results.txt) for the complete optimization journey.**

## 📚 Current Status

> **Understanding Crafts & Phases:** Each **Craft** is a complete system component. Each craft is built through multiple **Phases** (optimization or implementation steps). Each craft starts with Phase 1 for clarity. See [Phases to Crafts Mapping](docs/PHASES_TO_CRAFTS_MAPPING.md) for details.

### ✅ Craft #0: Complete PoC (Available)
**Location:** [`phase0/`](phase0/)

All 5 components in 600 lines:
- Ingestion API (single-threaded HTTP server)
- In-memory queue (thread-safe buffer)
- Storage consumer (background file writer)
- Query API (HTTP GET with filtering)
- Alerting engine (rule evaluation)

**Performance:** ~50-100 RPS (intentionally simple)
**Time:** 2-3 hours

### ✅ Craft #1: Metrics Ingestion (Available)
**Location:** [`src/`](src/) | **Documentation:** [`craft1/README.md`](craft1/README.md)

Optimized through **8 phases**:
- **Phase 1:** Threading per request → 88% success @ 20 clients
- **Phase 2:** Async I/O (producer-consumer) → 66% success @ 50 clients
- **Phase 3:** JSON parsing optimization → 80% success, 2.73ms latency
- **Phase 4:** Per-client mutex pools → incremental improvement
- **Phase 5:** Thread pool architecture → 100% success, 0.65ms latency
- **Phase 6:** Lock-free ring buffers → eliminated overhead
- **Phase 7:** HTTP Keep-Alive → **~2k RPS, 0.25ms latency** ✅
- **Phase 8:** Event-driven I/O (epoll/kqueue) → 🚧 in progress

**Current Performance (Phase 7):** ~2k RPS sustained, 100% reliability, p50 = 0.25ms
**Time:** 8-12 hours total across all phases

### 🚀 Craft #2: Distributed Message Queue (In Progress)
**Location:** [`craft2/`](craft2/) | **Documentation:** [`craft2/README.md`](craft2/README.md)

Kafka-like distributed queue through **3 phases**:
- **Phase 1:** File-based partitioned queue → ✅ **COMPLETE** (800 RPS, 0.70ms latency)
- **Phase 2:** Kafka integration & comparison → ✅ **COMPLETE** (100K+ RPS, 0.15ms latency, 125x faster!)
- **Phase 3:** Distributed coordination (ZooKeeper/Raft) → 📝 Planned

**Current Performance:** 100,000+ RPS with Kafka backend (97.9% success rate)
**Time:** 8-12 hours total across all phases
**Status:** Phase 1-2 complete, Phase 3 in design

**What You'll Learn:**
- Message queue fundamentals: partitioning, offsets, producer/consumer patterns
- Threading bugs in concurrent systems (4 critical bugs found and fixed!)
- File-based vs Kafka architecture comparison (hands-on performance analysis)
- librdkafka integration and thread safety patterns

### 🔜 Craft #3: Time-Series Storage Engine (Coming Soon)
**Location:** [`craft3/`](craft3/)

InfluxDB-like time-series database:
- LSM tree storage engine
- Gorilla compression
- Tag-based indexing
- Time-range queries

**Target:** 1M+ writes/sec, 10:1 compression

### 🔜 Craft #4: Query & Aggregation Engine (Coming Soon)
**Location:** [`craft4/`](craft4/)

PromQL-like query processor:
- Query parser and AST
- Execution planning
- Parallel aggregation
- Result caching

**Target:** Query 100M+ data points in <1 second

### 🔜 Craft #5: Alerting & Notification System (Coming Soon)
**Location:** [`craft5/`](craft5/)

PagerDuty-like alerting platform:
- Event-driven rule evaluation
- Alert state machine
- Multi-channel notifications
- Escalation policies

**Target:** 10K+ rules/sec, <1 min detection latency

## 🚀 Quick Start

### 1. Start with Craft #0 (Recommended)
```bash
cd phase0
./build.sh
cd build
./phase0_poc
```

In another terminal:
```bash
cd phase0
./demo.sh  # See the complete system in action
```

**See [phase0/README.md](phase0/README.md) for full tutorial.**

### 2. Then Build Craft #1
```bash
mkdir build && cd build
cmake .. && make
./metricstream_server
```

Test it:
```bash
./load_test 8080 50 10    # 50 clients, 10 requests each
./performance_test.sh     # Systematic load testing
```

## 📖 Documentation

- **[phase0/README.md](phase0/README.md)** - Complete PoC tutorial (start here!)
- **[phase0/TUTORIAL.md](phase0/TUTORIAL.md)** - Step-by-step 2-3 hour guide
- **[phase0/EXERCISES.md](phase0/EXERCISES.md)** - 10 hands-on exercises
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Complete system design
- **[docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md)** - Architecture Decision Records
- **[docs/CAPACITY_PLANNING.md](docs/CAPACITY_PLANNING.md)** - Scaling analysis with AWS costs
- **[CLAUDE.md](CLAUDE.md)** - Guidance for AI agents working on this codebase

## 🎓 Who This Is For

### Junior Engineers Breaking Into Systems Roles
Stand out by demonstrating:
- "I built a complete monitoring platform—ingestion, storage, querying, alerting"
- "I understand trade-offs in distributed systems design"
- "I can decompose systems and coordinate AI agents building in parallel"

### Mid-Level Engineers Leveling Up
Senior/staff roles require:
- How to architect large-scale systems
- Trade-offs between consistency, availability, and partition tolerance
- How to decompose systems so teams (or AI agents) can build in parallel
- Understanding storage engines, query processors, and distributed protocols

### Anyone Who Wants to Understand Infrastructure
Maybe you use Prometheus, InfluxDB, or Kafka. **Systems Craft shows you how they work internally**—so you can design better systems, debug production issues, and make informed architectural decisions.

## 🏆 What You'll Learn

**Systems Design Skills:**
- Data modeling for access patterns
- Architecture trade-offs (consistency vs availability, latency vs throughput)
- Scalability patterns (sharding, replication, load balancing)
- Fault tolerance (what happens when nodes fail)

**AI Coordination Skills:**
- Service decomposition and API design
- Coordinating parallel development across components
- Code review and architecture decisions
- Managing teams of AI agents like a tech lead

**Implementation Skills:**
- Concurrent programming patterns
- Performance optimization methodology
- Storage engine internals
- Distributed protocols

## 📁 Repository Structure

```
/phase0                  # Craft #0: Complete 600-line PoC (START HERE!)
/src                     # Craft #1: Optimized ingestion implementation
/craft2-5                # Crafts #2-5: Coming soon (design docs available)
/docs                    # Technical documentation
/learning-resources      # Educational materials and visualizations
/tests                   # Test suites
/build                   # Build artifacts (generated)
```

## 🤝 For AI Agents

If you're an AI agent working on this codebase:
1. Read [CLAUDE.md](CLAUDE.md) for project context and workflow
2. Read [AGENTS.md](../AGENTS.md) for commands and conventions
3. Always measure before optimizing (use load_test, document results)
4. Follow the skills workflow for worktrees, TDD, debugging

## 📊 Performance Journey (Craft #1)

| Phase | Optimization | Result @ 100 clients |
|-------|-------------|---------------------|
| **Phase 1** | Threading per request | 20 clients: 81% → 88% success |
| **Phase 2** | Async I/O with producer-consumer | 50 clients: 59% → 66% success |
| **Phase 3** | JSON parsing (O(n²)→O(n)) | 80.2% success, 2.73ms latency |
| **Phase 4** | Hash-based per-client mutex | 95%+ success |
| **Phase 5** | Thread pool architecture | 100% success, 0.65ms latency |
| **Phase 6** | Lock-free ring buffer | Eliminated collection overhead |
| **Phase 7** | HTTP Keep-Alive | **100% success, 0.25ms, ~2k RPS** |

## 📋 Prerequisites

**Programming Experience:** You don't need C++ experience! Systems Craft teaches C++ fundamentals as you build. If you have programming experience in **Python, Go, Rust, Java, or JavaScript**, you'll learn quickly. Core concepts (concurrency, async I/O, data structures) transfer across languages.

**System Requirements:**
- C++17 or later
- CMake 3.16+
- Linux or macOS
- ~2-3 hours for Craft #0 (complete PoC)
- ~8-12 hours per craft for optimization/building

**What You Should Know:**
- Basic programming concepts (loops, functions, data structures)
- Command line / terminal basics
- Understanding of HTTP and basic networking helps but isn't required

## 📝 License

This is an educational project. Feel free to use, modify, and learn from it.

## 🚀 Getting Started Now

**The best way to learn is by doing:**

1. **[→ Start with Craft #0 PoC (2-3 hours)](phase0/README.md)**
2. Build all 5 components, see the complete system
3. Then optimize each component (Crafts #1-5)

**Don't just read about distributed systems. Design and build them.**

**Welcome to Systems Craft.**
