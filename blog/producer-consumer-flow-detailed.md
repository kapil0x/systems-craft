# Complete Producer-Consumer Flow: Multi-Process Deep Dive

**A step-by-step walkthrough of exactly what happens when messages flow through a partitioned queue with multiple producers, multiple consumer groups, and everything in between.**

---

## Scenario Setup

### System Configuration

```
Queue Configuration:
- 4 partitions (partition-0, partition-1, partition-2, partition-3)
- Hash-based routing: partition = hash(client_id) % 4

Producers (2 processes):
- Producer A (PID 1001)
- Producer B (PID 1002)

Consumer Group "storage-writer" (2 processes):
- Consumer S1 (PID 2001) → assigned partitions [0, 1]
- Consumer S2 (PID 2002) → assigned partitions [2, 3]

Consumer Group "analytics" (2 processes):
- Consumer A1 (PID 3001) → assigned partitions [0, 2]
- Consumer A2 (PID 3002) → assigned partitions [1, 3]
```

### Initial Directory Structure

```
/data/message-queue/
├── queue/
│   ├── partition-0/
│   │   ├── offset.txt                    (current: 10)
│   │   ├── 00000000000001.msg
│   │   ├── ...
│   │   └── 00000000000010.msg
│   ├── partition-1/
│   │   ├── offset.txt                    (current: 15)
│   │   └── ...
│   ├── partition-2/
│   │   ├── offset.txt                    (current: 8)
│   │   └── ...
│   └── partition-3/
│       ├── offset.txt                    (current: 12)
│       └── ...
│
├── consumer-groups/
│   ├── storage-writer/
│   │   ├── coordinator.lock
│   │   ├── generation.txt                (current: 5)
│   │   ├── assignments.json              (S1=[0,1], S2=[2,3])
│   │   └── offsets/
│   │       ├── partition-0.offset        (committed: 9)
│   │       ├── partition-1.offset        (committed: 14)
│   │       ├── partition-2.offset        (committed: 7)
│   │       └── partition-3.offset        (committed: 11)
│   └── analytics/
│       ├── coordinator.lock
│       ├── generation.txt                (current: 3)
│       ├── assignments.json              (A1=[0,2], A2=[1,3])
│       └── offsets/
│           ├── partition-0.offset        (committed: 8)
│           ├── partition-1.offset        (committed: 13)
│           ├── partition-2.offset        (committed: 6)
│           └── partition-3.offset        (committed: 10)
│
└── .coordinator/
    └── heartbeats/
        ├── consumer-s1-2001.heartbeat    (last: T+0s)
        ├── consumer-s2-2002.heartbeat    (last: T+0s)
        ├── consumer-a1-3001.heartbeat    (last: T+0s)
        └── consumer-a2-3002.heartbeat    (last: T+0s)
```

### Key Observations from Initial State

**Producer offsets** (next write position per partition):
- P0: offset 10 → next message will be offset 11
- P1: offset 15 → next message will be offset 16
- P2: offset 8 → next message will be offset 9
- P3: offset 12 → next message will be offset 13

**Consumer "storage-writer" offsets** (last processed):
- P0: committed 9 → next read will be offset 10
- P1: committed 14 → next read will be offset 15
- P2: committed 7 → next read will be offset 8
- P3: committed 11 → next read will be offset 12

**Consumer "analytics" offsets** (lagging behind):
- P0: committed 8 → next read will be offset 9 (1 message behind)
- P1: committed 13 → next read will be offset 14 (1 message behind)
- P2: committed 6 → next read will be offset 7 (1 message behind)
- P3: committed 10 → next read will be offset 11 (1 message behind)

**Insight:** "storage-writer" is caught up, "analytics" has 1 message lag per partition!

---

## Message Flow Timeline

### T+0ms: Producer A Writes Message #1

**Client:** `client_abc` sends message `{"metric": "cpu_usage", "value": 75.5}`

#### Step 1: Partition Routing

```cpp
// Producer A (PID 1001)
std::string client_id = "client_abc";
std::hash<std::string> hasher;
size_t hash_value = hasher(client_id);  // Let's say = 18234
int partition = hash_value % 4;          // 18234 % 4 = 2

std::cout << "Message routed to partition-2\n";
```

**Result:** Message goes to `partition-2`

#### Step 2: Acquire Partition Lock

