# Craft #2, Phase 3: Distributed Coordination (Design & Simulation)

**Status:** 📝 Design Phase (ready for future implementation)  
**Time to Implement:** 6-8 hours  
**Prerequisites:** Complete Phase 1 & 2

---

## Overview

Phase 3 introduces **true distributed messaging**: multiple Kafka brokers, consumer group rebalancing, and fault tolerance across machines.

**Learning Goal:** Understand why distributed systems are hard - coordination, consensus, failure modes.

**What you'll build:**
- Multi-broker Kafka cluster
- Consumer groups that rebalance on broker failure
- Measured throughput under realistic network conditions
- Observe partition leadership election

---

## Architecture Evolution

### Phase 2 (Single Broker)
```
Producer → [Single Kafka Broker] → Consumer Group (single machine)
           (9092)
```
- ❌ No replication
- ❌ No failover
- ❌ Localhost latency hides network effects

### Phase 3 (Distributed)
```
Producer → [Kafka Cluster]     → Consumer Group (distributed)
           ├─ Broker 1 (leader)
           ├─ Broker 2 (replica)
           └─ Broker 3 (replica)
                 ↓
           [ZooKeeper/KRaft] (coordination)
           
Partition 0: Leader=Broker1, ISR=[1,2,3]
Partition 1: Leader=Broker2, ISR=[2,3,1]
Partition 2: Leader=Broker3, ISR=[3,1,2]
```

**Benefits demonstrated:**
- ✅ Replication (survivable broker failures)
- ✅ Automatic failover (ISR election)
- ✅ Consumer rebalancing (parallel processing)
- ✅ Realistic throughput (batching, leader balancing)

---

## Implementation Options

### Option A: Docker Compose (Local)
**Cost:** Free  
**Setup Time:** 15 minutes  
**Resources:** 4GB RAM (doable on M3)  
**Realism:** 70% (localhost networking masks latency, but architecture is correct)

```yaml
# docker-compose.yml
version: '3.8'
services:
  zookeeper:
    image: confluentinc/cp-zookeeper:7.5.0
    ports:
      - "2181:2181"
    environment:
      ZOOKEEPER_CLIENT_PORT: 2181
      ZOOKEEPER_TICK_TIME: 2000

  kafka-broker-1:
    image: confluentinc/cp-kafka:7.5.0
    depends_on:
      - zookeeper
    ports:
      - "9092:9092"
    environment:
      KAFKA_BROKER_ID: 1
      KAFKA_ZOOKEEPER_CONNECT: zookeeper:2181
      KAFKA_ADVERTISED_LISTENERS: PLAINTEXT://kafka-broker-1:29092,PLAINTEXT_HOST://localhost:9092
      KAFKA_LISTENER_SECURITY_PROTOCOL_MAP: PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT
      KAFKA_INTER_BROKER_LISTENER_NAME: PLAINTEXT
      
  kafka-broker-2:
    image: confluentinc/cp-kafka:7.5.0
    depends_on:
      - zookeeper
    ports:
      - "9093:9093"
    environment:
      KAFKA_BROKER_ID: 2
      KAFKA_ZOOKEEPER_CONNECT: zookeeper:2181
      KAFKA_ADVERTISED_LISTENERS: PLAINTEXT://kafka-broker-2:29093,PLAINTEXT_HOST://localhost:9093
      KAFKA_LISTENER_SECURITY_PROTOCOL_MAP: PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT
      KAFKA_INTER_BROKER_LISTENER_NAME: PLAINTEXT

  kafka-broker-3:
    image: confluentinc/cp-kafka:7.5.0
    depends_on:
      - zookeeper
    ports:
      - "9094:9094"
    environment:
      KAFKA_BROKER_ID: 3
      KAFKA_ZOOKEEPER_CONNECT: zookeeper:2181
      KAFKA_ADVERTISED_LISTENERS: PLAINTEXT://kafka-broker-3:29094,PLAINTEXT_HOST://localhost:9094
      KAFKA_LISTENER_SECURITY_PROTOCOL_MAP: PLAINTEXT:PLAINTEXT,PLAINTEXT_HOST:PLAINTEXT
      KAFKA_INTER_BROKER_LISTENER_NAME: PLAINTEXT
```

**Start cluster:**
```bash
docker-compose up -d
```

