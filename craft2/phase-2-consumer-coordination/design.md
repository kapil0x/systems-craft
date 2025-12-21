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

### Big Picture: How Everything Works Together

Before diving into file formats and rebalancing, let's see **how consumers coordinate** to process messages from a queue.

**Simple Scenario:** 3 consumer processes sharing work from 4 queue partitions.

---

#### Setup: Messages Waiting in Queue

```
queue/
├── partition-0/  (150 messages waiting)
│   ├── 00000000000000001.msg
│   ├── 00000000000000002.msg
│   └── ... (148 more)
│
├── partition-1/  (150 messages waiting)
│   ├── 00000000000000001.msg
│   ├── 00000000000000002.msg
│   └── ... (148 more)
│
├── partition-2/  (150 messages waiting)
└── partition-3/  (150 messages waiting)

Total: 600 messages to process
```

---

#### Consumer Coordination via Files

**Consumer Group:** `metrics-processors`

**File System State:**
```
consumer-groups/metrics-processors/
├── coordinator.lock              (empty file, not locked)
├── generation.txt                → 12
├── members/
│   ├── consumer-a-7001.json      → {consumer_id, hostname, joined_at}
│   ├── consumer-b-7002.json
│   └── consumer-c-7003.json
├── assignments.json              → {generation: 12, assignments: {A: [0,1], B: [2], C: [3]}}
└── offsets/
    ├── partition-0.offset        → 0 (start from beginning)
    ├── partition-1.offset        → 0
    ├── partition-2.offset        → 0
    └── partition-3.offset        → 0

.coordinator/heartbeats/
├── consumer-a-7001.heartbeat     → {timestamp: recent, generation: 12}
├── consumer-b-7002.heartbeat     → {timestamp: recent, generation: 12}
└── consumer-c-7003.heartbeat     → {timestamp: recent, generation: 12}
```

**Partition Assignment:**
```
P0 → Consumer A
P1 → Consumer A
P2 → Consumer B
P3 → Consumer C
```

---

#### How Consumers Process (Simplified)

**Consumer A** (processes P0, P1):

```cpp
// Main loop
while (running_) {
    // 1. Check for rebalance (reads generation.txt every 100ms)
    if (read_generation() != my_generation_) {
        rebalance();  // Stop, rejoin group
    }

    // 2. Read next message from assigned partitions
    for (int p : my_partitions_) {  // [0, 1]
        auto msg = read_next_message(p);
        if (msg) {
            process(msg);  // Business logic
            commit_offset_periodically(p, msg->offset);
        }
    }

    // 3. Send heartbeat every 5s (background thread)
    update_heartbeat_file();

    // 4. Check if other consumers are alive every 10s
    check_other_heartbeats();
}
```

**Key Points:**
1. **Reads `assignments.json` on startup** → knows to process P0, P1
2. **Reads `offsets/*.offset`** → knows where to resume (offset 0)
3. **Writes `offsets/*.offset`** → saves progress after processing
4. **Reads `generation.txt` frequently** → detects if rebalance happening
5. **Writes `heartbeats/*.heartbeat`** → proves it's alive
6. **Reads `heartbeats/*.heartbeat`** → monitors other consumers

**Consumer B** and **Consumer C** do the exact same thing with their assigned partitions.

---

#### File Usage Summary

| File | Who Reads? | Who Writes? | When? | Why? |
|------|-----------|-------------|--------|------|
| `coordinator.lock` | All consumers | First to rebalance | During rebalance | Prevent concurrent rebalancing |
| `generation.txt` | All consumers (every 100ms) | Rebalance initiator | On membership change | Version fence to detect changes |
| `members/*.json` | All consumers (startup) | Each consumer | Join/leave | Track who's in the group |
| `assignments.json` | All consumers (startup + rebalance) | Rebalance initiator | On membership change | Define partition ownership |
| `offsets/*.offset` | Owning consumer (startup) | Owning consumer (periodic) | Every N messages | Track processing progress |
| `heartbeats/*.heartbeat` | All consumers (every 10s) | Each consumer (every 5s) | Continuously | Prove liveness |

---

#### Example Processing Timeline

**T=0s:** All consumers start

```
Consumer A:
  - Reads assignments.json → learns it owns [P0, P1]
  - Reads offset files → P0@0, P1@0
  - Starts processing P0 message 1, P1 message 1

Consumer B:
  - Reads assignments.json → learns it owns [P2]
  - Reads offset files → P2@0
  - Starts processing P2 message 1

Consumer C:
  - Reads assignments.json → learns it owns [P3]
  - Reads offset files → P3@0
  - Starts processing P3 message 1
```

**T=5s:** Processing continues, heartbeats sent

```
Consumer A processed 50 messages (P0: 1-25, P1: 1-25)
  - Writes offsets/partition-0.offset → 25
  - Writes offsets/partition-1.offset → 25
  - Writes heartbeats/consumer-a-7001.heartbeat → {timestamp: T=5s}

Consumer B processed 25 messages (P2: 1-25)
  - Writes offsets/partition-2.offset → 25
  - Writes heartbeats/consumer-b-7002.heartbeat → {timestamp: T=5s}

Consumer C processed 25 messages (P3: 1-25)
  - Writes offsets/partition-3.offset → 25
  - Writes heartbeats/consumer-c-7003.heartbeat → {timestamp: T=5s}
```

**T=10s:** All consumers check heartbeats