```cpp
// Producer A needs exclusive access to partition-2
std::lock_guard<std::mutex> lock(partition_mutexes_[2]);

// Memory state (Producer A only):
// partition_mutexes_[0] = unlocked
// partition_mutexes_[1] = unlocked
// partition_mutexes_[2] = LOCKED by Producer A  ←
// partition_mutexes_[3] = unlocked
```

**Lock held:** Producer A owns partition-2 mutex (in-process, memory-only)

#### Step 3: Read Current Offset

```cpp
// Read queue/partition-2/offset.txt
std::ifstream offset_file("queue/partition-2/offset.txt");
uint64_t current_offset;
offset_file >> current_offset;  // Returns: 8

// Increment for new message
uint64_t new_offset = current_offset + 1;  // 9
```

**Offset change:** P2 offset: 8 → 9

#### Step 4: Write Message File

```cpp
// Build filename with zero-padded offset
std::string filename = "queue/partition-2/00000000000009.msg";

std::ofstream msg_file(filename, std::ios::binary);
msg_file << R"({"metric": "cpu_usage", "value": 75.5})";
msg_file.flush();

// Ensure durability (critical!)
int fd = fileno(&msg_file);
fsync(fd);  // Force write to disk (~1-5ms on SSD)
```

**Filesystem change:**
```
queue/partition-2/
├── 00000000000008.msg  (existed)
└── 00000000000009.msg  ← NEW FILE CREATED
```

#### Step 5: Update Offset File

```cpp
std::ofstream offset_file("queue/partition-2/offset.txt");
offset_file << 9;
offset_file.flush();
fsync(fileno(&offset_file));
```

**Offset file change:**
```
Before: queue/partition-2/offset.txt → "8"
After:  queue/partition-2/offset.txt → "9"
```

#### Step 6: Release Lock

```cpp
// lock_guard destructor automatically releases mutex
// partition_mutexes_[2] = unlocked
```

**Total time:** ~2-6ms (dominated by fsync)

**Producer A returns:** `{partition: 2, offset: 9}` to HTTP client

---

### T+3ms: Producer B Writes Message #2 (Concurrent)

**Client:** `client_xyz` sends message `{"metric": "memory_free", "value": 2048}`

#### Step 1: Partition Routing

```cpp
// Producer B (PID 1002)
std::string client_id = "client_xyz";
size_t hash_value = hasher(client_id);  // Let's say = 9871
int partition = hash_value % 4;          // 9871 % 4 = 3
```

**Result:** Message goes to `partition-3` (DIFFERENT partition than Producer A!)

#### Step 2-6: Same Flow, But on Partition-3

```
Lock: partition_mutexes_[3]  (No contention with Producer A!)
Read: queue/partition-3/offset.txt → 12
Write: queue/partition-3/00000000000013.msg
Update: queue/partition-3/offset.txt → 13
Release: partition_mutexes_[3]
```

**Key Insight:** Producer A and Producer B run **in parallel** because they write to different partitions. No lock contention!

**Filesystem state after both producers:**
```
queue/
├── partition-2/
│   ├── 00000000000009.msg  ← Producer A wrote this
│   └── offset.txt (9)
└── partition-3/
    ├── 00000000000013.msg  ← Producer B wrote this
    └── offset.txt (13)
```

---

### T+10ms: Producer A Writes Message #3

**Client:** `client_abc` (again) sends message `{"metric": "cpu_usage", "value": 80.2}`

#### Partition Routing

```cpp
std::string client_id = "client_abc";
int partition = hasher(client_id) % 4;  // Same hash as before → 2
```

