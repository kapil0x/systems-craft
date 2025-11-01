# End-to-End User Metric Flow

**From User Application → MetricStream → Storage**

This document shows exactly how a single metric travels through our system, from the moment a user sends it until it's stored and queryable.

---

## Overview: The Journey of One Metric

```
User App → HTTP Request → Event Loop → Thread Pool → Validation → Kafka → Storage → Query
   1           2              3            4             5          6        7        8
```

**Total Time: ~5-10ms** (Phase 8 with Kafka integration)

---

## Detailed Flow

### 1. User Application Sends Metric

**User's Code (Python example):**
```python
import requests
import time

# User wants to track their application's CPU usage
metric = {
    "timestamp": "2025-10-12T15:30:00Z",
    "name": "app.cpu.usage",
    "value": 75.5,
    "tags": {
        "host": "web-server-01",
        "region": "us-west-2",
        "service": "api"
    }
}

# Send to MetricStream ingestion endpoint
response = requests.post(
    "http://metricstream.company.com/metrics",
    json={"metrics": [metric]},
    headers={"Authorization": "Bearer user-api-key-123"}
)

print(response.json())
# {"success": true, "metrics_processed": 1}
```

**What happens:** HTTP POST with JSON payload containing the metric data.

---

### 2. HTTP Request Arrives at Server

**Network Layer:**
```
TCP Connection established (if not using keep-alive)
↓
HTTP Request:
POST /metrics HTTP/1.1
Host: metricstream.company.com
Content-Type: application/json
Authorization: Bearer user-api-key-123
Content-Length: 234

{"metrics":[{"timestamp":"2025-10-12T15:30:00Z","name":"app.cpu.usage",...}]}
```

**Server Socket:** Listening on port 8080 (or 443 with TLS in production)

---

### 3. Event Loop Receives Request (Phase 8)

**Event Loop (src/event_loop.cpp):**
```cpp
// epoll_wait() detects incoming data
struct epoll_event events[1024];
int nfds = epoll_wait(epoll_fd_, events, 1024, 100);

for (int i = 0; i < nfds; i++) {
    if (events[i].events & EPOLLIN) {
        int client_fd = events[i].data.fd;

        // Non-blocking read
        char buffer[4096];
        ssize_t bytes = read(client_fd, buffer, sizeof(buffer));

        // Check for complete HTTP request (\r\n\r\n)
        if (request_complete(buffer)) {
            // Delegate to thread pool for CPU work
            thread_pool_->enqueue([buffer, client_fd]() {
                process_request(buffer, client_fd);
            });
        }
    }
}
```

**Time: ~0.1ms** (non-blocking, instant when data ready)

**What happens:**
- Event loop detects data on socket (via epoll)
- Reads HTTP request into buffer (non-blocking)
- Passes to thread pool for processing

---

### 4. Thread Pool Processes Request

**Worker Thread (src/ingestion_service.cpp):**

```cpp
HttpResponse IngestionService::handle_metrics_post(const HttpRequest& request) {
    // Step 4a: Parse JSON
    auto metrics = json_parser_.parse(request.body);
    // Time: ~0.5ms for typical batch

    // Step 4b: Validate auth token
    if (!validate_auth(request.headers["Authorization"])) {
        return error_response(401, "Unauthorized");
    }
    // Time: ~0.1ms (check Redis/cache)

    // Step 4c: Rate limiting check
    std::string client_id = extract_client_id(request);
    if (!rate_limiter_.allow_request(client_id)) {
        return error_response(429, "Rate limit exceeded");
    }
    // Time: ~0.05ms (hash lookup + atomic ops)

    // Step 4d: Validate each metric
    for (const auto& metric : metrics) {
        if (!validate_metric(metric)) {
            return error_response(400, "Invalid metric format");
        }
    }
    // Time: ~0.2ms per batch

    // Step 4e: Enrich metrics with metadata
    for (auto& metric : metrics) {
        metric["ingestion_time"] = current_timestamp();
        metric["client_id"] = client_id;
        metric["datacenter"] = "us-west-2";
    }
    // Time: ~0.1ms

    // Step 4f: Send to Kafka (async)
    kafka_producer_->send_async(metrics);
    // Time: ~0.5ms to enqueue (actual send is async)

    return success_response(metrics.size());
}
```

**Total Processing Time: ~1.5ms**

**What happens:**
1. Parse JSON → Extract metrics array
2. Authenticate → Check API key validity
3. Rate limit → Ensure user hasn't exceeded quota
4. Validate → Check required fields (timestamp, name, value)
5. Enrich → Add server-side metadata
6. Kafka enqueue → Hand off to message queue

---

### 5. Validation Details

