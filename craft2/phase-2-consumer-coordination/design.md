# Phase 10: Multi-Process Consumer Coordination

**Date:** 2025-10-25
**Status:** 🔜 Design Phase
**Learning Goal:** Understand distributed coordination - consumer groups, partition assignment, rebalancing, and failure detection

---

## Executive Summary

Phase 10 extends the single-process queue consumer (Phase 9) to support **multiple independent consumer processes** coordinating through file-based locks. This phase teaches the distributed systems fundamentals behind Kafka's consumer group protocol before migrating to ZooKeeper/Raft in Phase 11.

**What We're Building:**
- Consumer group coordinator (file-based locks)
- Dynamic partition assignment and rebalancing
- Heartbeat-based failure detection
- Graceful consumer join/leave protocol

**Why Build This (vs using ZooKeeper directly):**
- Understand coordination primitives from first principles
- See exactly how partition assignment and rebalancing work
- Appreciate distributed consensus when we add ZooKeeper in Phase 11
- Learn the difference between single-machine vs multi-machine coordination

**Progression:**
- **Phase 9:** Single consumer process (multiple threads, one per partition)
- **Phase 10:** Multiple consumer processes (file-based coordination, single machine)
- **Phase 11:** Distributed coordination (ZooKeeper/Raft, multiple machines)

---

## Architecture Overview

### Phase 9 System (Single Process)

```
Queue (4 partitions)
     ↓
┌────────────────────────────────────┐
│  Consumer Process                  │
│                                    │
│  Thread 0 → Partition 0           │
│  Thread 1 → Partition 1           │
│  Thread 2 → Partition 2           │
│  Thread 3 → Partition 3           │
└────────────────────────────────────┘
```

