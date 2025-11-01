#!/bin/bash

# Comprehensive RPS (Requests Per Second) Measurement Script
# Tests MetricStream server at various load levels to find maximum RPS

echo "═══════════════════════════════════════════════════════════"
echo "          MetricStream RPS Capacity Measurement"
echo "═══════════════════════════════════════════════════════════"
echo ""

PORT=8080
RESULTS_FILE="rps_measurement_results.txt"

# Clear previous results
> $RESULTS_FILE

echo "Test Configuration:"
echo "  Server Port: $PORT"
echo "  Results File: $RESULTS_FILE"
echo ""

# Function to calculate RPS from load test output
calculate_rps() {
    local clients=$1
    local requests_per_client=$2
    local duration=$3

    local total_requests=$((clients * requests_per_client))
    local rps=$(awk "BEGIN {printf \"%.2f\", $total_requests / $duration}")

    echo "$rps"
}

# Test 1: Baseline - Low load
echo "─────────────────────────────────────────────────────────────"
echo "TEST 1: Baseline (10 concurrent clients, 100 requests each)"
echo "─────────────────────────────────────────────────────────────"

START_TIME=$(date +%s.%N)
OUTPUT=$(./build/load_test_persistent $PORT 10 100 1000)
END_TIME=$(date +%s.%N)
DURATION=$(awk "BEGIN {printf \"%.3f\", $END_TIME - $START_TIME}")

echo "$OUTPUT"
SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
AVG_LATENCY=$(echo "$OUTPUT" | grep "Average latency:" | awk '{print $3}')
RPS=$(calculate_rps 10 100 $DURATION)

echo ""
echo "Results:"
echo "  Total Duration: ${DURATION}s"
echo "  RPS: $RPS"
echo ""

echo "TEST 1 - Baseline (10 clients, 100 req/client)" >> $RESULTS_FILE
echo "  Success Rate: $SUCCESS_RATE" >> $RESULTS_FILE
echo "  Avg Latency: $AVG_LATENCY" >> $RESULTS_FILE
echo "  RPS: $RPS" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

sleep 2

# Test 2: Medium load
echo "─────────────────────────────────────────────────────────────"
echo "TEST 2: Medium Load (50 concurrent clients, 100 requests each)"
echo "─────────────────────────────────────────────────────────────"

START_TIME=$(date +%s.%N)
OUTPUT=$(./build/load_test_persistent $PORT 50 100 5000)
END_TIME=$(date +%s.%N)
DURATION=$(awk "BEGIN {printf \"%.3f\", $END_TIME - $START_TIME}")

echo "$OUTPUT"
SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
AVG_LATENCY=$(echo "$OUTPUT" | grep "Average latency:" | awk '{print $3}')
RPS=$(calculate_rps 50 100 $DURATION)

echo ""
echo "Results:"
echo "  Total Duration: ${DURATION}s"
echo "  RPS: $RPS"
echo ""

echo "TEST 2 - Medium Load (50 clients, 100 req/client)" >> $RESULTS_FILE
echo "  Success Rate: $SUCCESS_RATE" >> $RESULTS_FILE
echo "  Avg Latency: $AVG_LATENCY" >> $RESULTS_FILE
echo "  RPS: $RPS" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

sleep 2

# Test 3: High load
echo "─────────────────────────────────────────────────────────────"
echo "TEST 3: High Load (100 concurrent clients, 100 requests each)"
echo "─────────────────────────────────────────────────────────────"

START_TIME=$(date +%s.%N)
OUTPUT=$(./build/load_test_persistent $PORT 100 100 5000)
END_TIME=$(date +%s.%N)
DURATION=$(awk "BEGIN {printf \"%.3f\", $END_TIME - $START_TIME}")

echo "$OUTPUT"
SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
AVG_LATENCY=$(echo "$OUTPUT" | grep "Average latency:" | awk '{print $3}')
RPS=$(calculate_rps 100 100 $DURATION)

echo ""
echo "Results:"
echo "  Total Duration: ${DURATION}s"
echo "  RPS: $RPS"
echo ""

echo "TEST 3 - High Load (100 clients, 100 req/client)" >> $RESULTS_FILE
echo "  Success Rate: $SUCCESS_RATE" >> $RESULTS_FILE
echo "  Avg Latency: $AVG_LATENCY" >> $RESULTS_FILE
echo "  RPS: $RPS" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

sleep 2

# Test 4: Very high load
echo "─────────────────────────────────────────────────────────────"
echo "TEST 4: Very High Load (200 concurrent clients, 50 requests each)"
echo "─────────────────────────────────────────────────────────────"

START_TIME=$(date +%s.%N)
OUTPUT=$(./build/load_test_persistent $PORT 200 50 5000)
END_TIME=$(date +%s.%N)
DURATION=$(awk "BEGIN {printf \"%.3f\", $END_TIME - $START_TIME}")

echo "$OUTPUT"
SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
AVG_LATENCY=$(echo "$OUTPUT" | grep "Average latency:" | awk '{print $3}')
RPS=$(calculate_rps 200 50 $DURATION)

echo ""
echo "Results:"
echo "  Total Duration: ${DURATION}s"
echo "  RPS: $RPS"
echo ""

echo "TEST 4 - Very High Load (200 clients, 50 req/client)" >> $RESULTS_FILE
echo "  Success Rate: $SUCCESS_RATE" >> $RESULTS_FILE
echo "  Avg Latency: $AVG_LATENCY" >> $RESULTS_FILE
echo "  RPS: $RPS" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

sleep 2

# Test 5: Extreme load - find the breaking point
echo "─────────────────────────────────────────────────────────────"
echo "TEST 5: Extreme Load (500 concurrent clients, 20 requests each)"
echo "─────────────────────────────────────────────────────────────"

START_TIME=$(date +%s.%N)
OUTPUT=$(./build/load_test_persistent $PORT 500 20 5000)
END_TIME=$(date +%s.%N)
DURATION=$(awk "BEGIN {printf \"%.3f\", $END_TIME - $START_TIME}")

echo "$OUTPUT"
SUCCESS_RATE=$(echo "$OUTPUT" | grep "Success rate:" | awk '{print $3}')
AVG_LATENCY=$(echo "$OUTPUT" | grep "Average latency:" | awk '{print $3}')
RPS=$(calculate_rps 500 20 $DURATION)

echo ""
echo "Results:"
echo "  Total Duration: ${DURATION}s"
echo "  RPS: $RPS"
echo ""

echo "TEST 5 - Extreme Load (500 clients, 20 req/client)" >> $RESULTS_FILE
echo "  Success Rate: $SUCCESS_RATE" >> $RESULTS_FILE
echo "  Avg Latency: $AVG_LATENCY" >> $RESULTS_FILE
echo "  RPS: $RPS" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

# Summary
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "                    TEST SUMMARY"
echo "═══════════════════════════════════════════════════════════"
echo ""
cat $RESULTS_FILE
echo ""
echo "Full results saved to: $RESULTS_FILE"
echo ""
echo "Next Steps:"
echo "  1. Analyze bottlenecks from these results"
echo "  2. Profile server to identify hot paths"
echo "  3. Design Phase 8 optimization"
echo ""
