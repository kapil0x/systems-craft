# File-Based Queue: Consumer State Storage

## Directory Structure

```
/data/message-queue/
├── partitions/                    # Message data
│   ├── partition-0/
│   │   ├── 00000000000000000000.log    # Messages (offset 0-999)
│   │   ├── 00000000000000001000.log    # Messages (offset 1000-1999)
│   │   └── 00000000000000001000.index  # Sparse index for fast seeking
│   ├── partition-1/
│   └── partition-2/
│
├── consumer-groups/               # Consumer coordination state
│   ├── analytics-workers/
│   │   ├── group.meta             # Group metadata (creation time, etc.)
│   │   ├── assignments.lock       # Partition assignments (CRITICAL)
│   │   └── offsets/
│   │       ├── partition-0.offset # Last committed offset for P0
│   │       ├── partition-1.offset # Last committed offset for P1
│   │       └── partition-2.offset # Last committed offset for P2
│   └── alerting-consumers/
│       ├── group.meta
│       ├── assignments.lock
│       └── offsets/
│
└── .coordinator/                  # Process-level coordination
    ├── leader.lock                # Leader election (single writer)
    └── consumers/
        ├── process-a-1234.heartbeat   # TTL-based liveness
        └── process-b-5678.heartbeat   # TTL-based liveness
```

## Key Files Explained

### 1. `assignments.lock` - Partition Ownership

**Format (JSON for simplicity):**
```json
{
  "version": 5,
  "generation": 3,
  "assignments": {
    "process-a-1234": [0, 2, 4],
    "process-b-5678": [1, 3, 5]
  },
  "timestamp": "2025-10-25T10:30:00Z"
}
```

**Access pattern:**
```cpp
// Process A wants to join consumer group "analytics-workers"
int fd = open("consumer-groups/analytics-workers/assignments.lock", O_RDWR);
flock(fd, LOCK_EX);  // Exclusive lock - blocks other processes

// Read current assignments
Assignments current = read_assignments(fd);

// Rebalance: add myself, redistribute partitions
current.add_consumer("process-a-1234");
current.rebalance();  // Round-robin or range assignment

// Write back atomically
write_assignments_atomic(fd, current);

flock(fd, LOCK_UN);  // Release lock
close(fd);
```

**Why this works:**
- `flock()` provides **process-level mutual exclusion** via the kernel
- Only one process can hold LOCK_EX at a time
- Survives process crashes (kernel releases lock automatically)

### 2. `partition-X.offset` - Committed Offsets

**Format (simple text file):**
```
12450
```

**Or structured (if you need metadata):**
```json
{
  "offset": 12450,
  "timestamp": "2025-10-25T10:29:58Z",
  "consumer_id": "process-a-1234"
}
```

**Access pattern:**
```cpp
// Consumer commits offset after processing batch
void commit_offset(string group, int partition, long offset) {
    string path = "consumer-groups/" + group + "/offsets/partition-"
                  + to_string(partition) + ".offset";

    // Atomic write using rename trick
    string tmp = path + ".tmp";
    write_file(tmp, to_string(offset));
    rename(tmp, path);  // Atomic on POSIX systems
}

// Consumer reads offset on startup
long get_committed_offset(string group, int partition) {
    string path = "consumer-groups/" + group + "/offsets/partition-"
                  + to_string(partition) + ".offset";

    if (!exists(path)) return 0;  // Start from beginning
    return stol(read_file(path));
}
```

**Why rename is atomic:**
- POSIX guarantees `rename()` is atomic within same filesystem
- No partial writes visible to other processes
- Crash-safe: either old or new file, never corrupted

### 3. `process-X.heartbeat` - Liveness Detection

**Format:**
```json
{
  "process_id": "process-a-1234",
  "pid": 1234,
  "hostname": "worker-node-1",
  "last_heartbeat": "2025-10-25T10:30:15Z"
}
```

**How it works:**
```cpp
// Each consumer process runs background thread
void heartbeat_loop() {
    while (running) {
        string path = ".coordinator/consumers/" + process_id + ".heartbeat";
        write_file(path, current_timestamp());
        sleep(5);  // Every 5 seconds
    }
}

// Coordinator checks for dead consumers
void rebalance_if_needed() {
    auto consumers = list_heartbeat_files();
    auto now = current_time();

    for (auto& consumer : consumers) {
        auto last_hb = read_heartbeat(consumer);
        if (now - last_hb > 30s) {
            // Consumer is dead - trigger rebalance
            remove_consumer(consumer);
            reassign_partitions();
        }
    }
}
```