**What We Check:**
```cpp
bool validate_metric(const Metric& metric) {
    // Required fields
    if (metric.timestamp.empty()) return false;
    if (metric.name.empty()) return false;
    if (!metric.has_value()) return false;

    // Timestamp format (ISO 8601)
    if (!is_valid_timestamp(metric.timestamp)) return false;

    // Metric name (alphanumeric + dots)
    if (!regex_match(metric.name, "^[a-zA-Z0-9._]+$")) return false;

    // Value must be numeric
    if (!is_numeric(metric.value)) return false;

    // Optional: check value range
    if (metric.value < -1e15 || metric.value > 1e15) return false;

    return true;
}
```

**Common Validation Errors:**
- Missing timestamp → `400 Bad Request`
- Invalid metric name → `400 Bad Request`
- Non-numeric value → `400 Bad Request`
- Rate limit exceeded → `429 Too Many Requests`
- Invalid auth token → `401 Unauthorized`

---

### 6. Kafka Producer Sends to Message Queue

**Kafka Integration (future - not in current code):**

```cpp
class KafkaProducer {
    void send_async(const std::vector<Metric>& metrics) {
        // Batch metrics for efficiency
        std::string batch = serialize_to_json(metrics);

        // Kafka configuration
        std::string topic = "metrics.ingest";
        int partition = hash(metrics[0].client_id) % num_partitions;

        // Async send (returns immediately, callback when done)
        producer_->produce(
            topic,
            partition,
            batch.data(),
            batch.size(),
            nullptr,  // key
            0,        // key len
            [](const Message& msg, DeliveryReport& dr) {
                if (dr.err()) {
                    log_error("Kafka delivery failed: " + dr.err_str());
                    // Retry logic or DLQ (dead letter queue)
                } else {
                    metrics_sent_counter.increment();
                }
            }
        );
    }
};
```

**Kafka Topic Structure:**
```
Topic: metrics.ingest
├─ Partition 0: client_id hash % 10 == 0
├─ Partition 1: client_id hash % 10 == 1
├─ ...
└─ Partition 9: client_id hash % 10 == 9

Benefits:
- Parallel consumption (10 workers can read simultaneously)
- Order preserved per client_id (same partition)
- Load balanced across partitions
```

**Kafka Message Format:**
```json
{
  "timestamp": "2025-10-12T15:30:00Z",
  "name": "app.cpu.usage",
  "value": 75.5,
  "tags": {
    "host": "web-server-01",
    "region": "us-west-2",
    "service": "api"
  },
  "metadata": {
    "ingestion_time": "2025-10-12T15:30:00.123Z",
    "client_id": "user-123",
    "datacenter": "us-west-2"
  }
}
```