**Verify:**
```bash
# List brokers
docker exec kafka-broker-1 kafka-broker-api-versions --bootstrap-server localhost:9092

# Create topic with replication
docker exec kafka-broker-1 kafka-topics \
  --create \
  --bootstrap-server localhost:9092 \
  --topic metrics \
  --partitions 6 \
  --replication-factor 3
```

**Pros:**
- Free
- Works immediately
- Can simulate failures (docker stop)

**Cons:**
- Localhost networking (no real latency)
- Container overhead affects measurements

---

### Option B: AWS EC2 (Realistic)
**Cost:** ~$20-30/month (3 t3.small instances)  
**Setup Time:** 45 minutes  
**Realism:** 95% (real distributed system, real network latency)

**Architecture:**
- 3 EC2 instances (different AZs for fault tolerance)
- Kafka brokers + ZooKeeper
- Load test from fourth instance
- Measure latency between instances

**Advantages:**
- Real network effects (~1-5ms latency between brokers)
- Survivable broker failures
- Consumer rebalancing on different machines
- Measured throughput reflects production-like scaling

**Disadvantages:**
- Costs money
- Need AWS account setup

---

### Option C: Confluent Cloud (Managed)
**Cost:** Free tier (2GB, limited throughput)  
**Setup Time:** 10 minutes  
**Realism:** 90% (production infrastructure, but shared)

**No infrastructure to manage - just API credentials.**

**Advantages:**
- Production-grade infrastructure
- No ops overhead
- Real cluster management lessons

**Disadvantages:**
- Limited free tier
- Less control for learning

---

## Phase 3 Learning Plan (Docker + Simulation)

### Part 1: Multi-Broker Setup
```bash
# 1. Start cluster
docker-compose up -d

# 2. Create replicated topic
docker exec kafka-broker-1 kafka-topics \
  --create \
  --bootstrap-server localhost:9092 \
  --topic metrics \
  --partitions 6 \
  --replication-factor 3

# 3. Verify replication
docker exec kafka-broker-1 kafka-topics \
  --describe \
  --bootstrap-server localhost:9092 \
  --topic metrics
```

**Expected output:**
```
Topic: metrics
  Partition: 0    Leader: 2   Replicas: [2,1,3]   Isr: [2,1,3]
  Partition: 1    Leader: 3   Replicas: [3,2,1]   Isr: [3,2,1]
  ...
```

### Part 2: Consumer Group Rebalancing
```bash
# Consumer 1 (gets partitions [0,2,4])
./build/metricstream_consumer kafka localhost:9092,localhost:9093,localhost:9094 metrics group-1

# Consumer 2 (rebalance: gets [1,3,5])
./build/metricstream_consumer kafka localhost:9092,localhost:9093,localhost:9094 metrics group-1

# Observe logs:
# "Rebalancing: [Partition 0,2,4] → [Partition 1,3,5]"
```

### Part 3: Failure & Recovery (Simulation)
```bash
# Kill broker 2
docker stop kafka-broker-2

# Observe:
# - Producer keeps working (writes to brokers 1, 3)
# - Brokers 1,3 elect new leaders
# - ISR shrinks: [2,1,3] → [1,3]
# - ZooKeeper triggers rebalancing

# Consumer group rebalances:
# - Consumer 1,2 pause briefly
# - New partition assignments

# Restart broker 2
docker start kafka-broker-2

# Observe:
# - Broker 2 catches up (replicas)
# - ISR expands: [1,3] → [1,3,2]
# - Cluster returns to normal
```

### Part 4: Throughput Measurement
**With replication (3 copies):**
- Producer latency: ~5-10ms (wait for ISR acks)
- Throughput: Scales with partitions (6 partitions = potential 6x parallel processing)
- Success rate: 99%+ (failures handled automatically)

**Measured vs. Phase 2:**
```
                  | Phase 2 (Single)  | Phase 3 (Distributed)
------------------|-------------------|---------------------
Fault Tolerance   | None              | Automatic failover
Parallel Procs    | 1                 | N (partitions)
Replication       | None              | 3x (3 brokers)
Latency Impact    | +0ms              | +5-10ms (acks)
Throughput        | Single broker     | Scales with replication
```

---

## Code Changes Needed