## Comparison: File-Based vs ZooKeeper

| Feature | File-Based | ZooKeeper |
|---------|-----------|-----------|
| **Partition locks** | `assignments.lock` + flock() | `/consumers/group/ids/process-a` ephemeral node |
| **Offset storage** | `partition-X.offset` files | `/consumers/group/offsets/partition-X` znode |
| **Liveness** | Heartbeat files + timestamp check | Ephemeral nodes (auto-deleted on disconnect) |
| **Leader election** | `leader.lock` + flock() | `/controller` ephemeral node with lowest sequence |
| **Multi-machine** | ❌ Single machine only | ✅ Distributed consensus |
| **Complexity** | Low (just file I/O) | High (network, Raft/Zab protocol) |

## Implementation Challenges

### Challenge 1: Atomic Rebalance
**Problem:** Between reading assignments and writing new ones, another process might join.

**Solution:** Version numbers + compare-and-swap
```cpp
Assignments current = read_assignments(fd);
int version = current.version;

// ... rebalance logic ...

if (!cas_write_assignments(fd, version, new_assignments)) {
    // Version changed - retry
    goto retry;
}
```

### Challenge 2: Stale Locks
**Problem:** Process crashes while holding flock() - who releases it?

**Solution:** Kernel releases flock() automatically on process death! (This is the magic)
```cpp
// Process A crashes while holding lock
flock(fd, LOCK_EX);  // Acquired
// ... CRASH ...

// Kernel automatically releases the lock
// Process B can now acquire it
```

### Challenge 3: Split-Brain
**Problem:** Two processes think they own the same partition.

**Solution 1 (File-based):** Strict assignment protocol
```cpp
// Before consuming, ALWAYS verify you still own the partition
bool verify_ownership(int partition) {
    Assignments current = read_assignments();  // May block on flock
    return current.owner_of(partition) == my_process_id;
}

// In consumer loop
while (true) {
    if (!verify_ownership(my_partition)) {
        stop_consuming();  // Lost partition in rebalance
        break;
    }
    Message msg = fetch_next();
    process(msg);
}
```

**Solution 2 (ZooKeeper):** Watch for assignment changes
```cpp
zk.watch("/consumers/analytics-workers/assignments", [this](Event e) {
    if (e.type == CHANGED) {
        recheck_my_assignments();
    }
});
```

## Code Sketch

```cpp
class FileBasedConsumerCoordinator {
    string base_path;
    string group_name;
    string process_id;

public:
    // Join consumer group and get assigned partitions
    vector<int> join_group() {
        string lock_path = base_path + "/consumer-groups/" + group_name
                          + "/assignments.lock";

        int fd = open(lock_path.c_str(), O_RDWR | O_CREAT, 0644);
        flock(fd, LOCK_EX);  // Block until acquired

        Assignments current = read_assignments(fd);
        current.add_consumer(process_id);
        current.rebalance();  // Redistribute partitions

        write_assignments_atomic(fd, current);
        vector<int> my_partitions = current.get_partitions(process_id);

        flock(fd, LOCK_UN);
        close(fd);

        return my_partitions;
    }

    // Commit offset for a partition
    void commit_offset(int partition, long offset) {
        string path = base_path + "/consumer-groups/" + group_name
                     + "/offsets/partition-" + to_string(partition) + ".offset";

        string tmp = path + ".tmp." + process_id;
        ofstream ofs(tmp);
        ofs << offset;
        ofs.close();

        rename(tmp.c_str(), path.c_str());  // Atomic
    }

    // Get last committed offset
    long get_offset(int partition) {
        string path = base_path + "/consumer-groups/" + group_name
                     + "/offsets/partition-" + to_string(partition) + ".offset";

        ifstream ifs(path);
        if (!ifs) return 0;

        long offset;
        ifs >> offset;
        return offset;
    }
};
```

## When to Use What

**Use file-based coordination when:**
- ✅ Single machine deployment
- ✅ Simple operational requirements
- ✅ Learning/prototyping
- ✅ Low latency requirements (no network)

**Use ZooKeeper/etcd when:**
- ✅ Multi-machine deployment
- ✅ High availability requirements
- ✅ Production systems at scale
- ✅ Already have ZooKeeper infrastructure

For **Craft #2**, I'd recommend starting with file-based (learn the concepts), then optionally adding ZooKeeper integration as an advanced phase.

Want me to implement a working prototype of this file-based coordinator?
