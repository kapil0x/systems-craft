#!/bin/bash

# Kafka vs File-Based Queue Comparison Benchmark
# Phase 11: Craft #2 - Distributed Message Queue
#
# This script compares the performance of:
# 1. File-based partitioned queue (Phase 9)
# 2. Kafka-based message queue (Phase 11)
#
# Prerequisites:
# - Kafka running on localhost:9092 with topic "metrics"
# - Built binaries in build/ directory

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Kafka vs File-Based Queue Performance Comparison         ║${NC}"
echo -e "${BLUE}║  Phase 11 - Craft #2: Distributed Message Queue           ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Configuration
PORT=8080
NUM_PARTITIONS=4
KAFKA_BROKERS="localhost:9092"
KAFKA_TOPIC="metrics"
RESULTS_FILE="kafka_comparison_results.txt"

# Test parameters
WARMUP_CLIENTS=10
WARMUP_REQUESTS=10

TEST_CLIENTS=(20 50 100 200)
TEST_REQUESTS_PER_CLIENT=100

# Function to check if Kafka is running
check_kafka() {
    echo -e "${YELLOW}Checking Kafka availability...${NC}"

    # Try to connect to Kafka using nc (netcat)
    if ! nc -z localhost 9092 2>/dev/null; then
        echo -e "${RED}ERROR: Kafka is not running on localhost:9092${NC}"
        echo ""
        echo "To start Kafka locally:"
        echo "  1. brew install kafka"
        echo "  2. brew services start zookeeper"
        echo "  3. brew services start kafka"
        echo ""
        echo "Or use Docker:"
        echo "  docker run -d -p 9092:9092 apache/kafka:latest"
        echo ""
        return 1
    fi

    echo -e "${GREEN}✓ Kafka is running${NC}"
    return 0
}

# Function to create Kafka topic if it doesn't exist
create_kafka_topic() {
    echo -e "${YELLOW}Creating Kafka topic '${KAFKA_TOPIC}'...${NC}"

    # Check if kafka-topics command is available
    if command -v kafka-topics &> /dev/null; then
        kafka-topics --create \
            --bootstrap-server ${KAFKA_BROKERS} \
            --topic ${KAFKA_TOPIC} \
            --partitions ${NUM_PARTITIONS} \
            --replication-factor 1 \
            --if-not-exists 2>/dev/null || true
        echo -e "${GREEN}✓ Topic ready${NC}"
    else
        echo -e "${YELLOW}kafka-topics not found, assuming topic exists${NC}"
    fi
}

# Function to cleanup old data
cleanup() {
    local mode=$1
    echo -e "${YELLOW}Cleaning up ${mode} data...${NC}"

    if [ "$mode" == "file" ]; then
        rm -rf queue/ consumer_offsets/ metrics.jsonl 2>/dev/null || true
    elif [ "$mode" == "kafka" ]; then
        # Optionally delete and recreate Kafka topic for clean state
        if command -v kafka-topics &> /dev/null; then
            kafka-topics --delete --bootstrap-server ${KAFKA_BROKERS} --topic ${KAFKA_TOPIC} 2>/dev/null || true
            sleep 2
            create_kafka_topic
        fi
    fi

    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

# Function to start server in specific mode
start_server() {
    local mode=$1
    echo -e "${YELLOW}Starting MetricStream server (${mode} mode)...${NC}"

    # Kill any existing server
    pkill -f metricstream_server || true
    sleep 1

    # Start server based on mode
    if [ "$mode" == "file" ]; then
        ./build/metricstream_server ${PORT} file > server_${mode}.log 2>&1 &
    elif [ "$mode" == "kafka" ]; then
        ./build/metricstream_server ${PORT} kafka ${KAFKA_BROKERS} ${KAFKA_TOPIC} > server_${mode}.log 2>&1 &
    fi

    SERVER_PID=$!

    # Wait for server to be ready
    sleep 2

    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}ERROR: Server failed to start${NC}"
        cat server_${mode}.log
        exit 1
    fi

    echo -e "${GREEN}✓ Server started (PID: ${SERVER_PID})${NC}"
}