**Limitations:**
- Single point of failure (process crash = no processing)
- No horizontal scaling (can't add more consumers)
- Thread count limited by partitions (4 partitions = max 4 threads)

### Phase 10 System (Multi-Process)

```
Queue (4 partitions)
     ↓
┌─────────────────────────────────────────────────────┐
│             Coordinator                             │
│  (File-based locks + heartbeat monitoring)          │
└─────────────────────────────────────────────────────┘
     ↓                           ↓
┌──────────────────┐    ┌──────────────────┐
│  Consumer A      │    │  Consumer B      │
│                  │    │                  │
│  P0, P2          │    │  P1, P3          │
└──────────────────┘    └──────────────────┘

Benefits:
- Fault tolerance: Consumer A crashes → rebalance → B takes P0, P2
- Horizontal scaling: Add Consumer C → rebalance → distribute load
- Independent processes: Can deploy on different machines (Phase 11)
```

---

## Core Concepts (Learning Focus)

### 1. Consumer Groups

**What:** Named group of consumer processes that coordinate to consume partitions

**Why:**
- **Load balancing:** Distribute partitions across multiple consumers
- **Fault tolerance:** If one consumer dies, others take over its partitions
- **Scalability:** Add more consumers to handle more load

**Example:**
```
Consumer Group "analytics-workers":
  - Process A (consumer-a-1234) → owns [P0, P2]
  - Process B (consumer-b-5678) → owns [P1, P3]

Consumer Group "alerting-system":
  - Process C (alerting-c-9999) → owns [P0, P1, P2, P3]

(Same partitions consumed by BOTH groups independently!)
```

**Key Insight:** Consumer group = unit of parallelism and fault tolerance

---

### 2. Partition Assignment

**What:** Protocol for deciding which consumer owns which partitions

**Strategies:**
1. **Range assignment:** Divide partitions into contiguous ranges
2. **Round-robin:** Distribute partitions evenly in circular fashion
3. **Sticky assignment:** Minimize partition movement during rebalance

**Example (Round-Robin with 4 partitions, 2 consumers):**
```
Initial state:
  Consumer A → [P0, P2]
  Consumer B → [P1, P3]

Consumer C joins:
  Consumer A → [P0]
  Consumer B → [P1]
  Consumer C → [P2, P3]

Consumer B crashes:
  Consumer A → [P0, P1]
  Consumer C → [P2, P3]
```

**Key Property:** Each partition owned by exactly ONE consumer in a group

---

### 3. Rebalancing

**What:** Process of redistributing partitions when consumer group membership changes

**Triggers:**
- New consumer joins
- Existing consumer crashes/leaves
- Partitions added/removed (Phase 11+)

**Protocol Steps:**
1. **Detect change** (heartbeat timeout or join request)
2. **Stop consumption** (all consumers pause)
3. **Reassign partitions** (coordinator calculates new assignment)
4. **Commit offsets** (consumers save progress)
5. **Resume consumption** (consumers start with new partitions)

**Challenge:** During rebalance, NO messages are processed (unavailability window)

---

### 4. Heartbeats & Failure Detection

**What:** Periodic signals from consumers to prove they're alive

**Why:**
- **Liveness detection:** Distinguish crash from slow consumer
- **Automatic recovery:** Dead consumer triggers rebalance
- **Split-brain prevention:** Stale consumers can't own partitions

**Example:**
```
Consumer A heartbeat timeline:
t=0s  ❤️ alive
t=5s  ❤️ alive
t=10s ❤️ alive
t=15s 💀 missed (process crashed)
t=30s ⚠️ timeout → rebalance triggered
```

**Key Parameters:**
- `heartbeat.interval.ms`: How often to send heartbeat (default: 5s)
- `session.timeout.ms`: How long to wait before declaring dead (default: 30s)

---

## File-Based Coordination Design

### Directory Structure

```
/data/message-queue/
├── queue/                              (from Phase 9)
│   ├── partition-0/
│   ├── partition-1/
│   ├── partition-2/
│   └── partition-3/
│
├── consumer-groups/
│   └── analytics-workers/              ← Consumer group name
│       ├── coordinator.lock            ← CRITICAL: Mutual exclusion
│       ├── generation.txt              ← Rebalance epoch number
│       ├── members/
│       │   ├── consumer-a-1234.json    ← Member metadata
│       │   └── consumer-b-5678.json
│       ├── assignments.json            ← Current partition ownership
│       └── offsets/                    (from Phase 9)
│           ├── partition-0.offset
│           ├── partition-1.offset
│           ├── partition-2.offset
│           └── partition-3.offset
│
└── .coordinator/                       ← Process-level coordination
    └── heartbeats/
        ├── consumer-a-1234.heartbeat   ← Timestamp of last heartbeat
        └── consumer-b-5678.heartbeat
```

---

### File Formats

#### 1. `coordinator.lock` (Empty File)

**Purpose:** Mutual exclusion for rebalancing operations

**Usage:**
```cpp
int fd = open("consumer-groups/analytics-workers/coordinator.lock", O_RDWR | O_CREAT);
flock(fd, LOCK_EX);  // Block until acquired - only one process can hold this

// Critical section: read assignments, modify, write back
Assignments current = read_assignments();
current.rebalance();
write_assignments(current);

flock(fd, LOCK_UN);  // Release
close(fd);
```

**Why it works:**
- POSIX `flock()` provides process-level mutual exclusion via kernel
- Lock automatically released on process crash (kernel cleanup)
- Works across processes on same machine

---

#### 2. `generation.txt` (Rebalance Epoch)

**Purpose:** Detect stale assignments during rebalancing

**Format:**
```
5
```

**Usage:**
```cpp
// Consumer reads generation before processing
int generation = read_generation();

// ... processing loop ...

// Before committing offset, verify generation hasn't changed
if (read_generation() != generation) {
    // Rebalance happened - stop processing, rejoin group
    throw RebalanceException();
}
```

**Key Insight:** Generation number = "fence" to prevent stale consumers

---

#### 3. `members/consumer-X.json` (Member Metadata)

**Purpose:** Track which consumers are in the group

**Format:**
```json
{
  "consumer_id": "consumer-a-1234",
  "process_id": 1234,
  "hostname": "worker-node-1",
  "joined_at": "2025-10-25T10:30:00Z",
  "client_metadata": {
    "version": "1.0.0"
  }
}
```

**Lifecycle:**
- Created when consumer joins group
- Deleted when consumer gracefully leaves
- Cleaned up by coordinator when heartbeat times out

---

#### 4. `assignments.json` (Partition Ownership)

**Purpose:** Current partition assignment for the group

**Format:**
```json
{
  "generation": 5,
  "timestamp": "2025-10-25T10:30:15Z",
  "assignments": {
    "consumer-a-1234": [0, 2],
    "consumer-b-5678": [1, 3]
  }
}
```

**Access Pattern:**
```cpp
// Only modified under coordinator.lock
Assignments assign = read_assignments();
assign.add_consumer("consumer-c-9999");
assign.rebalance();  // Recalculate partition distribution
write_assignments(assign);
```

---

#### 5. `.coordinator/heartbeats/consumer-X.heartbeat`

**Purpose:** Liveness detection

**Format:**
```json
{
  "consumer_id": "consumer-a-1234",
  "timestamp": "2025-10-25T10:30:15.123Z",
  "generation": 5
}
```

**Background Thread (Each Consumer):**
```cpp
void heartbeat_loop() {
    while (running_) {
        Heartbeat hb = {
            .consumer_id = consumer_id_,
            .timestamp = current_timestamp(),
            .generation = current_generation_
        };

        write_heartbeat_file(hb);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
```

**Coordinator Check (Runs periodically):**
```cpp
void check_heartbeats() {
    auto now = current_time();

    for (auto& member : get_members()) {
        auto hb = read_heartbeat(member);

        if (now - hb.timestamp > 30s) {
            // Consumer is dead - trigger rebalance
            std::cout << "Consumer " << member << " timed out\n";
            trigger_rebalance(member);
        }
    }
}
```

---

## Component Design

### Component 1: ConsumerGroupCoordinator

**Purpose:** Manage consumer group membership and partition assignment

**Class Definition:**
```cpp
class ConsumerGroupCoordinator {
private:
    std::string group_name_;
    std::string base_path_;
    int num_partitions_;

    // Current state
    int generation_;
    std::map<std::string, std::vector<int>> assignments_;

public:
    ConsumerGroupCoordinator(const std::string& group_name,
                              const std::string& base_path,
                              int num_partitions);

    // Consumer lifecycle
    std::vector<int> join_group(const std::string& consumer_id,
                                 const MemberMetadata& metadata);
    void leave_group(const std::string& consumer_id);

    // Rebalancing
    void trigger_rebalance(const std::string& reason);
    Assignments compute_assignment(const std::vector<std::string>& members);

    // Heartbeat monitoring
    void start_heartbeat_monitor();
    void check_liveness();

    // File operations (with coordinator.lock)
    Assignments read_assignments();
    void write_assignments(const Assignments& assign);
    int acquire_coordinator_lock();
    void release_coordinator_lock(int fd);
};
```

---

### Component 2: ConsumerGroupMember

**Purpose:** Represents a single consumer process in the group

**Class Definition:**
```cpp
class ConsumerGroupMember {
private:
    std::string consumer_id_;
    std::string group_name_;
    ConsumerGroupCoordinator* coordinator_;

    // Current assignment
    int generation_;
    std::vector<int> assigned_partitions_;

    // Heartbeat thread
    std::thread heartbeat_thread_;
    std::atomic<bool> running_;

public:
    ConsumerGroupMember(const std::string& consumer_id,
                         const std::string& group_name);

    // Lifecycle
    void join();
    void leave();

    // Consumption
    void start_consuming();
    void consume_partition(int partition);

    // Heartbeat
    void send_heartbeat();
    void heartbeat_loop();

    // Rebalance handling
    void on_rebalance_triggered();
    void rejoin_group();
};
```

---

### Component 3: PartitionAssignor (Strategy Pattern)

**Purpose:** Pluggable partition assignment strategies

**Interface:**
```cpp
class PartitionAssignor {
public:
    virtual ~PartitionAssignor() = default;

    virtual std::map<std::string, std::vector<int>> assign(
        const std::vector<std::string>& consumers,
        int num_partitions) = 0;
};
```

**Implementation 1: RoundRobinAssignor**
```cpp
class RoundRobinAssignor : public PartitionAssignor {
public:
    std::map<std::string, std::vector<int>> assign(
        const std::vector<std::string>& consumers,
        int num_partitions) override {

        std::map<std::string, std::vector<int>> result;

        // Round-robin distribution
        for (int p = 0; p < num_partitions; p++) {
            std::string consumer = consumers[p % consumers.size()];
            result[consumer].push_back(p);
        }

        return result;
    }
};
```

**Implementation 2: RangeAssignor**
```cpp
class RangeAssignor : public PartitionAssignor {
public:
    std::map<std::string, std::vector<int>> assign(
        const std::vector<std::string>& consumers,
        int num_partitions) override {

        std::map<std::string, std::vector<int>> result;

        int partitions_per_consumer = num_partitions / consumers.size();
        int remainder = num_partitions % consumers.size();

        int partition_idx = 0;
        for (size_t i = 0; i < consumers.size(); i++) {
            int count = partitions_per_consumer + (i < remainder ? 1 : 0);

            for (int j = 0; j < count; j++) {
                result[consumers[i]].push_back(partition_idx++);
            }
        }

        return result;
    }
};
```

---

## Rebalancing Protocol (Detailed)

### Step-by-Step Flow

**Scenario:** Consumer C joins group with existing members A and B

```
Initial State:
  Consumer A → [P0, P1]
  Consumer B → [P2, P3]
  Generation: 3

Consumer C joins:

┌─────────────────────────────────────────────────────────┐
│ Step 1: Consumer C initiates join                       │
└─────────────────────────────────────────────────────────┘

Consumer C:
  1. Acquire coordinator.lock
  2. Read current assignments.json (generation=3)
  3. Write members/consumer-c-9999.json
  4. Increment generation → 4
  5. Write generation.txt = 4
  6. Trigger rebalance flag
  7. Release coordinator.lock

┌─────────────────────────────────────────────────────────┐
│ Step 2: Existing consumers detect rebalance             │
└─────────────────────────────────────────────────────────┘

Consumer A & B:
  1. Detect generation change (3 → 4)
  2. Stop consuming (pause message processing)
  3. Commit current offsets
  4. Wait for new assignment

┌─────────────────────────────────────────────────────────┐
│ Step 3: Coordinator computes new assignment             │
└─────────────────────────────────────────────────────────┘

Coordinator (first consumer to acquire lock):
  1. Acquire coordinator.lock
  2. Read all members: [A, B, C]
  3. Compute assignment using RoundRobinAssignor:
       A → [P0]
       B → [P1]
       C → [P2, P3]
  4. Write assignments.json with generation=4
  5. Release coordinator.lock

┌─────────────────────────────────────────────────────────┐
│ Step 4: Consumers read new assignments and resume       │
└─────────────────────────────────────────────────────────┘

All Consumers:
  1. Read assignments.json (generation=4)
  2. Compare to previous assignment:
       A: [P0, P1] → [P0] (lost P1)
       B: [P2, P3] → [P1] (lost P2, P3, gained P1)
       C: [] → [P2, P3] (gained P2, P3)
  3. For lost partitions: commit final offsets
  4. For new partitions: load committed offsets
  5. Resume consuming with new partitions

┌─────────────────────────────────────────────────────────┐
│ Step 5: System stabilizes                               │
└─────────────────────────────────────────────────────────┘

Final State:
  Consumer A → [P0]
  Consumer B → [P1]
  Consumer C → [P2, P3]
  Generation: 4
```

---

### Rebalance Code Flow

```cpp
// Consumer main loop
void ConsumerGroupMember::start_consuming() {
    while (running_) {
        try {
            // Check for rebalance
            int current_gen = read_generation();
            if (current_gen != generation_) {
                on_rebalance_triggered();
                continue;
            }

            // Consume messages from assigned partitions
            for (int partition : assigned_partitions_) {
                consume_partition(partition);
            }

        } catch (const RebalanceException& e) {
            std::cout << "Rebalance detected, rejoining...\n";
            rejoin_group();
        }
    }
}

void ConsumerGroupMember::on_rebalance_triggered() {
    std::cout << "Rebalance triggered (generation changed)\n";

    // Step 1: Stop consuming
    pause_consumption();

    // Step 2: Commit current offsets
    for (int partition : assigned_partitions_) {
        uint64_t offset = get_current_offset(partition);
        commit_offset(partition, offset);
    }

    // Step 3: Wait for new assignment
    rejoin_group();
}

void ConsumerGroupMember::rejoin_group() {
    // Acquire coordinator lock
    int lock_fd = coordinator_->acquire_coordinator_lock();

    // Read new assignments
    Assignments assign = coordinator_->read_assignments();
    generation_ = assign.generation;
    assigned_partitions_ = assign.assignments[consumer_id_];

    // Release lock
    coordinator_->release_coordinator_lock(lock_fd);

    std::cout << "Rejoined group (gen=" << generation_
              << ") with partitions: ";
    for (int p : assigned_partitions_) {
        std::cout << "P" << p << " ";
    }
    std::cout << "\n";

    // Resume consumption
    resume_consumption();
}
```

---

## Failure Scenarios & Handling

### Scenario 1: Consumer Crash During Normal Processing

**What happens:**
```
Consumer A is processing partition P0 at offset 1250
Consumer A crashes (kill -9)
```

**Recovery:**
1. Heartbeat monitor detects timeout (30s)
2. Coordinator triggers rebalance
3. Consumer B acquires P0
4. Consumer B loads committed offset: 1248 (last commit before crash)
5. Consumer B re-processes messages 1249-1250 (at-least-once delivery)

**Key:** Committed offsets ensure no data loss, but duplicate processing is possible

---

### Scenario 2: Consumer Crash During Rebalance

**What happens:**
```
Rebalance starts (generation 3 → 4)
Consumer B crashes before reading new assignment
```

**Recovery:**
1. Other consumers complete rebalance with generation 4
2. Consumer B's heartbeat times out (30s)
3. Coordinator detects dead consumer, triggers another rebalance
4. Generation 4 → 5
5. Remaining consumers redistribute Consumer B's partitions

**Key:** Multiple rebalances can cascade during failures

---

### Scenario 3: Split-Brain (Stale Consumer)

**What happens:**
```
Consumer A experiences network partition (can't write heartbeat)
Coordinator declares Consumer A dead
Consumer B takes over P0
Consumer A resumes and tries to commit offset for P0
```

**Prevention:**
```cpp
void commit_offset(int partition, uint64_t offset) {
    // Read current assignment
    int lock_fd = acquire_coordinator_lock();
    Assignments assign = read_assignments();

    // Verify we still own this partition in current generation
    if (assign.assignments[consumer_id_].count(partition) == 0 ||
        assign.generation != generation_) {

        release_coordinator_lock(lock_fd);
        throw NotOwnerException("Lost partition ownership");
    }

    // Safe to commit
    write_offset_file(partition, offset);
    release_coordinator_lock(lock_fd);
}
```

**Key:** Always verify ownership before committing offsets (generation fence)

---

### Scenario 4: File Lock Deadlock

**What happens:**
```
Consumer A acquires coordinator.lock
Consumer A crashes before releasing
Lock file persists - no other consumer can acquire
```

**Why it doesn't happen:**
- POSIX `flock()` is automatically released by kernel on process death
- No manual cleanup needed
- This is the magic of file-based locks!

---

## Performance Characteristics

### Coordination Overhead

| Operation | Latency | Frequency |
|-----------|---------|-----------|
| Heartbeat write | ~1ms (file write) | Every 5s per consumer |
| Heartbeat check | ~5ms (read N files) | Every 10s (coordinator) |
| Rebalance | ~100-500ms | On membership change |
| Offset commit | ~1-5ms (file write) | Configurable (default: every 5s) |

### Rebalance Impact

**Downtime during rebalance:**
- Stop consumption: ~10ms (pause threads)
- Acquire lock + compute assignment: ~50ms
- Resume consumption: ~10ms
- **Total: ~70-100ms of unavailability**

**Message processing gap:**
```
If producing 1,000 msg/sec during rebalance:
  100ms downtime = ~100 messages buffered
  Consumers catch up immediately after rebalance
```

---

## Limitations (Phase 10)

**Single-machine only:**
- File-based locks don't work across network filesystem (NFS race conditions)
- All consumers must run on same host
- Limits horizontal scalability

**No automatic leader election:**
- First consumer to acquire lock acts as coordinator
- No dedicated coordinator process (ad-hoc leadership)

**File I/O overhead:**
- Every heartbeat = file write (disk I/O)
- Doesn't scale to 100+ consumers

**Race conditions possible:**
- Simultaneous joins can cause double rebalance
- Requires careful lock ordering

**Solutions (Phase 11 - ZooKeeper):**
- Distributed consensus (works across machines)
- True leader election with automatic failover
- In-memory coordination state (faster)
- Ephemeral nodes (automatic cleanup)

---

## Testing Plan

### Unit Tests

**1. PartitionAssignor Tests:**
```cpp
TEST(RoundRobinAssignor, EvenDistribution) {
    RoundRobinAssignor assignor;
    auto result = assignor.assign({"A", "B", "C"}, 6);

    EXPECT_EQ(result["A"], std::vector<int>({0, 3}));
    EXPECT_EQ(result["B"], std::vector<int>({1, 4}));
    EXPECT_EQ(result["C"], std::vector<int>({2, 5}));
}

TEST(RangeAssignor, UnevenPartitions) {
    RangeAssignor assignor;
    auto result = assignor.assign({"A", "B"}, 5);

    // A gets 3, B gets 2
    EXPECT_EQ(result["A"], std::vector<int>({0, 1, 2}));
    EXPECT_EQ(result["B"], std::vector<int>({3, 4}));
}
```

**2. ConsumerGroupCoordinator Tests:**
```cpp
TEST(Coordinator, JoinGroup) {
    ConsumerGroupCoordinator coord("test-group", "/tmp/queue", 4);

    auto partitions = coord.join_group("consumer-A", {});
    EXPECT_EQ(partitions.size(), 4);  // Owns all partitions
}

TEST(Coordinator, RebalanceOnJoin) {
    ConsumerGroupCoordinator coord("test-group", "/tmp/queue", 4);

    coord.join_group("consumer-A", {});  // A gets [0,1,2,3]
    coord.join_group("consumer-B", {});  // Rebalance: A=[0,1], B=[2,3]

    auto assign = coord.read_assignments();
    EXPECT_EQ(assign.assignments["consumer-A"], std::vector<int>({0, 1}));
    EXPECT_EQ(assign.assignments["consumer-B"], std::vector<int>({2, 3}));
}
```

**3. Heartbeat & Failure Detection:**
```cpp
TEST(Coordinator, DetectDeadConsumer) {
    ConsumerGroupCoordinator coord("test-group", "/tmp/queue", 4);

    coord.join_group("consumer-A", {});

    // Simulate missed heartbeats
    sleep(35);  // Exceeds 30s timeout

    coord.check_liveness();

    // Consumer A should be removed
    auto members = coord.get_members();
    EXPECT_EQ(members.size(), 0);
}
```

---

### Integration Tests

**1. Multi-Process Rebalancing:**
```bash
#!/bin/bash
# Start 2 consumers
./consumer --group=test-group --id=A &
PID_A=$!
./consumer --group=test-group --id=B &
PID_B=$!

sleep 5  # Let them stabilize

# Verify partition assignment
cat consumer-groups/test-group/assignments.json
# Expected: A=[0,1], B=[2,3]

# Add third consumer
./consumer --group=test-group --id=C &
PID_C=$!

sleep 2  # Wait for rebalance

# Verify new assignment
cat consumer-groups/test-group/assignments.json
# Expected: A=[0], B=[1], C=[2,3]

# Kill consumer B
kill -9 $PID_B

sleep 35  # Wait for heartbeat timeout + rebalance

# Verify assignment after failure
cat consumer-groups/test-group/assignments.json
# Expected: A=[0,1], C=[2,3]

# Cleanup
kill $PID_A $PID_C
```

**2. Offset Commit Verification:**
```bash
# Send 100 messages to queue
for i in {1..100}; do
  curl -X POST http://localhost:8080/metrics -d '{...}'
done

# Start consumer A
./consumer --group=test-group --id=A &
PID_A=$!

sleep 10  # Process ~50 messages

# Kill consumer mid-processing
kill -9 $PID_A

# Check committed offset
cat consumer-groups/test-group/offsets/partition-0.offset
# Expected: ~50 (not 100)

# Restart consumer A
./consumer --group=test-group --id=A &

# Verify it resumes from offset 50 (not 0)
```

---

### Performance Tests

**1. Rebalance Latency:**
```cpp
// Measure time from membership change to assignment complete
auto start = std::chrono::steady_clock::now();

coordinator.join_group("consumer-X", {});

auto end = std::chrono::steady_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "Rebalance latency: " << latency.count() << "ms\n";
// Target: < 100ms
```

**2. Heartbeat Overhead:**
```bash
# Measure file I/O during heartbeat loop
strace -c -e write ./consumer --group=test-group --id=A

# Count write() syscalls per second
# Target: ~0.2 write/sec (1 heartbeat every 5s)
```

**3. Scale Test:**
```bash
# Spawn 10 consumers simultaneously
for i in {1..10}; do
  ./consumer --group=test-group --id=consumer-$i &
done

# Measure:
# - Time to stabilize (all consumers assigned)
# - Number of rebalances triggered
# - Final partition distribution (should be even)
```

---

## Implementation Phases

### Phase 10A: Coordinator Core (3-4 hours)

**Tasks:**
1. Create `ConsumerGroupCoordinator` class
2. Implement file-based locking (`flock()`)
3. Implement partition assignment strategies (RoundRobin, Range)
4. Add join/leave group methods
5. Unit tests for assignment logic

**Success Criteria:**
- [ ] Multiple processes can acquire/release coordinator lock
- [ ] Partition assignment distributes evenly
- [ ] Assignment persisted to `assignments.json`

---

### Phase 10B: Heartbeat & Failure Detection (2-3 hours)

**Tasks:**
1. Implement heartbeat background thread
2. Add heartbeat file write/read
3. Implement liveness checking in coordinator
4. Trigger rebalance on timeout
5. Integration test: kill consumer, verify rebalance

**Success Criteria:**
- [ ] Heartbeats written every 5s
- [ ] Dead consumer detected within 30s
- [ ] Automatic rebalance triggered

---

### Phase 10C: Consumer Integration (3-4 hours)

**Tasks:**
1. Extend `QueueConsumer` (Phase 9) to use coordinator
2. Implement rebalance handling in consumer loop
3. Add generation tracking and verification
4. Build multi-consumer demo with 3 processes
5. End-to-end integration test

**Success Criteria:**
- [ ] Multiple consumers process different partitions
- [ ] Consumer join triggers rebalance
- [ ] Consumer crash triggers rebalance and recovery

---

### Phase 10D: Testing & Documentation (2-3 hours)

**Tasks:**
1. Comprehensive integration tests (multi-process scenarios)
2. Performance benchmarking (rebalance latency, heartbeat overhead)
3. Failure scenario testing (crash, timeout, split-brain)
4. Document results in `results.md`
5. Create learning exercises comparing Phase 9 vs Phase 10

**Success Criteria:**
- [ ] All failure scenarios handled gracefully
- [ ] Rebalance latency < 100ms
- [ ] Documentation complete with examples

---

## Learning Outcomes

By the end of Phase 10, you will understand:

1. **Consumer group coordination:**
   - How multiple processes share work
   - Partition assignment strategies
   - Trade-offs between even distribution and stability

2. **Rebalancing mechanics:**
   - When and why rebalancing happens
   - The "stop-the-world" nature of rebalance
   - Generation numbers as coordination fences

3. **Failure detection:**
   - Heartbeat-based liveness checking
   - Timeout tuning (false positives vs detection latency)
   - Split-brain prevention with generation fences

4. **File-based coordination:**
   - Using `flock()` for mutual exclusion
   - Atomic file operations (rename, write)
   - Limitations of single-machine coordination

5. **Why distributed consensus matters:**
   - Phase 10 limitations (single machine only)
   - What ZooKeeper/Raft solve (Phase 11 preview)
   - CAP theorem trade-offs

---

## Migration to ZooKeeper (Phase 11 Preview)

### What Stays the Same
- Consumer group concept
- Partition assignment strategies
- Heartbeat-based failure detection
- Generation numbers for fencing

### What Changes with ZooKeeper

**File System → ZooKeeper:**
```
consumer-groups/analytics-workers/
├── coordinator.lock         → /consumers/analytics-workers/lock (ephemeral)
├── generation.txt           → /consumers/analytics-workers/generation (znode)
├── members/                 → /consumers/analytics-workers/ids/ (ephemeral)
├── assignments.json         → /consumers/analytics-workers/owners/ (znodes)
└── offsets/                 → /consumers/analytics-workers/offsets/ (znodes)
```

**Key Benefits:**
- **Multi-machine:** Works across network (distributed consensus)
- **Ephemeral nodes:** Automatic cleanup on disconnect (no heartbeat files)
- **Watches:** Event-driven notifications (no polling for generation changes)
- **Transactions:** Atomic multi-znode updates (safer rebalancing)

**Code Changes (Preview):**
```cpp
// Phase 10: File-based
int fd = open("coordinator.lock", O_RDWR);
flock(fd, LOCK_EX);
// ... critical section ...
flock(fd, LOCK_UN);

// Phase 11: ZooKeeper
zoo_create(zh, "/consumers/test-group/lock", nullptr, 0,
           &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE, lock_path);
// Lock acquired (lowest sequence number wins)
// Automatically released on disconnect
```

---

## Next Steps

Ready to implement Phase 10A (Coordinator Core)?

**Suggested workflow:**
1. Use `/worktree` slash command to create branch: `phase-10-consumer-coordination`
2. Implement `ConsumerGroupCoordinator` with TDD
3. Test with 2 consumer processes locally
4. Gradually add rebalancing and failure detection
5. Document learnings in `results.md`

---

## References

**Kafka Protocol Documentation:**
- [KIP-62: Consumer Group Protocol](https://cwiki.apache.org/confluence/display/KAFKA/KIP-62)
- [Consumer Rebalance Protocol](https://kafka.apache.org/protocol#The_Messages_GroupCoordinator)

**File Locking:**
- `man 2 flock` (POSIX file locking)
- `man 2 rename` (atomic file operations)

**Distributed Coordination:**
- ZooKeeper Programmer's Guide
- Raft Consensus Algorithm (Diego Ongaro)
