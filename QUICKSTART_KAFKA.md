# Phase 11 Quick Start: Kafka Comparison

**5-Minute Guide** to running the file-based vs Kafka comparison benchmark.

---

## Option 1: File-Based Only (No Setup Required)

```bash
cd .worktrees/craft-2-phase-11-kafka

# Start server in file-based mode
./build/metricstream_server 8080 file &

# Start consumer
./build/metricstream_consumer file queue storage-writer 4 &

# Run load test
./build/load_test_persistent 8080 50 100

# Check results
ls -la queue/partition-*/
cat consumer_offsets/storage-writer/partition-0.offset

# Stop services
pkill -f metricstream_server
pkill -f metricstream_consumer
```

**Expected Performance:** ~600-800 RPS, 100% success @ 50 clients

---

## Option 2: Full Comparison (Kafka Required)

### Step 1: Install Kafka

**macOS:**
```bash
brew install kafka
brew services start zookeeper
brew services start kafka
```

**Docker (alternative):**
```bash
docker run -d -p 9092:9092 apache/kafka:latest
```

**Verify Kafka is running:**
```bash
nc -z localhost 9092 && echo "✓ Kafka ready" || echo "✗ Kafka not running"
```

### Step 2: Create Kafka Topic

```bash
# Using Homebrew Kafka
kafka-topics --create \
  --bootstrap-server localhost:9092 \
  --topic metrics \
  --partitions 4 \
  --replication-factor 1

# Verify topic created
kafka-topics --list --bootstrap-server localhost:9092
```

### Step 3: Run Comparison Benchmark

```bash
cd .worktrees/craft-2-phase-11-kafka

# Automated comparison (file-based vs Kafka)
./kafka_comparison_benchmark.sh

# Results in: kafka_comparison_results.txt
```

**Expected Results:**
- File-based: ~800 RPS max
- Kafka: ~100,000 RPS (125x faster!)

### Step 4: Manual Testing (Optional)

**Test File-Based Mode:**
```bash
# Terminal 1: Start server
./build/metricstream_server 8080 file

# Terminal 2: Start consumer
./build/metricstream_consumer file queue storage-writer 4

# Terminal 3: Send test request
curl -X POST http://localhost:8080/metrics \
  -H "Authorization: client_abc" \
  -d '{"metrics":[{"name":"cpu","value":75.5,"type":"gauge"}]}'

# Check queue directory
ls -la queue/partition-*/
cat queue/partition-2/00000000000001.msg
```

**Test Kafka Mode:**
```bash
# Terminal 1: Start server in Kafka mode
./build/metricstream_server 8080 kafka localhost:9092 metrics

# Terminal 2: Start Kafka consumer
./build/metricstream_consumer kafka localhost:9092 metrics consumer-group-1

# Terminal 3: Send test request (same as above)
curl -X POST http://localhost:8080/metrics \
  -H "Authorization: client_abc" \
  -d '{"metrics":[{"name":"cpu","value":75.5,"type":"gauge"}]}'

# Monitor Kafka directly
kafka-console-consumer --bootstrap-server localhost:9092 \
  --topic metrics --from-beginning
```

---

## Troubleshooting

### Kafka Connection Refused

```bash
# Check if Kafka is running
brew services list | grep kafka

# Restart if needed
brew services restart kafka

# Wait a few seconds
sleep 5

# Test connection
nc -z localhost 9092
```

### Topic Already Exists Error

```bash
# Delete and recreate topic
kafka-topics --delete --bootstrap-server localhost:9092 --topic metrics
sleep 2
kafka-topics --create --bootstrap-server localhost:9092 \
  --topic metrics --partitions 4 --replication-factor 1
```

### Consumer Not Receiving Messages

```bash
# Check consumer group status
kafka-consumer-groups --bootstrap-server localhost:9092 \
  --group consumer-group-1 --describe

# Reset offsets to beginning
kafka-consumer-groups --bootstrap-server localhost:9092 \
  --group consumer-group-1 --topic metrics \
  --reset-offsets --to-earliest --execute
```

### Server Won't Start

```bash
# Kill any existing servers
pkill -f metricstream_server

# Check port availability
lsof -i :8080

# Try different port
./build/metricstream_server 8081 file
```

---

## Viewing Results

### Open Interactive Visualization

```bash
# Open in browser
open kafka_vs_file_visualization.html
```

This shows:
- Architecture diagrams (file-based vs Kafka)
- Code examples with syntax highlighting
- Performance comparison tables
- When to use each approach

### Analyze Benchmark Results

```bash
# View CSV results
cat kafka_comparison_results.txt

# Format as table
column -t -s',' kafka_comparison_results.txt

# Example output:
# Mode        Clients  Requests  Success_Rate  RPS       Total_Time
# file-based  20       100       95%           600       3.3s
# file-based  50       100       88%           750       6.7s
# kafka       20       100       100%          18500     0.1s
# kafka       50       100       100%          42000     0.1s
```

---

## Next Steps

1. **Explore Consumer Groups:**
   - Start 2 Kafka consumers with same group ID
   - Watch partition rebalancing
   - Kill one consumer, observe automatic reassignment

2. **Test Failure Scenarios:**
   - Kill consumer mid-processing
   - Restart and verify offset recovery
   - Compare file-based vs Kafka recovery time

3. **Performance Tuning:**
   - Increase partition count (8, 16, 32)
   - Test different message sizes (1KB, 10KB, 100KB)
   - Enable Kafka compression (LZ4, Snappy)

4. **Read Documentation:**
   - [PHASE_11_KAFKA_INTEGRATION.md](PHASE_11_KAFKA_INTEGRATION.md) - Complete guide
   - [kafka_vs_file_visualization.html](kafka_vs_file_visualization.html) - Interactive diagrams

---

## Clean Up

```bash
# Stop all services
pkill -f metricstream_server
pkill -f metricstream_consumer
brew services stop kafka
brew services stop zookeeper

# Remove data
rm -rf queue/ consumer_offsets/ *.log

# Remove Kafka data (optional)
rm -rf /opt/homebrew/var/lib/kafka-logs/*
```

---

**You're now ready to compare file-based and Kafka message queues!** 🚀

Start with file-based mode to understand the concepts, then migrate to Kafka to see production optimizations in action.
