#!/bin/bash

# Simple Kafka vs File-Based Comparison
# Focuses on throughput measurement without persistent connections

set -e

echo "═══════════════════════════════════════════════════════════"
echo "  Simple Kafka vs File-Based Benchmark"
echo "═══════════════════════════════════════════════════════════"
echo ""

PORT=8080
RESULTS="simple_benchmark_results.txt"

# Clean up
pkill -f metricstream_server || true
sleep 1

# Header
echo "Mode,Test,RPS,Success_Rate,Avg_Latency_ms" > $RESULTS

run_test() {
    local mode=$1
    local clients=$2
    local requests=$3

    echo "Testing ${mode} mode: ${clients} clients × ${requests} requests"

    # Clean data
    rm -rf queue/ consumer_offsets/ metrics.jsonl 2>/dev/null || true

    # Start server
    if [ "$mode" == "file" ]; then
        ./build/metricstream_server ${PORT} file > server.log 2>&1 &
    else
        ./build/metricstream_server ${PORT} kafka localhost:9092 metrics > server.log 2>&1 &
    fi

    SERVER_PID=$!
    sleep 2

    # Run test
    OUTPUT=$(./build/load_test ${PORT} ${clients} ${requests} 2>&1)

    # Extract metrics
    SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
    RPS=$(echo "$OUTPUT" | grep "Throughput:" | awk '{print $2}')
    LATENCY=$(echo "$OUTPUT" | grep "Avg latency:" | awk '{print $3}')

    echo "${mode},${clients}x${requests},${RPS},${SUCCESS_RATE},${LATENCY}" >> $RESULTS
    echo "  ✓ RPS: ${RPS}, Success: ${SUCCESS_RATE}"

    # Stop server
    kill ${SERVER_PID} 2>/dev/null || true
    sleep 1
}

echo "TEST 1: File-Based Queue"
echo "-------------------------"
run_test "file" 10 50
run_test "file" 20 50
run_test "file" 50 20

echo ""
echo "TEST 2: Kafka Queue"
echo "-------------------"
run_test "kafka" 10 50
run_test "kafka" 20 50
run_test "kafka" 50 20
run_test "kafka" 100 20

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "Results saved to: $RESULTS"
echo ""
column -t -s',' $RESULTS
echo ""
echo "═══════════════════════════════════════════════════════════"
