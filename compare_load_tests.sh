#!/bin/bash

# Comparison test: Original vs Persistent Connection Load Tests
# Hypothesis: Original test is bottlenecked by TCP connection overhead

echo "=== Load Test Client Comparison ==="
echo "Testing hypothesis: Original load test bottleneck is TCP connection overhead"
echo ""

# Check if server is running
if ! pgrep -x "metricstream_server" > /dev/null; then
    echo "❌ MetricStream server not running. Start it first:"
    echo "   ./build/metricstream_server &"
    exit 1
fi

echo "✅ MetricStream server detected"
echo ""

# Test configuration: 2000 RPS target
CLIENTS=100
REQUESTS_PER_CLIENT=100
INTERVAL_US=5000  # 5ms = 5000μs → 100 clients × 200 req/sec = 20,000 RPS theoretical

echo "📊 Test Configuration:"
echo "   Clients: $CLIENTS"
echo "   Requests per client: $REQUESTS_PER_CLIENT"
echo "   Interval: ${INTERVAL_US}μs"
echo "   Total requests: $((CLIENTS * REQUESTS_PER_CLIENT))"
echo ""

# Test 1: Original load test (creates new connection per request)
echo "🔬 Test 1: Original Load Test (New connection per request)"
echo "   Expected bottleneck: TCP handshake overhead"
echo ""
./build/load_test 8080 $CLIENTS $REQUESTS_PER_CLIENT $((INTERVAL_US / 1000)) > original_results.txt
cat original_results.txt

echo ""
echo "💤 Cooling down (5s)..."
sleep 5
echo ""

# Test 2: Persistent connection load test (reuses connections)
echo "🔬 Test 2: Persistent Connection Load Test (Connection reuse)"
echo "   Expected: Higher success rate, lower latency"
echo ""
./build/load_test_persistent 8080 $CLIENTS $REQUESTS_PER_CLIENT $INTERVAL_US > persistent_results.txt
cat persistent_results.txt

echo ""
echo "=== Comparison Results ==="
echo ""

# Extract metrics
original_success=$(grep "Success Rate:" original_results.txt | awk '{print $3}' | sed 's/%//')
original_latency=$(grep "Avg Latency:" original_results.txt | awk '{print $3}')
original_rps=$(grep "Requests/sec:" original_results.txt | awk '{print $2}')

persistent_success=$(grep "Success Rate:" persistent_results.txt | awk '{print $3}' | sed 's/%//')
persistent_latency=$(grep "Avg Latency:" persistent_results.txt | awk '{print $3}')
persistent_rps=$(grep "Actual RPS:" persistent_results.txt | awk '{print $3}')

echo "📈 Original (New Connections):"
echo "   Success Rate: ${original_success}%"
echo "   Avg Latency: ${original_latency}"
echo "   RPS: ${original_rps}"
echo ""

echo "📈 Persistent (Reused Connections):"
echo "   Success Rate: ${persistent_success}%"
echo "   Avg Latency: ${persistent_latency}"
echo "   RPS: ${persistent_rps}"
echo ""

# Calculate improvement
if command -v bc &> /dev/null; then
    success_improvement=$(echo "scale=2; $persistent_success - $original_success" | bc)
    echo "✅ Success Rate Improvement: +${success_improvement}%"
    echo ""
fi

echo "🔍 Analysis:"
echo "If persistent test shows significantly better results:"
echo "  → Original bottleneck was TCP connection overhead (client-side)"
echo "  → Server can actually handle much higher load"
echo ""
echo "If results are similar:"
echo "  → Bottleneck is in server processing (accept loop, thread pool, file I/O)"
echo "  → Need to profile server internals"
echo ""

# Cleanup
rm -f original_results.txt persistent_results.txt
