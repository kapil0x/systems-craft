#!/bin/bash

cd /Users/kapiljain/claude/test/metricstream/.worktrees/craft-2-phase-11-kafka

PORT=8090

echo "═══════════════════════════════════════════════════════════"
echo "  Kafka vs File-Based Benchmark - Fixed Version"
echo "═══════════════════════════════════════════════════════════"
echo ""

# Kill any existing servers
pkill -f metricstream_server
sleep 2

# Clean data
rm -rf queue/ consumer_offsets/ metrics.jsonl

echo "TEST 1: File-Based Queue"
echo "-------------------------"
./build/metricstream_server $PORT file > /tmp/server_file.log 2>&1 &
FILE_PID=$!
sleep 4

./build/load_test $PORT 20 50 > /tmp/file_results.txt 2>&1

cat /tmp/file_results.txt
kill $FILE_PID 2>/dev/null
pkill -f metricstream_server
sleep 3

echo ""
echo "TEST 2: Kafka Queue"
echo "-------------------"
./build/metricstream_server $PORT kafka localhost:9092 metrics > /tmp/server_kafka.log 2>&1 &
KAFKA_PID=$!
sleep 4

./build/load_test $PORT 20 50 > /tmp/kafka_results.txt 2>&1

cat /tmp/kafka_results.txt
kill $KAFKA_PID
sleep 2

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  COMPARISON SUMMARY"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "FILE-BASED:"
grep -E "Success Rate|Requests/sec|Avg Latency" /tmp/file_results.txt
echo ""
echo "KAFKA:"
grep -E "Success Rate|Requests/sec|Avg Latency" /tmp/kafka_results.txt
echo ""
echo "═══════════════════════════════════════════════════════════"