**Time: ~1-2ms** (async, doesn't block response)

---

### 7. Storage Pipeline (ClickHouse Consumer)

**Kafka Consumer (separate service):**

```python
# Storage Consumer Service
from kafka import KafkaConsumer
import clickhouse_driver

consumer = KafkaConsumer(
    'metrics.ingest',
    bootstrap_servers='kafka:9092',
    group_id='metrics-storage-group',
    auto_offset_reset='earliest'
)

clickhouse = clickhouse_driver.Client('clickhouse:9000')

# Batch insert for efficiency
batch = []
batch_size = 1000
batch_timeout = 1.0  # seconds

for message in consumer:
    metric = json.loads(message.value)
    batch.append(metric)

    if len(batch) >= batch_size or time_since_last_flush > batch_timeout:
        # Bulk insert to ClickHouse
        clickhouse.execute(
            "INSERT INTO metrics (timestamp, name, value, tags) VALUES",
            batch
        )
        batch.clear()
        consumer.commit()
```

**ClickHouse Table Schema:**
```sql
CREATE TABLE metrics (
    timestamp DateTime64(3),
    name String,
    value Float64,
    tags Map(String, String),
    client_id String,
    ingestion_time DateTime64(3)
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (name, timestamp)
SETTINGS index_granularity = 8192;
```

**Benefits:**
- Columnar storage → Fast aggregations
- Partition by month → Old data can be archived
- Sorted by (name, timestamp) → Fast time-series queries

**Time: ~10-50ms** (batched write, happens asynchronously)

---

### 8. User Queries Their Metrics

**Query API (future endpoint):**

```python
# User queries their data
GET /api/query?name=app.cpu.usage&start=2025-10-12T15:00:00Z&end=2025-10-12T16:00:00Z

# Backend executes ClickHouse query
query = """
SELECT
    toStartOfMinute(timestamp) as time_bucket,
    avg(value) as avg_cpu,
    max(value) as max_cpu,
    min(value) as min_cpu
FROM metrics
WHERE
    name = 'app.cpu.usage'
    AND timestamp BETWEEN '2025-10-12 15:00:00' AND '2025-10-12 16:00:00'
    AND client_id = 'user-123'
GROUP BY time_bucket
ORDER BY time_bucket
"""

# Response
{
  "data": [
    {"time": "2025-10-12T15:00:00Z", "avg": 72.3, "max": 85.1, "min": 60.5},
    {"time": "2025-10-12T15:01:00Z", "avg": 75.5, "max": 88.2, "min": 65.3},
    ...
  ]
}
```

**Query Time: ~50-200ms** (depending on time range and aggregation)

---

## Current State vs Future

### ✅ **Current Implementation (Phase 8)**

```
User → HTTP → Event Loop → Thread Pool → Validation → File (metrics.jsonl)
                                                              ↓
                                                    Single file, append-only
```

**Performance:** 145,348 RPS, 100% success rate

**Limitations:**
- Single file (no horizontal scaling)
- No indexing (slow queries)
- No aggregations (must read all data)

---

### 🚀 **Future Implementation (Phase 9+)**

```
User → HTTP → Event Loop → Thread Pool → Validation → Kafka → ClickHouse → Query API
        ↓                                                ↓         ↓
    Keep-Alive                                    Distributed  Columnar
    145K RPS                                      10 partitions Storage
```

**Benefits:**
- **Kafka:** Distributed, fault-tolerant message queue
- **ClickHouse:** Fast time-series queries and aggregations
- **Horizontal scaling:** Add more ingestion servers as needed
- **Durability:** Kafka retains data for replay/recovery

**Target Performance:** 500K+ RPS per datacenter

---

## Error Handling & Retries

### Client-Side Retry Logic

```python
import requests
from requests.adapters import HTTPAdapter
from requests.packages.urllib3.util.retry import Retry

# Configure retry strategy
retry_strategy = Retry(
    total=3,
    status_forcelist=[429, 500, 502, 503, 504],
    backoff_factor=1,  # 1s, 2s, 4s
)

adapter = HTTPAdapter(max_retries=retry_strategy)
session = requests.Session()
session.mount("http://", adapter)

# This will automatically retry on failures
response = session.post(
    "http://metricstream.company.com/metrics",
    json={"metrics": [metric]}
)
```

### Server-Side Resilience

**Kafka Producer Failures:**
```cpp
// If Kafka is down, metrics go to:
1. In-memory buffer (last 10K metrics)
2. Local disk failover (metrics_failover.jsonl)
3. Dead letter queue (DLQ) for later replay
```

**ClickHouse Failures:**
```python
# Consumer handles failures:
1. Retry 3 times with exponential backoff
2. Send to DLQ topic: metrics.ingest.dlq
3. Alert operations team
4. Metrics NOT lost (still in Kafka)
```

---

## Monitoring & Observability

**Metrics We Track:**
```
metricstream.requests.total         Counter   Total requests received
metricstream.requests.success       Counter   Successful requests
metricstream.requests.failed        Counter   Failed requests (by error code)
metricstream.latency.p50            Histogram Request latency (50th percentile)
metricstream.latency.p99            Histogram Request latency (99th percentile)
metricstream.kafka.send.success     Counter   Successful Kafka sends
metricstream.kafka.send.failed      Counter   Failed Kafka sends
metricstream.rate_limit.exceeded    Counter   Rate limit rejections
```

**Alerts:**
```
- Error rate > 1% → Page ops team
- p99 latency > 100ms → Page ops team
- Kafka lag > 1 million → Warning (catch up)
- ClickHouse insert failures > 0 → Critical
```

---

## Summary

**End-to-End Flow:**

1. **User sends metric** (HTTP POST) → ~1ms network
2. **Event loop receives** (epoll) → ~0.1ms
3. **Thread pool processes** (parse, validate) → ~1.5ms
4. **Kafka enqueue** (async) → ~0.5ms
5. **HTTP response** → ~1ms network
6. **Kafka → ClickHouse** (background) → ~50ms
7. **User queries data** → ~100ms

**Total user-facing latency:** ~5ms (steps 1-5)

**End-to-end latency:** ~60ms (until queryable)

**Current throughput:** 145,348 RPS (Phase 8, single server)

**Future throughput:** 500K+ RPS (with Kafka + horizontal scaling)

---

## Try It Yourself

**Current System (Phase 8):**
```bash
# Start server
./build/metricstream_server

# Send a metric
curl -X POST http://localhost:8080/metrics \
  -H "Content-Type: application/json" \
  -d '{
    "metrics": [{
      "timestamp": "2025-10-12T15:30:00Z",
      "name": "test.metric",
      "value": 42.5
    }]
  }'

# Check the file
tail -f metrics.jsonl
```

**Future System (Phase 9):**
```bash
# Will include:
# - Kafka setup
# - ClickHouse setup
# - Query API
# - Web dashboard
```

---

**Next Steps:** Want to see this visualized? Check out `docs/phase8_architecture_visual.html` for interactive diagrams!