# Function to run load test
run_load_test() {
    local clients=$1
    local requests=$2
    local label=$3

    echo -e "${YELLOW}Running test: ${clients} clients × ${requests} requests${NC}"

    ./build/load_test_persistent ${PORT} ${clients} ${requests} 2>&1 | tee -a temp_test_output.txt

    # Extract key metrics
    local success_rate=$(grep "Success rate:" temp_test_output.txt | tail -1 | awk '{print $3}')
    local total_time=$(grep "Total test time:" temp_test_output.txt | tail -1 | awk '{print $4}')
    local rps=$(grep "Throughput:" temp_test_output.txt | tail -1 | awk '{print $2}')

    # Store results
    echo "${label},${clients},${requests},${success_rate},${rps},${total_time}" >> ${RESULTS_FILE}

    rm -f temp_test_output.txt

    echo -e "${GREEN}  ✓ Success: ${success_rate}, RPS: ${rps}${NC}"
    sleep 2
}

# Function to stop server
stop_server() {
    echo -e "${YELLOW}Stopping server...${NC}"
    pkill -f metricstream_server || true
    sleep 1
    echo -e "${GREEN}✓ Server stopped${NC}"
}

# Main benchmark execution
main() {
    # Initialize results file
    echo "Mode,Clients,Requests,Success_Rate,RPS,Total_Time" > ${RESULTS_FILE}

    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  TEST 1: File-Based Partitioned Queue${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    cleanup "file"
    start_server "file"

    # Warmup
    echo -e "${YELLOW}Warmup run...${NC}"
    ./build/load_test_persistent ${PORT} ${WARMUP_CLIENTS} ${WARMUP_REQUESTS} > /dev/null 2>&1 || true
    sleep 2

    # Run tests
    for clients in "${TEST_CLIENTS[@]}"; do
        run_load_test ${clients} ${TEST_REQUESTS_PER_CLIENT} "file-based"
    done

    stop_server

    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  TEST 2: Kafka-Based Message Queue${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    # Check Kafka availability
    if ! check_kafka; then
        echo -e "${YELLOW}Skipping Kafka tests - Kafka not available${NC}"
        echo "file-based,N/A,N/A,N/A,N/A,Kafka not available" >> ${RESULTS_FILE}
    else
        create_kafka_topic
        cleanup "kafka"
        start_server "kafka"

        # Warmup
        echo -e "${YELLOW}Warmup run...${NC}"
        ./build/load_test_persistent ${PORT} ${WARMUP_CLIENTS} ${WARMUP_REQUESTS} > /dev/null 2>&1 || true
        sleep 2

        # Run tests
        for clients in "${TEST_CLIENTS[@]}"; do
            run_load_test ${clients} ${TEST_REQUESTS_PER_CLIENT} "kafka"
        done

        stop_server
    fi

    # Generate comparison report
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  RESULTS SUMMARY${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    echo -e "${GREEN}Results saved to: ${RESULTS_FILE}${NC}"
    echo ""

    # Display results table
    column -t -s',' ${RESULTS_FILE}

    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}Benchmark complete!${NC}"
    echo ""
    echo "Key Learnings:"
    echo "  1. File-based: Simple, good for single-machine learning"
    echo "  2. Kafka: Production-ready, horizontal scaling, replication"
    echo "  3. Trade-off: Simplicity vs Performance vs Scalability"
    echo ""
    echo "Next steps:"
    echo "  - Analyze results in ${RESULTS_FILE}"
    echo "  - Review server logs: server_file.log, server_kafka.log"
    echo "  - Document learnings in craft2/phase-11-kafka/results.md"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
}

# Run the benchmark
main