**Result:** Goes to `partition-2` (SAME as message #1 - ordering preserved!)

#### Write Flow

```
Lock: partition_mutexes_[2]
Read: queue/partition-2/offset.txt → 9
Write: queue/partition-2/00000000000010.msg  ← Sequential offset
Update: queue/partition-2/offset.txt → 10
Release: partition_mutexes_[2]
```

**Ordering guarantee:** Messages from `client_abc` are written in order (offset 9, then offset 10) because they go to the same partition.

---

### T+15ms: Consumer S1 Reads from Partition-0

**Consumer S1** (storage-writer group, assigned partitions [0, 1])

Consumer S1 is running a loop:

```cpp
void ConsumerGroupMember::consume_partition(int partition) {
    while (running_) {
        // Step 1: Check generation (rebalance detection)
        int current_gen = read_generation();  // Reads generation.txt
        if (current_gen != generation_) {
            throw RebalanceException();  // Not happening now
        }

        // Step 2: Read next message
        auto msg = read_next(partition);

        if (msg) {
            process(msg);
            commit_offset(partition, msg->offset);
        } else {
            // No new messages, sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

#### Step 1: Determine Next Offset to Read

```cpp
// S1's in-memory state (from startup):
uint64_t last_committed = 9;  // Loaded from consumer-groups/storage-writer/offsets/partition-0.offset

uint64_t next_offset = last_committed + 1;  // 10
```

#### Step 2: Try to Read Message File

```cpp
std::string filename = "queue/partition-0/" + format_offset(10) + ".msg";
// filename = "queue/partition-0/00000000000010.msg"

std::ifstream file(filename);
if (!file.is_open()) {
    // File doesn't exist yet - partition-0 only has up to offset 10
    return std::nullopt;  // No message available
}
```

**Result:** Consumer S1 finds message at offset 10!

#### Step 3: Read Message Content

```cpp
std::string content((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

// content = "{...whatever was in offset 10...}"
```

#### Step 4: Process Message

```cpp
// For "storage-writer" group: write to database/file
process_message(content);  // Application logic (not shown)
```

#### Step 5: Commit Offset

This is where coordination happens!

```cpp
void commit_offset(int partition, uint64_t offset) {
    std::string offset_file = "consumer-groups/storage-writer/offsets/partition-"
                             + std::to_string(partition) + ".offset";

    // Atomic write using rename trick
    std::string tmp = offset_file + ".tmp.s1-2001";  // Unique temp file

    std::ofstream ofs(tmp);
    ofs << offset;  // Write 10
    ofs.close();

    // Atomic rename - CRITICAL for multi-process safety
    rename(tmp.c_str(), offset_file.c_str());
}
```

**Filesystem changes:**
```
Before commit:
consumer-groups/storage-writer/offsets/partition-0.offset → "9"

During commit:
consumer-groups/storage-writer/offsets/partition-0.offset.tmp.s1-2001 → "10" (temp file)

After commit (atomic rename):
consumer-groups/storage-writer/offsets/partition-0.offset → "10"
```

**Why rename is atomic:**
- POSIX guarantees `rename()` is atomic within same filesystem
- Other processes see either old value (9) or new value (10), never partial write
- Crash-safe: if S1 crashes during write, either old or new file exists, never corrupted

---

### T+18ms: Consumer A1 Reads from Partition-2 (Analytics Group)

**Consumer A1** (analytics group, assigned partitions [0, 2])

Remember: Partition-2 now has 2 NEW messages (offsets 9 and 10 from Producer A)

#### Step 1: Determine Next Offset

```cpp
// A1's state (from consumer-groups/analytics/offsets/partition-2.offset):
uint64_t last_committed = 6;

uint64_t next_offset = 7;  // Start reading from offset 7
```

#### Step 2: Read Message at Offset 7

```cpp
std::string filename = "queue/partition-2/00000000000007.msg";
std::ifstream file(filename);
// Reads old message that was written before T+0ms
```

#### Step 3: Process and Commit

```cpp
process_analytics(content);  // Different processing logic than storage-writer!

commit_offset(2, 7);  // Updates consumer-groups/analytics/offsets/partition-2.offset → 7
```

**Key Insight:** Both consumer groups are reading the **same partition**, but:
- Different offsets (storage-writer at 7, analytics at 7)
- Different processing logic
- Independent offset tracking
- No coordination needed between groups!

**Offset state after T+18ms:**
```
storage-writer group:
  partition-0.offset = 10
  partition-2.offset = 7  (unchanged, S2 handles this)

analytics group:
  partition-2.offset = 7  ← A1 just committed this
```

---

### T+25ms: Simultaneous Reads on Same Partition (Race Condition Prevention)

**Scenario:** What if two consumers from the SAME group try to read partition-2?

Let's say Consumer S2 (storage-writer group) is assigned partition-2.

#### Attempt by S1 (NOT assigned partition-2):

```cpp
// S1 tries to read partition-2 (but shouldn't!)
void ConsumerGroupMember::consume_partition(int partition) {
    // GUARD: Verify partition assignment
    if (!is_assigned_to_me(partition)) {
        std::cerr << "Partition " << partition << " not assigned to me!\n";
        return;  // Exit early
    }

    // ... continue with read ...
}

bool is_assigned_to_me(int partition) {
    // Read current assignment
    Assignments assign = read_assignments();  // Reads consumer-groups/storage-writer/assignments.json

    // Check if partition is in my list
    auto& my_partitions = assign.assignments[consumer_id_];  // consumer_id_ = "s1-2001"
    return std::find(my_partitions.begin(), my_partitions.end(), partition) != my_partitions.end();
}
```

**Result:** S1 sees it's NOT assigned partition-2 (it's assigned to S2), so it doesn't read!

#### Correct Read by S2 (assigned partition-2):

```cpp
// S2 checks assignment
Assignments assign = read_assignments();
// assign.assignments["s2-2002"] = [2, 3]  ← partition-2 is here!

// S2 proceeds with read
uint64_t next_offset = 7;  // Reads from consumer-groups/storage-writer/offsets/partition-2.offset
std::string filename = "queue/partition-2/00000000000007.msg";
// ... process and commit ...
```

**Protection mechanism:**
1. Partition assignment stored in `assignments.json` (single source of truth)
2. Each consumer checks assignment before reading
3. Only one consumer per partition per group

---

### T+100ms: Consumer A1 Processes Backlog

**Consumer A1** continues reading partition-2 (analytics group):

```
T+100ms: Read offset 7, commit offset 7  ✓
T+105ms: Read offset 8, commit offset 8  ✓
T+110ms: Read offset 9, commit offset 9  ✓  (message from Producer A at T+0ms!)
T+115ms: Read offset 10, commit offset 10 ✓ (message from Producer A at T+10ms!)
T+120ms: Try offset 11... file doesn't exist (caught up to producer!)
```

**Final state for analytics group, partition-2:**
```
consumer-groups/analytics/offsets/partition-2.offset → 10
```

Now analytics group is caught up on partition-2!

---

### T+5000ms (5 seconds): Heartbeat Updates

Every consumer sends heartbeats every 5 seconds:

```cpp
// Consumer S1 background thread
void heartbeat_loop() {
    while (running_) {
        Heartbeat hb = {
            .consumer_id = "s1-2001",
            .timestamp = "2025-10-25T10:30:05.000Z",  // T+5000ms
            .generation = 5
        };

        // Write heartbeat file
        std::string hb_file = ".coordinator/heartbeats/consumer-s1-2001.heartbeat";
        std::ofstream ofs(hb_file);
        ofs << to_json(hb);
        ofs.close();

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
```

**All consumers update heartbeats:**
```
.coordinator/heartbeats/
├── consumer-s1-2001.heartbeat  (timestamp: T+5000ms)
├── consumer-s2-2002.heartbeat  (timestamp: T+5000ms)
├── consumer-a1-3001.heartbeat  (timestamp: T+5000ms)
└── consumer-a2-3002.heartbeat  (timestamp: T+5000ms)
```

---

## Failure Scenario: Consumer S2 Crashes

### T+8000ms: Consumer S2 (PID 2002) Crashes

```bash
# Consumer S2 crashes (simulated)
kill -9 2002
```

**Immediate effects:**
- S2 stops processing partitions [2, 3]
- S2 stops sending heartbeats
- Queue continues accepting messages (producers unaffected!)

**Last state before crash:**
```
storage-writer/offsets/partition-2.offset = 8  (S2 processed up to offset 8)
storage-writer/offsets/partition-3.offset = 12 (S2 processed up to offset 12)

Queue state:
partition-2/offset.txt = 10  (2 unprocessed messages: offsets 9, 10)
partition-3/offset.txt = 13  (1 unprocessed message: offset 13)
```

---

### T+10000ms: Heartbeat Monitor Detects Lag

**Coordinator checks heartbeats** (runs every 10 seconds):

```cpp
void check_heartbeats() {
    auto now = current_timestamp();  // T+10000ms

    for (auto& member : get_members()) {
        auto hb = read_heartbeat(member);

        auto elapsed = now - hb.timestamp;

        if (elapsed > 30s) {
            // Consumer is dead - trigger rebalance
            trigger_rebalance(member);
        }
    }
}
```

**Heartbeat check results:**
```
Consumer S1: last_heartbeat = T+10000ms → alive (0s elapsed) ✓
Consumer S2: last_heartbeat = T+5000ms  → alive (5s elapsed) ✓ (still within 30s threshold)
Consumer A1: last_heartbeat = T+10000ms → alive ✓
Consumer A2: last_heartbeat = T+10000ms → alive ✓
```

**No rebalance triggered yet** - 30s timeout not reached!

---

### T+15000ms: S1 Continues Processing

Consumer S1 (still alive) continues processing partitions [0, 1]:

```
S1 reads partition-0, offset 11
S1 reads partition-1, offset 16
... continues normally ...
```

**Partitions [2, 3] are NOT being processed** - messages piling up!

---

### T+35000ms: Heartbeat Timeout Reached

**Coordinator detects dead consumer:**

```cpp
void check_heartbeats() {
    auto now = current_timestamp();  // T+35000ms

    auto hb_s2 = read_heartbeat("s2-2002");
    auto elapsed = now - hb_s2.timestamp;  // T+35000ms - T+5000ms = 30000ms

    if (elapsed > 30000ms) {
        std::cout << "Consumer s2-2002 timed out (30s exceeded)\n";
        trigger_rebalance("s2-2002");
    }
}
```

**Rebalance triggered!**

---

### T+35050ms: Rebalancing Protocol Begins

#### Step 1: Coordinator Acquires Lock

```cpp
// First consumer to notice (let's say S1) becomes coordinator
int lock_fd = open("consumer-groups/storage-writer/coordinator.lock", O_RDWR | O_CREAT);
flock(lock_fd, LOCK_EX);  // Blocks other consumers
```

**Lock held:** S1 owns coordinator.lock (exclusive file lock)

#### Step 2: Read Current State

```cpp
// Read members
auto members = list_directory("consumer-groups/storage-writer/members/");
// members = ["s1-2001.json", "s2-2002.json"]

// Read assignments
Assignments current = read_assignments();
// current.assignments = {"s1-2001": [0, 1], "s2-2002": [2, 3]}
// current.generation = 5
```

#### Step 3: Remove Dead Consumer

```cpp
// Remove s2-2002 from members
std::filesystem::remove("consumer-groups/storage-writer/members/s2-2002.json");

// Update member list
members = ["s1-2001"];  // Only S1 remains
```

#### Step 4: Compute New Assignment

```cpp
// Use RoundRobinAssignor
RoundRobinAssignor assignor;
auto new_assignments = assignor.assign(members, 4);  // 1 consumer, 4 partitions

// Result: s1-2001 → [0, 1, 2, 3] (S1 now owns ALL partitions!)
```

#### Step 5: Increment Generation

```cpp
int new_generation = current.generation + 1;  // 5 → 6

std::ofstream gen_file("consumer-groups/storage-writer/generation.txt");
gen_file << new_generation;
gen_file.close();
```

**Generation change: 5 → 6** (this signals all consumers to rejoin!)

#### Step 6: Write New Assignments

```cpp
Assignments new_assign = {
    .generation = 6,
    .timestamp = current_timestamp(),
    .assignments = {
        {"s1-2001", {0, 1, 2, 3}}  // S1 owns all partitions now
    }
};

write_assignments_atomic(new_assign);
```

**Assignment file change:**
```json
Before (generation 5):
{
  "generation": 5,
  "assignments": {
    "s1-2001": [0, 1],
    "s2-2002": [2, 3]
  }
}

After (generation 6):
{
  "generation": 6,
  "assignments": {
    "s1-2001": [0, 1, 2, 3]
  }
}
```

#### Step 7: Release Lock

```cpp
flock(lock_fd, LOCK_UN);
close(lock_fd);
```

**Rebalance complete: ~50-100ms**

---

### T+35100ms: Consumer S1 Detects Rebalance

**S1's main loop detects generation change:**

```cpp
void consume_partition(int partition) {
    while (running_) {
        // Check generation before each read
        int current_gen = read_generation();  // Returns 6

        if (current_gen != generation_) {  // generation_ = 5 (old value)
            std::cout << "Rebalance detected! Generation changed: 5 → 6\n";
            on_rebalance_triggered();
            break;  // Exit consumption loop
        }

        // ... normal consumption ...
    }
}
```

#### S1's Rebalance Handler

```cpp
void on_rebalance_triggered() {
    std::cout << "Stopping consumption and committing offsets...\n";

    // Step 1: Commit current offsets for owned partitions
    for (int partition : assigned_partitions_) {  // [0, 1]
        uint64_t current_offset = get_current_offset(partition);
        commit_offset(partition, current_offset);
    }

    // Step 2: Read new assignments
    Assignments new_assign = read_assignments();
    generation_ = new_assign.generation;  // 6
    assigned_partitions_ = new_assign.assignments["s1-2001"];  // [0, 1, 2, 3]

    std::cout << "New assignment (gen " << generation_ << "): ";
    for (int p : assigned_partitions_) {
        std::cout << "P" << p << " ";
    }
    std::cout << "\n";
    // Output: "New assignment (gen 6): P0 P1 P2 P3"

    // Step 3: For NEW partitions, load committed offsets
    for (int partition : {2, 3}) {  // Newly assigned partitions
        uint64_t committed = load_committed_offset(partition);
        std::cout << "Partition " << partition << " resuming from offset " << committed + 1 << "\n";
    }
}
```

**S1 loads offsets for new partitions:**
```cpp
uint64_t offset_p2 = load_committed_offset(2);  // Reads storage-writer/offsets/partition-2.offset → 8
uint64_t offset_p3 = load_committed_offset(3);  // Reads storage-writer/offsets/partition-3.offset → 12
```

---

### T+35200ms: S1 Resumes with New Partitions

**S1 now processes ALL 4 partitions:**

```cpp
// Spawn threads for newly assigned partitions
std::thread t2([this]() { consume_partition(2); });
std::thread t3([this]() { consume_partition(3); });

// t2 starts reading from partition-2, offset 9 (last committed was 8)
// t3 starts reading from partition-3, offset 13 (last committed was 12)
```

**Messages get processed:**
```
T+35200ms: S1/Thread-2 reads partition-2/offset 9  (message from Producer A at T+0ms!)
T+35205ms: S1/Thread-2 reads partition-2/offset 10 (message from Producer A at T+10ms!)
T+35210ms: S1/Thread-3 reads partition-3/offset 13 (message from Producer B at T+3ms!)
```

**Recovery complete!** S1 has taken over the failed consumer's partitions.

**Total downtime for partitions [2, 3]: ~30 seconds** (from crash at T+8s to recovery at T+35s)

---

## Complete Chronological Timeline

```
T+0ms     Producer A → partition-2 (offset 9)
          [Lock P2 mutex] → [Write file] → [Update offset] → [Release]

T+3ms     Producer B → partition-3 (offset 13) [Parallel!]
          [Lock P3 mutex] → [Write file] → [Update offset] → [Release]

T+10ms    Producer A → partition-2 (offset 10)
          [Lock P2 mutex] → [Write file] → [Update offset] → [Release]

T+15ms    Consumer S1 → reads partition-0 (offset 10)
          [Check generation] → [Read file] → [Process] → [Commit offset atomically]

T+18ms    Consumer A1 → reads partition-2 (offset 7) [Different group!]
          [Check generation] → [Read file] → [Process] → [Commit offset]

T+5000ms  All consumers send heartbeats
          [Write heartbeat files with current timestamp]

T+8000ms  💥 Consumer S2 CRASHES (kill -9)
          Partitions [2, 3] stop being processed

T+10000ms Heartbeat monitor checks liveness
          S2 last seen at T+5000ms (5s ago) → Still within 30s threshold ✓

T+15000ms S1 continues processing [0, 1]
          Partitions [2, 3] accumulating unprocessed messages

T+35000ms Heartbeat monitor detects S2 timeout
          S2 last seen at T+5000ms (30s ago) → TIMEOUT! ❌

T+35050ms Rebalancing begins
          [Acquire coordinator.lock]
          [Remove S2 from members]
          [Reassign partitions: S1 → [0,1,2,3]]
          [Increment generation: 5 → 6]
          [Write new assignments]
          [Release coordinator.lock]

T+35100ms S1 detects generation change
          [Stop consumption]
          [Commit current offsets]
          [Load new assignments]
          [Resume with all 4 partitions]

T+35200ms S1 processes backlog from partitions [2, 3]
          System fully recovered ✅
```

---

## Key Locks and Synchronization Points

### Producer Side Locks

**1. Partition Mutex (In-Memory, Per-Process)**
```cpp
std::vector<std::mutex> partition_mutexes_;  // One per partition

// Lock granularity: Per-partition
// Scope: Single producer process
// Duration: ~2-6ms (during write)
// Contention: Only if same producer tries to write to same partition concurrently
```

**Example:**
```
Producer A has 2 threads writing to same partition:
Thread 1: [Lock P0] → write → [Release]
Thread 2: [Lock P0] → BLOCKS until Thread 1 releases
```

**No contention between producers** - each has its own mutex array!

### Consumer Side Locks

**2. Coordinator Lock (File-Based, Multi-Process)**
```cpp
int fd = flock("consumer-groups/GROUP/coordinator.lock", LOCK_EX);

// Lock granularity: Per consumer group
// Scope: All consumers in the group (across processes!)
// Duration: ~50-100ms (during rebalance)
// Contention: Only during rebalancing (rare)
```

**When acquired:**
- Consumer joins group
- Rebalancing triggered (failure detection)
- Consumer leaves group

**3. Partition Assignment (Implicit Lock)**
```cpp
// No actual lock - enforced by assignment protocol
// Rule: Only one consumer per partition per group

// Enforced by:
Assignments assign = read_assignments();
if (!assign.assignments[my_id].contains(partition)) {
    return;  // Not my partition, don't read
}
```

**4. Offset Commit (Atomic File Rename)**
```cpp
// No lock needed - atomicity via rename()
rename("partition-0.offset.tmp", "partition-0.offset");

// POSIX guarantees:
// - Atomic operation (all-or-nothing)
// - Other processes see old OR new, never partial
```

---

## Filesystem State Diagram

### At T+0ms (Initial State)

```
queue/
├── partition-0/offset.txt        10 ─┐
├── partition-1/offset.txt        15  │ Producer write positions
├── partition-2/offset.txt         8  │
└── partition-3/offset.txt        12 ─┘

consumer-groups/
├── storage-writer/
│   ├── generation.txt             5
│   ├── assignments.json          S1=[0,1], S2=[2,3]
│   └── offsets/
│       ├── partition-0.offset     9 ─┐
│       ├── partition-1.offset    14  │ Consumer read positions
│       ├── partition-2.offset     7  │ (storage-writer group)
│       └── partition-3.offset    11 ─┘
└── analytics/
    └── offsets/
        ├── partition-0.offset     8 ─┐
        ├── partition-1.offset    13  │ Consumer read positions
        ├── partition-2.offset     6  │ (analytics group)
        └── partition-3.offset    10 ─┘
```

### At T+10ms (After Producer Writes)

```
queue/
├── partition-2/
│   ├── 00000000000009.msg   ← NEW (Producer A, T+0ms)
│   ├── 00000000000010.msg   ← NEW (Producer A, T+10ms)
│   └── offset.txt            10 (was 8)
└── partition-3/
    ├── 00000000000013.msg   ← NEW (Producer B, T+3ms)
    └── offset.txt            13 (was 12)
```

### At T+35100ms (After Rebalance)

```
consumer-groups/storage-writer/
├── generation.txt             6  (was 5) ← Rebalance trigger
├── assignments.json          S1=[0,1,2,3]  (was S1=[0,1], S2=[2,3])
├── members/
│   └── s1-2001.json          ← Only S1 remains (S2 removed)
└── offsets/
    ├── partition-2.offset     8  ← S1 will resume from offset 9
    └── partition-3.offset    12  ← S1 will resume from offset 13
```

---

## Common Pitfalls and How They're Prevented

### Pitfall 1: Two Consumers Read Same Partition (Same Group)

**Problem:** Duplicate processing

**Prevention:**
```cpp
// Each consumer checks assignment before reading
Assignments assign = read_assignments();
if (!assign.is_assigned_to(my_id, partition)) {
    throw NotAssignedException();
}
```

**Enforced by:** `assignments.json` (single source of truth)

---

### Pitfall 2: Consumer Commits Offset for Lost Partition

**Problem:** Consumer crashes during rebalance, tries to commit offset for partition it no longer owns

**Prevention:**
```cpp
void commit_offset(int partition, uint64_t offset) {
    // ALWAYS verify ownership before committing
    Assignments current = read_assignments();

    if (!current.is_assigned_to(my_id, partition) ||
        current.generation != my_generation) {
        throw NotOwnerException("Lost partition ownership during rebalance");
    }

    // Safe to commit
    write_offset_file(partition, offset);
}
```

**Enforced by:** Generation number check (fence)

---

### Pitfall 3: Stale Heartbeat Causes Premature Rebalance

**Problem:** Consumer is alive but slow network causes heartbeat delay

**Prevention:**
```cpp
// Tuning parameters
heartbeat.interval.ms = 5000     // Send heartbeat every 5s
session.timeout.ms = 30000       // Declare dead after 30s

// 30s / 5s = 6 missed heartbeats before timeout
// Tolerates transient network issues
```

**Trade-off:** Longer timeout = slower failure detection but fewer false positives

---

### Pitfall 4: Split-Brain During Rebalance

**Problem:** Old consumer (didn't see rebalance) and new consumer both process same partition

**Prevention:**
```cpp
// Consumer checks generation on EVERY read
int current_gen = read_generation();
if (current_gen != my_generation) {
    // Stop immediately, rejoin group
    throw RebalanceException();
}

// This check happens before every message read (< 1ms overhead)
```

**Enforced by:** Frequent generation checks + atomic generation file updates

---

## Performance Characteristics

### Producer Throughput

**Per partition:**
- Bottleneck: `fsync()` latency (~1-5ms on SSD)
- Throughput: ~200-1000 writes/sec per partition

**With 4 partitions (parallel):**
- Aggregate: ~800-4000 writes/sec
- Scalability: Linear with partition count (if producers distributed)

### Consumer Lag

**Catching up from 100 message backlog:**
```
Message processing: ~0.5ms (file read + application logic)
Offset commit: ~1ms (atomic rename)
Total per message: ~1.5ms

100 messages × 1.5ms = 150ms to catch up
```

**Real-world lag sources:**
- Slow downstream processing (database writes, API calls)
- Consumer crashes (30s detection + rebalance)
- Partition skew (hot partitions get more load)

### Rebalance Overhead

**Timeline:**
```
Detection: 30s (heartbeat timeout)
Lock acquisition: ~10ms
Reassignment computation: ~5ms
File writes: ~20ms
Consumer rejoin: ~50ms
Total: ~30.1s (dominated by detection timeout)
```

**Optimization:** Shorter `session.timeout.ms` reduces detection time but increases false positives

---

## Comparison: Single-Process vs Multi-Process

| Aspect | Single Process (Phase 9) | Multi-Process (Phase 10) |
|--------|-------------------------|--------------------------|
| **Coordination** | Thread mutexes (in-memory) | File locks + assignments |
| **Partition assignment** | Static (1 thread per partition) | Dynamic (rebalancing) |
| **Failure detection** | None (process crash = total failure) | Heartbeat + timeout (30s) |
| **Scalability** | Limited to partition count | Horizontal (add more consumers) |
| **Complexity** | Low (~300 LOC) | High (~1200 LOC) |
| **Latency** | Lower (no coordination overhead) | Higher (~50ms rebalance latency) |
| **Fault tolerance** | None | High (automatic failover) |

---

## Summary: What Happens When...

### A Producer Writes a Message

1. Hash client_id to determine partition
2. Acquire partition mutex (in-memory lock)
3. Read current offset from file
4. Write message file with next offset
5. fsync() to ensure durability
6. Update offset file
7. Release mutex
8. Return partition + offset to client

**Time:** ~2-6ms
**Locks:** Partition mutex (process-local)
**Filesystem changes:** 2 files (message file + offset file)

---

### A Consumer Reads a Message

1. Check generation number (rebalance detection)
2. Verify partition assignment
3. Calculate next offset (last_committed + 1)
4. Read message file
5. Process message (application logic)
6. Commit offset (atomic rename)
7. Update in-memory offset tracker

**Time:** ~1-10ms (depends on processing logic)
**Locks:** None (partition exclusivity via assignment)
**Filesystem changes:** 1 file (offset commit)

---

### A Consumer Crashes

1. Consumer stops sending heartbeats
2. Coordinator detects timeout (30s)
3. Coordinator acquires coordinator.lock
4. Remove dead consumer from members
5. Recompute partition assignment
6. Increment generation number
7. Write new assignments
8. Release coordinator.lock
9. Remaining consumers detect generation change
10. Consumers rejoin with new assignments
11. New owners load committed offsets
12. Resume processing from last commit

**Time:** ~30-35s (dominated by timeout)
**Locks:** Coordinator lock (multi-process, file-based)
**Filesystem changes:** 3 files (members/, generation.txt, assignments.json)

---

## Next Steps

**To see this in action:**
1. Implement Phase 9 (single consumer)
2. Add instrumentation (log every lock acquisition, offset update)
3. Run with 2 producers, 1 consumer group
4. Observe file system changes in real-time (`watch -n 0.1 ls -lR queue/`)
5. Implement Phase 10 (multi-process coordination)
6. Test failure scenarios (`kill -9` consumer mid-processing)

**Learning exercises:**
- Trace a message from producer to 2 different consumer groups
- Measure rebalance latency with different timeout values
- Simulate network partition (consumer alive but can't write heartbeat)
- Implement "exactly-once" semantics with transactional offset commits

---

*This deep dive shows that distributed systems aren't magic - they're careful orchestration of locks, files, and coordination protocols. By understanding every lock acquisition and filesystem operation, you can reason about performance, debug issues, and design better systems.*