```
Consumer A checks:
  - Reads heartbeats/consumer-b-7002.heartbeat → timestamp T=5s (5s ago) ✓ healthy
  - Reads heartbeats/consumer-c-7003.heartbeat → timestamp T=5s (5s ago) ✓ healthy
  → All good, continue processing

Consumer B and C do the same → all healthy
```

**T=20s:** Processing complete

```
Final offsets:
  partition-0.offset → 150 (Consumer A)
  partition-1.offset → 150 (Consumer A)
  partition-2.offset → 150 (Consumer B)
  partition-3.offset → 150 (Consumer C)

Total processed: 600 messages
Distribution:
  Consumer A: 300 messages (50%) - owns 2 partitions
  Consumer B: 150 messages (25%) - owns 1 partition
  Consumer C: 150 messages (25%) - owns 1 partition
```

---

#### What the 5 Files Achieve

**Without coordination files**, you'd need:
- Dedicated coordinator process (single point of failure)
- Network protocol for communication
- Complex leader election
- Distributed locks

**With file-based coordination:**
- ✅ Decentralized (any consumer can trigger rebalance)
- ✅ Simple (just file I/O, no network protocol)
- ✅ Durable (crashes don't lose state)
- ✅ Observable (can `cat` files to debug)
- ❌ Single-machine only (files don't work across network)

---

**Next:** See what happens when consumers join/crash (rebalancing)

---

### Real-World Example: Complete Rebalancing Scenario

Now that you've seen the system in steady state, let's walk through a **complete rebalancing scenario** to see how all 5 coordination files work together when membership changes.

**Scenario:** Analytics team running metric processing with a consumer group called `analytics-workers`. We'll watch what happens when:
1. System starts with 2 consumers
2. A 3rd consumer joins (rebalance triggered)
3. One consumer crashes (rebalance triggered again)

---

#### Initial State: 2 Consumers Processing 4 Partitions

**Consumer Group:** `analytics-workers`
**Partitions:** 4 (P0, P1, P2, P3)
**Consumers:**
- `consumer-alice-1001` on host `worker-node-1`
- `consumer-bob-2002` on host `worker-node-1`

**File System State:**
```
consumer-groups/analytics-workers/
├── coordinator.lock              (empty file, not held)
├── generation.txt                3
├── members/
│   ├── consumer-alice-1001.json  (joined 5 minutes ago)
│   └── consumer-bob-2002.json    (joined 5 minutes ago)
├── assignments.json
│   {
│     "generation": 3,
│     "timestamp": "2025-11-03T10:00:00Z",
│     "assignments": {
│       "consumer-alice-1001": [0, 1],
│       "consumer-bob-2002": [2, 3]
│     }
│   }
└── offsets/
    ├── partition-0.offset        12450
    ├── partition-1.offset        12389
    ├── partition-2.offset        12501
    └── partition-3.offset        12478

.coordinator/heartbeats/
├── consumer-alice-1001.heartbeat
│   {
│     "consumer_id": "consumer-alice-1001",
│     "timestamp": "2025-11-03T10:05:23.145Z",  ← 2 seconds ago
│     "generation": 3
│   }
└── consumer-bob-2002.heartbeat
    {
      "consumer_id": "consumer-bob-2002",
      "timestamp": "2025-11-03T10:05:24.891Z",  ← 1 second ago
      "generation": 3
    }
```

**System Behavior:**
- Alice processes partitions P0, P1 (consuming at offset 12450, 12389)
- Bob processes partitions P2, P3 (consuming at offset 12501, 12478)
- Both send heartbeats every 5 seconds
- No coordinator lock held (steady state)

---

#### Event 1: New Consumer Joins (T+0s)

Charlie starts a new consumer process to help with the load.

**Timeline:**

**T+0.000s - Charlie acquires coordinator lock**
```bash
# Charlie's process executes:
int lock_fd = open("consumer-groups/analytics-workers/coordinator.lock", O_RDWR | O_CREAT);
flock(lock_fd, LOCK_EX);  # BLOCKS until acquired (no one holds it, so immediate)
```

**File Change:**
```
coordinator.lock  (Charlie holds exclusive lock - no file content change, kernel tracks owner)
```

**T+0.050s - Charlie reads current state**
```cpp
// Charlie reads membership
members = list_files("consumer-groups/analytics-workers/members/");
// Returns: ["consumer-alice-1001.json", "consumer-bob-2002.json"]

// Charlie reads current generation
generation = read_file("consumer-groups/analytics-workers/generation.txt");
// Returns: 3

// Charlie reads current assignments
assignments = read_json("consumer-groups/analytics-workers/assignments.json");
// Returns: {alice: [0,1], bob: [2,3]}
```

**T+0.100s - Charlie writes own membership file**
```cpp
write_json("consumer-groups/analytics-workers/members/consumer-charlie-3003.json", {
  "consumer_id": "consumer-charlie-3003",
  "process_id": 3003,
  "hostname": "worker-node-1",
  "joined_at": "2025-11-03T10:05:25.100Z",
  "client_metadata": {"version": "1.0.0"}
});
```

**File Change:**
```
members/
├── consumer-alice-1001.json
├── consumer-bob-2002.json
└── consumer-charlie-3003.json  ← NEW
```

**T+0.150s - Charlie increments generation**
```cpp
int new_generation = generation + 1;  // 3 → 4
write_file("consumer-groups/analytics-workers/generation.txt", "4");
```

**File Change:**
```
generation.txt: 3 → 4  ← REBALANCE TRIGGER!
```

**T+0.200s - Charlie computes new assignment**
```cpp
// Get all members
members = ["consumer-alice-1001", "consumer-bob-2002", "consumer-charlie-3003"];

// Use RoundRobinAssignor
RoundRobinAssignor assignor;
new_assignments = assignor.assign(members, 4);

// Result:
// consumer-alice-1001:   [0]     (lost P1)
// consumer-bob-2002:     [1]     (lost P2, P3, gained P1)
// consumer-charlie-3003: [2, 3]  (gained P2, P3)
```

**T+0.250s - Charlie writes new assignments**
```cpp
write_json("consumer-groups/analytics-workers/assignments.json", {
  "generation": 4,
  "timestamp": "2025-11-03T10:05:25.250Z",
  "assignments": {
    "consumer-alice-1001": [0],
    "consumer-bob-2002": [1],
    "consumer-charlie-3003": [2, 3]
  }
});
```

**File Change:**
```
assignments.json:
  generation: 3 → 4
  assignments:
    alice: [0, 1] → [0]
    bob:   [2, 3] → [1]
    charlie: (none) → [2, 3]
```

**T+0.300s - Charlie releases lock and starts consuming**
```cpp
flock(lock_fd, LOCK_UN);
close(lock_fd);

// Charlie starts heartbeat thread
start_heartbeat_thread();

// Charlie loads offsets for P2, P3
offset_p2 = read_file("consumer-groups/analytics-workers/offsets/partition-2.offset");
offset_p3 = read_file("consumer-groups/analytics-workers/offsets/partition-3.offset");
// Returns: 12501, 12478

// Charlie starts consuming from P2@12502, P3@12479
```

**Charlie's heartbeat file created:**
```
.coordinator/heartbeats/consumer-charlie-3003.heartbeat
{
  "consumer_id": "consumer-charlie-3003",
  "timestamp": "2025-11-03T10:05:25.300Z",
  "generation": 4
}
```

---

#### Concurrent Event: Alice and Bob Detect Rebalance (T+0.100s - T+0.500s)

While Charlie is writing files, Alice and Bob are still processing messages. Here's what happens in their main loops:

**Alice's Thread (every 100ms check):**

**T+0.120s - Alice detects generation change**
```cpp
// Alice's main consumption loop
while (running_) {
    // Check generation before processing next batch
    int current_gen = read_file("consumer-groups/analytics-workers/generation.txt");
    // Reads: 4 (was 3 in memory)

    if (current_gen != my_generation_) {
        std::cout << "Rebalance detected! Gen 3 → 4\n";
        on_rebalance_triggered();
        break;
    }

    // (would process messages here, but rebalance detected)
}
```

**T+0.150s - Alice commits current offsets for P0, P1**
```cpp
void on_rebalance_triggered() {
    // Stop consuming
    pause_consumption();

    // Commit current progress
    commit_offset(0, 12455);  // Processed 5 more messages on P0
    commit_offset(1, 12392);  // Processed 3 more messages on P1
}
```

**File Changes:**
```
offsets/partition-0.offset: 12450 → 12455
offsets/partition-1.offset: 12389 → 12392
```

**T+0.400s - Alice tries to acquire lock to read new assignment**
```cpp
void rejoin_group() {
    int lock_fd = open("coordinator.lock", O_RDWR);
    flock(lock_fd, LOCK_EX);  // BLOCKS - Charlie still holds it until T+0.300s

    // Lock acquired at T+0.400s (Charlie released at T+0.300s, Bob might race here)

    // Read new assignment
    assignments = read_json("assignments.json");
    my_partitions_ = assignments["consumer-alice-1001"];  // [0]
    my_generation_ = assignments["generation"];  // 4

    flock(lock_fd, LOCK_UN);
    close(lock_fd);
}
```

**T+0.450s - Alice resumes with new assignment**
```cpp
std::cout << "Rejoined group (gen=4) with partitions: P0\n";

// Load offset for P0 (still owns this)
offset_p0 = read_file("offsets/partition-0.offset");  // 12455

// Resume consuming P0 from 12456
// No longer consuming P1 (Bob owns it now)
resume_consumption();
```

**Bob's Experience (similar timeline, slight race with Alice):**

**T+0.130s** - Detects generation 4
**T+0.160s** - Commits offsets for P2 (12506), P3 (12481)
**T+0.350s** - Tries to acquire lock (Charlie still holds) - BLOCKS
**T+0.420s** - Acquires lock (after Alice releases at T+0.410s)
**T+0.450s** - Reads new assignment: P1 only
**T+0.480s** - Loads offset for P1 (12392, Alice's last commit)
**T+0.500s** - Starts consuming P1 from 12393

---

#### Final State After Rebalance (T+0.500s)

**System State:**
```
Generation: 4
Members: 3 (Alice, Bob, Charlie)

Partition Assignment:
  P0 → Alice   (was Alice, no change)
  P1 → Bob     (was Alice, moved to Bob)
  P2 → Charlie (was Bob, moved to Charlie)
  P3 → Charlie (was Bob, moved to Charlie)

Consumption Status:
  Alice:   P0 @ offset 12456  (consuming)
  Bob:     P1 @ offset 12393  (consuming, took over from Alice)
  Charlie: P2 @ offset 12502, P3 @ offset 12479  (consuming, took over from Bob)

Heartbeats:
  consumer-alice-1001.heartbeat   - generation: 4, timestamp: every 5s
  consumer-bob-2002.heartbeat     - generation: 4, timestamp: every 5s
  consumer-charlie-3003.heartbeat - generation: 4, timestamp: every 5s
```

**Rebalance Impact:**
- **Duration:** ~400ms from first generation change to all consumers resumed
- **Message Gap:** ~40 messages buffered during rebalance (at 100 msg/sec rate)
- **Partition Handoff:** P1 moved from Alice to Bob, P2/P3 moved from Bob to Charlie
- **No Data Loss:** All offsets committed before handoff

---

#### Event 2: Bob Crashes (T+60s)

One minute later, Bob's process crashes (kill -9).

**Timeline:**

**T+60.000s - Bob crashes**
```bash
kill -9 2002  # Bob's process terminated immediately
```

**What happens:**
- Bob's consumption stops instantly
- Bob's heartbeat thread stops
- **Kernel automatically releases Bob's file locks (if any were held)**
- Bob's member file and heartbeat file persist on disk

**T+60.000s to T+90.000s - Heartbeat aging**

Bob's last heartbeat file remains at:
```json
{
  "consumer_id": "consumer-bob-2002",
  "timestamp": "2025-11-03T10:06:24.891Z",  ← 1 second before crash
  "generation": 4
}
```

**Alice and Charlie continue processing normally.**

**T+90.000s - Alice's heartbeat monitor detects timeout**

Alice's consumer includes a background heartbeat monitor (all consumers run this):

```cpp
// Alice's heartbeat monitor (runs every 10 seconds)
void check_heartbeats() {
    auto now = current_time();  // 2025-11-03T10:06:55.000Z

    for (auto& member_file : list_files("members/")) {
        std::string consumer_id = extract_id(member_file);

        auto hb = read_heartbeat(consumer_id);
        auto age = now - hb.timestamp;

        if (age > 30s) {
            std::cout << "Consumer " << consumer_id << " timed out (age="
                      << age << "s), triggering rebalance\n";
            trigger_rebalance(consumer_id);
            break;
        }
    }
}

// Results:
// consumer-alice-1001   - age: 2s   ✓ alive
// consumer-bob-2002     - age: 31s  ✗ TIMEOUT! (crashed at T+60s)
// consumer-charlie-3003 - age: 1s   ✓ alive
```

**T+90.050s - Alice acquires lock and triggers rebalance**

```cpp
void trigger_rebalance(const std::string& dead_consumer) {
    // Acquire coordinator lock
    int lock_fd = open("coordinator.lock", O_RDWR);
    flock(lock_fd, LOCK_EX);

    // Pause own consumption
    pause_consumption();
    commit_offset(0, 15234);  // Save Alice's current progress on P0

    // Remove dead consumer's member file
    remove("members/consumer-bob-2002.json");

    // Remove dead consumer's heartbeat file
    remove(".coordinator/heartbeats/consumer-bob-2002.heartbeat");

    // Increment generation
    int gen = read_file("generation.txt");  // 4
    write_file("generation.txt", gen + 1);  // 5

    // Compute new assignment
    members = ["consumer-alice-1001", "consumer-charlie-3003"];  // Bob removed
    RoundRobinAssignor assignor;
    new_assignments = assignor.assign(members, 4);
    // Result:
    //   consumer-alice-1001:   [0, 2]  (has P0, gains P2)
    //   consumer-charlie-3003: [1, 3]  (gains P1, keeps P3)

    // Write new assignment
    write_json("assignments.json", {
      "generation": 5,
      "timestamp": "2025-11-03T10:06:55.050Z",
      "assignments": {
        "consumer-alice-1001": [0, 2],
        "consumer-charlie-3003": [1, 3]
      }
    });

    // Release lock
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
}
```

**File Changes:**
```
generation.txt: 4 → 5

members/
├── consumer-alice-1001.json
├── consumer-bob-2002.json       ← DELETED
└── consumer-charlie-3003.json

assignments.json:
  generation: 4 → 5
  assignments:
    alice:   [0]    → [0, 2]  (gains P2)
    bob:     [1]    → DELETED
    charlie: [2, 3] → [1, 3]  (gains P1, loses P2)

.coordinator/heartbeats/
├── consumer-alice-1001.heartbeat
├── consumer-bob-2002.heartbeat  ← DELETED
└── consumer-charlie-3003.heartbeat
```

**T+90.100s - Charlie detects generation change and rebalances**

```cpp
// Charlie's main loop
int current_gen = read_file("generation.txt");  // 5
if (current_gen != my_generation_) {  // was 4
    on_rebalance_triggered();
}

// Charlie commits current progress on P2, P3
commit_offset(2, 14103);
commit_offset(3, 14087);

// Charlie acquires lock (Alice already released it)
flock(lock_fd, LOCK_EX);

// Charlie reads new assignment
assignments = read_json("assignments.json");
my_partitions_ = [1, 3];  // Lost P2, gained P1
my_generation_ = 5;

flock(lock_fd, LOCK_UN);

// Charlie loads offset for P1 (Bob's last commit before crash)
offset_p1 = read_file("offsets/partition-1.offset");  // Let's say Bob got to 14562

// Charlie starts consuming P1@14563, P3@14088 (re-reads P3 from checkpoint)
resume_consumption();
```

**T+90.150s - Alice loads new partition P2**

```cpp
// Alice rejoins and reads new assignment: [0, 2]

// Load offset for P2 (Charlie's last commit)
offset_p2 = read_file("offsets/partition-2.offset");  // 14103

// Resume consuming P0@15235, P2@14104
resume_consumption();
```

---

#### Final State After Crash Recovery (T+90.200s)

**System State:**
```
Generation: 5
Members: 2 (Alice, Charlie)

Partition Assignment:
  P0 → Alice   (still Alice)
  P1 → Charlie (was Bob, moved to Charlie)
  P2 → Alice   (was Charlie, moved to Alice)
  P3 → Charlie (still Charlie)

File System:
consumer-groups/analytics-workers/
├── coordinator.lock              (not held)
├── generation.txt                5
├── members/
│   ├── consumer-alice-1001.json
│   └── consumer-charlie-3003.json
├── assignments.json
│   {
│     "generation": 5,
│     "assignments": {
│       "consumer-alice-1001": [0, 2],
│       "consumer-charlie-3003": [1, 3]
│     }
│   }
└── offsets/
    ├── partition-0.offset        15234  (Alice's checkpoint before rebalance)
    ├── partition-1.offset        14562  (Bob's last checkpoint before crash)
    ├── partition-2.offset        14103  (Charlie's checkpoint before rebalance)
    └── partition-3.offset        14087  (Charlie's checkpoint before rebalance)

.coordinator/heartbeats/
├── consumer-alice-1001.heartbeat   (generation: 5)
└── consumer-charlie-3003.heartbeat (generation: 5)
```

**Recovery Summary:**
- Bob crashed at T+60s, but wasn't detected until T+90s (30s timeout)
- Alice detected the failure and coordinated rebalance
- Partitions P1 (Bob's) redistributed to Charlie
- Partition P2 moved from Charlie to Alice (load balancing)
- **No data loss:** Bob's last committed offset (14562) used by Charlie
- **Rebalance duration:** ~150ms
- **Total unavailability window:** ~150ms (Alice and Charlie pause briefly)

---

#### Key Observations from This Example

1. **`coordinator.lock` prevents races:**
   - Only one consumer can modify assignments at a time
   - Charlie, Alice, and Bob safely coordinate despite running concurrently
   - Kernel automatically releases lock on crash (Bob's crash doesn't deadlock system)

2. **`generation.txt` acts as a version fence:**
   - Incremented on every membership change (3 → 4 → 5)
   - Consumers detect stale state immediately (within 100ms poll interval)
   - Prevents split-brain: old consumers can't commit offsets after rebalance

3. **`members/*.json` tracks group membership:**
   - Charlie adds himself to members/ directory
   - Alice removes Bob after timeout
   - Simple file existence = membership

4. **`assignments.json` defines ownership:**
   - Modified only under coordinator lock
   - Contains generation number to detect races
   - Consumers load this after detecting generation change

5. **`heartbeats/*.heartbeat` enables failure detection:**
   - Written every 5 seconds by each consumer
   - Monitored every 10 seconds by all consumers
   - 30-second timeout balances false positives vs detection latency
   - Simple timestamp comparison, no complex consensus needed

6. **Coordination is decentralized:**
   - No dedicated coordinator process
   - Any consumer can trigger rebalance (Alice did it for Bob's crash)
   - First to acquire lock becomes temporary coordinator
   - This is a "leader-less" coordination approach

---

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

**🎯 Design Rationale & Problem Solved**

**The Coordination Problem:**

Without `coordinator.lock`, imagine this race condition:

```
T=0.000s: Consumer A detects Bob crashed, starts rebalance
T=0.001s: Consumer C ALSO detects Bob crashed, starts rebalance
T=0.100s: Both read assignments.json (current: {A: [0,1], B: [2], C: [3]})
T=0.150s: A computes new assignment: {A: [0,2], C: [1,3]}
T=0.151s: C computes new assignment: {A: [0,1,2], C: [3]} (different!)
T=0.200s: A writes assignments.json → {A: [0,2], C: [1,3]}
T=0.201s: C writes assignments.json → {A: [0,1,2], C: [3]}  ← OVERWRITES A's work!
T=0.250s: SPLIT-BRAIN: A thinks it owns [0,2], C thinks A owns [0,1,2]
          → Partition 1 not owned by anyone! Messages lost!
```

**What breaks without coordinator.lock:**
1. **Double rebalancing:** Multiple consumers trigger simultaneous rebalances
2. **Lost partitions:** Conflicting assignments leave partitions unowned
3. **Duplicate ownership:** Two consumers think they own same partition → duplicate processing
4. **Inconsistent state:** `assignments.json` gets corrupted by concurrent writes

**How coordinator.lock solves this:**

```
T=0.000s: Consumer A acquires lock → BLOCKS others
T=0.001s: Consumer C tries to acquire → BLOCKS (waits for A)
T=0.100s: A reads, computes, writes assignments
T=0.200s: A releases lock
T=0.201s: C acquires lock → reads A's NEW assignments
T=0.250s: C sees Bob already removed, no rebalance needed, releases lock
```

**Key Property:** Serializes rebalancing operations (only one at a time).

**Invariants Maintained:**
- Only ONE consumer modifies `assignments.json` at any moment
- All reads of `assignments.json` happen BEFORE or AFTER writes (never during)
- `generation.txt`, `members/`, and `assignments.json` stay consistent (atomic update group)

**Real-World Analogy:**

Think of `coordinator.lock` like a "talking stick" in a meeting:
- Only person holding stick can speak (modify state)
- Others must wait their turn (blocked on lock)
- If speaker leaves meeting suddenly (crash), stick automatically passes to next person (kernel cleanup)
- No two people can hold stick simultaneously (mutual exclusion guarantee)

---

**📊 Read/Write Access Patterns**

**Access Matrix:**

| Operation | Who | Frequency | Blocking? | Duration Held |
|-----------|-----|-----------|-----------|---------------|
| **Acquire lock** | Any consumer detecting membership change | Rare (only during rebalance) | Yes (blocks until available) | 50-200ms |
| **Hold lock** | Rebalance initiator | During entire rebalance process | N/A | 50-200ms |
| **Release lock** | Rebalance initiator | End of rebalance | No | Instant |
| **Check if locked** | None (lock is opaque) | Never | N/A | N/A |

**Lifecycle State Diagram:**

```
[Steady State] ─────────────────────────────────────────────┐
                                                             │
  No one holds lock                                          │
  All consumers processing normally                          │
  Lock file exists but not acquired                          │
                                                             │
         │                                                   │
         │ Trigger: Consumer detects timeout / join         │
         ↓                                                   │
                                                             │
[Consumer A Attempts Lock] ────────────────────────┐        │
                                                    │        │
  A: flock(fd, LOCK_EX) → SUCCESS (acquired)       │        │
  A is now "coordinator"                            │        │
  Other consumers blocked if they try               │        │
                                                    │        │
         │                                          │        │
         │ A performs rebalance                     │        │
         ↓                                          │        │
                                                    │        │
[Critical Section] ──────────────────────────────┐ │        │
                                                 │ │        │
  ATOMIC operation group:                        │ │        │
  1. Read members/*.json                         │ │        │
  2. Read assignments.json                       │ │        │
  3. Compute new assignment                      │ │        │
  4. Increment generation.txt                    │ │        │
  5. Write new assignments.json                  │ │        │
  6. (Optional) Delete/add member files          │ │        │
                                                 │ │        │
  Duration: 50-200ms                             │ │        │
                                                 │ │        │
         │                                       │ │        │
         ↓                                       │ │        │
                                                 │ │        │
[Release Lock] ──────────────────────────────────┘ │        │
                                                   │        │
  A: flock(fd, LOCK_UN) → lock released           │        │
  A: close(fd)                                     │        │
                                                   │        │
         │                                         │        │
         │ If other consumers waiting...           │        │
         ↓                                         │        │
                                                   │        │
[Consumer B Acquires Lock] (if racing) ────────────┘        │
                                                            │
  B sees A already completed rebalance                      │
  B releases lock immediately                               │
                                                            │
         │                                                  │
         └──────────────────────────────────────────────────┘
```

**Concurrency Rules:**

1. **Can 2 processes acquire simultaneously?**
   - **No.** POSIX `flock()` guarantees exclusive access (LOCK_EX)
   - Kernel maintains queue of waiters
   - Second process BLOCKS until first releases

2. **What if process crashes while holding lock?**
   - **Automatic cleanup.** Kernel releases lock when file descriptor closed
   - `close(fd)` happens automatically on process death
   - Next waiter immediately unblocks and acquires lock
   - **No deadlock possible**

3. **Can process read lock status without acquiring?**
   - **No.** `flock()` doesn't support "try-lock" queries
   - Only way to check: attempt `flock()` with LOCK_NB (non-blocking)
   - Not used in this design (consumers always willing to wait)

4. **Lock ordering to prevent deadlocks?**
   - **Not applicable.** Only ONE lock in the entire system
   - No possibility of circular wait (deadlock impossible)

**Performance Characteristics:**

- **Acquisition latency:** ~0.01ms (in-memory kernel operation)
- **Contention:** Rare (only during rebalancing, not steady state)
- **Throughput impact:** Zero (lock not held during message processing)
- **Scalability limit:** ~10-20 consumers (too many simultaneous rebalance attempts cause thundering herd)

**Lock Hold Duration Breakdown:**

```
Total rebalance time: ~150ms
├── Acquire lock:          0.01ms  ( 0.01%)
├── Read files:           10ms     ( 6.67%)
│   ├── List members/
│   ├── Read assignments.json
│   └── Read generation.txt
├── Compute assignment:    5ms     ( 3.33%)
├── Write files:          30ms     (20.00%)
│   ├── Write generation.txt
│   ├── Write assignments.json
│   └── (Optional) Update members/
├── fsync durability:    100ms     (66.67%)
└── Release lock:          0.01ms  ( 0.01%)
```

**Bottleneck:** fsync() calls for durability (can optimize by batching)

---

**🔄 Alternative Designs Considered**

**Alternative 1: POSIX Semaphores (`sem_open`)**

**How it would work:**
```cpp
sem_t* sem = sem_open("/consumer_group_lock", O_CREAT, 0644, 1);
sem_wait(sem);  // Acquire
// Critical section
sem_post(sem);  // Release
sem_close(sem);
```

**Pros:**
- Named semaphores persist across processes
- Simpler API than file locks
- Supports try-wait (non-blocking)

**Cons:**
- ❌ **No automatic cleanup on crash** → deadlock if process dies holding semaphore
- ❌ Requires manual cleanup (sem_unlink) → orphaned semaphores accumulate
- ❌ Not visible in filesystem (can't `ls` to debug)
- ❌ Platform-specific limits (macOS has issues with named semaphores)

**Why rejected:** Crash recovery is critical. `flock()` auto-releases on crash; semaphores don't.

---

**Alternative 2: SQLite Database with Transaction Locks**

**How it would work:**
```cpp
sqlite3* db = open("coordination.db");
sqlite3_exec(db, "BEGIN EXCLUSIVE TRANSACTION");
// Read assignments, compute new, write
sqlite3_exec(db, "COMMIT");
```

**Pros:**
- ACID transactions (atomic updates)
- Single file (simpler than multiple files)
- SQL queries for complex coordination
- Built-in locking

**Cons:**
- ❌ **Single writer bottleneck** → only one transaction at a time
- ❌ Heavyweight (full SQL engine for simple key-value storage)
- ❌ External dependency (need libsqlite3)
- ❌ Lock timeout tuning required (default 5s too long for real-time rebalancing)
- ❌ WAL mode still does fsync (no performance gain)

**Why rejected:** Overkill for this use case. File locks are simpler and sufficient.

---

**Alternative 3: Advisory Lock Files (`lockf()` or `fcntl()`)**

**How it would work:**
```cpp
int fd = open("coordinator.lock", O_RDWR | O_CREAT);
lockf(fd, F_LOCK, 0);  // Acquire
// Critical section
lockf(fd, F_ULOCK, 0);  // Release
close(fd);
```

**Pros:**
- POSIX standard (portable)
- Automatic release on process death
- Byte-range locking (can lock parts of files)

**Cons:**
- ⚠️ **Advisory only** → malicious process can ignore lock
- ⚠️ Behavior varies across NFS implementations
- No functional advantage over `flock()` for this use case

**Why rejected:** `flock()` is simpler and more widely supported.

---

**Alternative 4: Shared Memory with Atomic Operations (`shm_open` + atomics)**

**How it would work:**
```cpp
int fd = shm_open("/consumer_lock", O_CREAT | O_RDWR, 0644);
ftruncate(fd, sizeof(std::atomic<bool>));
std::atomic<bool>* lock = mmap(..., fd, ...);

bool expected = false;
while (!lock->compare_exchange_strong(expected, true)) {
    expected = false;  // Retry
}
// Critical section
lock->store(false);  // Release
```

**Pros:**
- **Fastest** (no syscalls after initial setup)
- Lock/unlock in ~10 nanoseconds (vs 10 microseconds for flock)
- True shared memory (no file I/O)

**Cons:**
- ❌ **No automatic cleanup** → deadlock if process crashes holding lock
- ❌ **No persistence** → state lost if all processes restart
- ❌ Complex cleanup logic needed
- ❌ Requires careful memory barriers and ordering
- ❌ Harder to debug (can't inspect with `ls` or `cat`)

**Why rejected:** We need durability (crash recovery). Shared memory is volatile.

---

**Alternative 5: Redis with SET NX (Network Lock)**

**How it would work:**
```cpp
redis_client.set("coordinator_lock", consumer_id, "NX", "EX", 30);  // 30s expiry
// Critical section
redis_client.del("coordinator_lock");
```

**Pros:**
- Works across network (multi-machine)
- Built-in expiry (automatic cleanup after timeout)
- Atomic operations (SET NX is atomic)
- Simple API

**Cons:**
- ❌ **External dependency** → requires Redis server running
- ❌ Network overhead (latency: ~1-5ms vs 0.01ms for flock)
- ❌ Single point of failure (Redis crash = entire system down)
- ❌ **Overkill for single-machine** coordination (this is Phase 2)
- ❌ Clock skew issues (expiry based on wall-clock time)

**Why rejected:** Phase 2 is single-machine only. Redis is for Phase 3 (distributed).

---

**Why File Locks Won:**

| Criterion | `flock()` | Semaphores | SQLite | Shared Mem | Redis |
|-----------|-----------|------------|--------|------------|-------|
| **Auto cleanup on crash** | ✅ Kernel | ❌ Manual | ✅ Rollback | ❌ Manual | ⚠️ Timeout |
| **Durability** | ✅ Files persist | ❌ Volatile | ✅ DB persists | ❌ Volatile | ✅ Persists |
| **Simplicity** | ✅ 5 LOC | ✅ 6 LOC | ❌ 20+ LOC | ❌ 30+ LOC | ❌ 10+ LOC + server |
| **Debuggability** | ✅ `ls`, `lsof` | ❌ Hidden | ⚠️ SQL client | ❌ Hidden | ⚠️ Redis client |
| **Performance** | ✅ 0.01ms | ✅ 0.01ms | ⚠️ 1-5ms | ✅ 0.00001ms | ❌ 1-10ms |
| **Portability** | ✅ POSIX | ⚠️ macOS issues | ✅ Cross-platform | ⚠️ Complex | ✅ Client libs |
| **Multi-machine** | ❌ No | ❌ No | ❌ No | ❌ No | ✅ Yes |

**Decision:** `flock()` wins for single-machine coordination (Phase 2). Redis will be used in Phase 3 (distributed).

---

**🚀 Evolution Path**

**Phase 2: File-Based (Current)**

```cpp
// Acquire lock
int lock_fd = open("consumer-groups/my-group/coordinator.lock", O_RDWR | O_CREAT, 0644);
int result = flock(lock_fd, LOCK_EX);  // Blocks until acquired

// Critical section: read-modify-write
int generation = read_file("generation.txt");
Assignments assign = read_assignments();
assign.rebalance();
write_file("generation.txt", generation + 1);
write_assignments(assign);

// Release lock
flock(lock_fd, LOCK_UN);
close(lock_fd);
```

**Characteristics:**
- **Scope:** Single machine only (all consumers on same host)
- **Latency:** ~0.01ms to acquire lock
- **Failure handling:** Kernel auto-releases on crash
- **Limitations:** Doesn't work across network (NFS has race conditions)

---

**How Kafka Handles This (Production Reality)**

Kafka uses **ZooKeeper** for coordination, NOT file locks:

```java
// Kafka's Group Coordinator (simplified)
String lockPath = "/consumers/" + groupId + "/lock";

// ZooKeeper ephemeral sequential node for leader election
zookeeper.create(lockPath, data, EPHEMERAL_SEQUENTIAL);

// Lowest sequence number = coordinator
List<String> children = zookeeper.getChildren("/consumers/" + groupId);
String lowestSeq = children.stream().min().get();

if (myNode.equals(lowestSeq)) {
    // I am coordinator - perform rebalance
    performRebalance();
}

// Lock automatically released when session ends (ephemeral node deleted)
```

**Key Differences:**

| Aspect | Phase 2 (File Lock) | Kafka (ZooKeeper) |
|--------|---------------------|-------------------|
| **Scope** | Single machine | Distributed (across data centers) |
| **Lock mechanism** | `flock()` syscall | ZooKeeper ephemeral nodes |
| **Cleanup** | Kernel auto-release | ZooKeeper session timeout |
| **Leader election** | First to acquire lock | Lowest sequence number |
| **Network-aware** | No (local filesystem) | Yes (distributed consensus) |
| **Failure detection** | Immediate (kernel knows) | ~3-30s (ZK session timeout) |
| **Scalability** | ~10 consumers | 1000s of consumers across brokers |

**Why ZooKeeper:**
- **Multi-machine:** Consumers can run on different hosts
- **Consensus:** All consumers agree on same coordinator (no split-brain)
- **Atomic broadcast:** Changes propagate to all nodes reliably
- **Watches:** Consumers notified immediately when lock released (no polling)

---

**How ZooKeeper Will Handle This (Phase 3 Preview)**

```cpp
// Phase 3: ZooKeeper-based coordination (preview)

zhandle_t* zh = zookeeper_init("localhost:2181", watcher_fn, 10000, nullptr, nullptr, 0);

// Attempt to create ephemeral sequential node
std::string lock_path = "/consumers/my-group/lock-";
char created_path[256];

int rc = zoo_create(zh,
                     lock_path.c_str(),
                     consumer_id.c_str(),
                     consumer_id.length(),
                     &ZOO_OPEN_ACL_UNSAFE,
                     ZOO_EPHEMERAL | ZOO_SEQUENCE,  // Auto-delete on disconnect
                     created_path,
                     sizeof(created_path));

// Read all lock nodes, find lowest sequence (= coordinator)
String_vector children;
zoo_get_children(zh, "/consumers/my-group", 0, &children);

std::sort(children.data, children.data + children.count);
std::string coordinator = children.data[0];  // Lowest seq

if (strcmp(created_path, coordinator) == 0) {
    // I am coordinator!
    perform_rebalance();
} else {
    // Watch the node before me (wait for my turn)
    zoo_wexists(zh, get_prev_node(created_path), watcher_fn, nullptr, nullptr);
}

// Cleanup: ephemeral node auto-deleted when session ends
```

**ZooKeeper Benefits:**

1. **Automatic leader election:**
   - Sequential nodes guarantee ordered queue
   - Lowest sequence = coordinator
   - No race conditions (atomic node creation)

2. **Ephemeral nodes:**
   - Auto-deleted when consumer disconnects
   - No manual cleanup needed
   - Faster failure detection (ZK session timeout, not file heartbeat)

3. **Watches (event-driven):**
   - Consumers notified when coordinator changes
   - No polling `generation.txt` every 100ms
   - Lower CPU usage, faster reaction time

4. **Distributed consensus:**
   - Works across machines (WAN)
   - Tolerates network partitions (ZK quorum)
   - Strong consistency guarantees

**Migration Code Example:**

```cpp
// Phase 2 → Phase 3 migration

class CoordinatorLock {
public:
    virtual void acquire() = 0;
    virtual void release() = 0;
    virtual ~CoordinatorLock() = default;
};

// Phase 2 implementation
class FileLock : public CoordinatorLock {
    int fd_;
public:
    void acquire() override {
        fd_ = open("coordinator.lock", O_RDWR | O_CREAT);
        flock(fd_, LOCK_EX);
    }
    void release() override {
        flock(fd_, LOCK_UN);
        close(fd_);
    }
};

// Phase 3 implementation
class ZooKeeperLock : public CoordinatorLock {
    zhandle_t* zh_;
    std::string lock_path_;
public:
    void acquire() override {
        zoo_create(zh_, lock_path_.c_str(), ..., ZOO_EPHEMERAL | ZOO_SEQUENCE, ...);
        wait_until_lowest_sequence();
    }
    void release() override {
        zoo_delete(zh_, lock_path_.c_str(), -1);
    }
};

// Usage (same for both phases)
std::unique_ptr<CoordinatorLock> lock = create_lock(mode);
lock->acquire();
// Critical section
lock->release();
```

**When to Use Each:**

| Use Case | Phase 2 (File Lock) | Phase 3 (ZooKeeper) |
|----------|---------------------|---------------------|
| **Single machine** | ✅ Perfect | ⚠️ Overkill (but works) |
| **Multiple machines (same datacenter)** | ❌ Won't work | ✅ Required |
| **Multiple datacenters** | ❌ Won't work | ✅ Required |
| **<10 consumers** | ✅ Simple | ⚠️ Operational overhead |
| **100s of consumers** | ❌ Too slow | ✅ Designed for this |
| **Learning/prototyping** | ✅ Zero dependencies | ❌ Requires ZK cluster |

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
