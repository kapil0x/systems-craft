# Docker Quickstart - Systems Craft

Get the monitoring platform running in **30 seconds**. No C++ compiler, no CMake, no dependencies.

## 🚀 Fastest Start (Using Docker Compose)

```bash
# 1. Clone the repo
git clone https://github.com/kapil0x/systems-craft.git
cd systems-craft

# 2. Start the server
docker-compose up

# Done! Server running on http://localhost:8080
```

## 🧪 Test It Works

Open a new terminal:

```bash
# Send a test metric
curl -X POST http://localhost:8080/metrics \
  -H "Content-Type: application/json" \
  -d '{"timestamp":"2025-01-01T12:00:00Z","name":"cpu_usage","value":75.5,"client_id":"test_client"}'

# Query metrics
curl http://localhost:8080/metrics

# Response: Your metric is stored!
```

## 📊 Run Benchmark Tool

Now open the benchmark tool in your browser:

1. Go to `website/benchmark.html` in your browser
2. Server URL: `http://localhost:8080`
3. Click **"Start Benchmark"**
4. See real performance metrics!

**Example Results:**
```
Throughput: 45-60 RPS
Avg Latency: 2-3ms
Success Rate: 100%
```

This is your **baseline**. Now you know what to optimize!

## 🛠️ Alternative: Build Without Docker Compose

```bash
# Build the image
docker build -t systems-craft:latest .

# Run the container
docker run -p 8080:8080 systems-craft:latest

# Server starts immediately
```

## 🔍 Inside the Container

```bash
# Access the running container
docker exec -it systems-craft-poc bash

# Check metrics file
cat metrics.jsonl

# Run the benchmark inside container
./simple_benchmark localhost 8080 50 10
```

## 📦 What's Included

The Docker image contains:
- ✅ Complete Phase 0 PoC (all 5 components)
- ✅ HTTP ingestion server (port 8080)
- ✅ In-memory message queue
- ✅ File-based storage (metrics.jsonl)
- ✅ Query API endpoint
- ✅ Simple alerting engine
- ✅ Benchmark tool (simple_benchmark)

## 🎓 Next Steps

### Option A: Optimize the PoC (Craft #1)
The baseline performance is ~50-60 RPS. Can you get it to 2,000 RPS?

1. Read [craft1.html](website/craft1.html) for the optimization guide
2. Implement each phase (threading, async I/O, etc.)
3. Re-benchmark to measure improvement
4. Track progress on your dashboard

### Option B: Build Message Queue (Craft #2)
Learn how distributed systems handle message buffering.

1. Read [craft2.html](website/craft2.html)
2. Implement file-based queue with partitioning
3. Compare with Kafka integration
4. Benchmark latency improvements

## 🔧 Troubleshooting

### Port Already in Use
```bash
# Check what's using port 8080
lsof -i :8080

# Use a different port
docker run -p 8081:8080 systems-craft:latest
# Now access at http://localhost:8081
```

### Build Fails
```bash
# Clean build
docker-compose down
docker-compose build --no-cache
docker-compose up
```

### Container Exits Immediately
```bash
# Check logs
docker logs systems-craft-poc

# Common issue: phase0/build.sh permissions
# Fix: chmod +x phase0/build.sh in Dockerfile (already handled)
```

### Benchmark Tool Shows CORS Error
- This is normal for localhost
- The benchmark tool runs in browser, server runs in Docker
- Both on localhost, so CORS shouldn't block
- If it does: Run benchmark tool by opening `file://` URL directly

## 🌐 Deploy to Production

```bash
# Tag the image
docker tag systems-craft:latest your-registry/systems-craft:v1.0

# Push to registry
docker push your-registry/systems-craft:v1.0

# Deploy (example: Kubernetes)
kubectl apply -f k8s/deployment.yaml

# Or use Docker Swarm, ECS, Cloud Run, etc.
```

## 📚 Learn More

- [Architecture Explorer](website/architecture.html) - Visual system overview
- [Craft #1](website/craft1.html) - Ingestion optimization (40x improvement)
- [Craft #2](website/craft2.html) - Message queue patterns
- [Benchmark Tool](website/benchmark.html) - Measure performance
- [Progress Dashboard](website/progress.html) - Track your learning

---

**Time to first metric:** 30 seconds ⚡
**Time to understand bottlenecks:** 5 minutes 📊
**Time to 2k RPS:** 2-3 hours learning + optimizing 🚀

Start building!