### Producer (Optional Enhancement)
```cpp
// Add replication/acks configuration
RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

// Request all ISR replicas to acknowledge
conf->set("acks", "all", errstr);  // -1 = wait for all ISR

// Add retries for broker failures
conf->set("retries", "10000", errstr);
conf->set("retry.backoff.ms", "100", errstr);
```

### Consumer (Optional Enhancement)
```cpp
// Consumer group coordination
RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

// Join consumer group
conf->set("group.id", "metrics-group-1", errstr);

// Handle rebalancing
conf->set("partition.assignment.strategy", "roundrobin", errstr);
```

---

## Simulation Scenarios

### Scenario 1: Broker Failure
```
Timeline:
t=0:     All 3 brokers healthy
t=10:    Kill broker 2
t=11:    Producer detects failure, sends to brokers 1,3
t=12:    ZooKeeper triggers leader election for partitions with leader=2
t=13:    New leaders elected from ISR
t=14:    Consumer group rebalances
t=20:    Restart broker 2
t=21:    Broker 2 replicas catch up
t=25:    Cluster back to normal state
```

### Scenario 2: Cascading Failure
```
t=0:     All 3 brokers healthy
t=10:    Kill broker 1 (random)
t=15:    Kill broker 3 (while recovering)
t=16:    Broker 2 is the only leader - SINGLE POINT OF FAILURE
t=20:    Restart broker 1
t=25:    Cluster recovers
```

**Learning:** Why Kafka uses odd replication factors (3, not 2)

### Scenario 3: Consumer Group Rebalancing
```
t=0:     Consumer A starts, assigned [P0,P1,P2]
t=5:     Consumer B joins
         → Rebalance triggered
         → Assignment: A=[P0,P2], B=[P1]
t=6:     Consumer B leaves
         → Rebalance triggered
         → Assignment: A=[P0,P1,P2]
```

---

## Measurements to Capture

```
Metric                           | Single Broker | 3 Brokers | Learning
---------------------------------|---------------|-----------|-----------------------------
Producer latency (acks=1)        | 0.15ms        | 0.5ms     | Replication cost
Producer latency (acks=all)      | 2-5ms         | 10-15ms   | Durability cost
Throughput (1 partition)         | 10K RPS       | 10K RPS   | Single partition bound
Throughput (6 partitions)        | 10K RPS       | 60K RPS   | Parallelism scales
Message loss (broker dies)       | ALL lost      | 0 (acks=all) | Why replication matters
Rebalance time                   | N/A           | 1-3s      | Coordination overhead
Recovery time (broker restart)   | N/A           | 30-60s    | Catch-up time
```

---

## What Phase 3 Teaches

1. **Consensus is hard**
   - 3 brokers + ZooKeeper + elections = complex coordination
   - Failures cascade without proper replication
   - Why Raft/PBFT were invented

2. **Throughput scaling requires parallelism**
   - Single partition = single broker bottleneck
   - Partitions distribute across brokers
   - Consumer groups parallelize reading

3. **Durability has cost**
   - `acks=all` = wait for all replicas = latency
   - Trade-off between speed and safety

4. **Observing failures teaches system design**
   - When broker dies, what happens?
   - How does ZooKeeper detect failure?
   - Why did consumer group pause?

---

## Future: Implementation Path (When Ready)

1. **Docker setup** (15 min)
   - Start cluster
   - Verify 3-broker topology

2. **Producer enhancement** (30 min)
   - Add `acks=all` configuration
   - Measure latency impact

3. **Consumer enhancement** (30 min)
   - Join consumer group
   - Log rebalancing events

4. **Chaos testing** (1-2 hours)
   - Kill brokers systematically
   - Measure recovery time
   - Document failure modes

5. **Throughput benchmark** (1 hour)
   - Run with 6 partitions
   - Measure scaling with partition count
   - Compare to Phase 2

---

## Resources

- [Kafka Replication Protocol](https://kafka.apache.org/documentation/#replication)
- [ZooKeeper Coordination](https://zookeeper.apache.org/doc/current/index.html)
- [Docker Compose Kafka](https://github.com/confluentinc/cp-docker-images)
- [Kafka Failure Modes](https://kafka.apache.org/documentation/#bestpractices)

---

**This is the design for Phase 3: Distributed Coordination.** When ready to implement, start with Docker Compose, measure, then optionally scale to AWS for production-like testing.
