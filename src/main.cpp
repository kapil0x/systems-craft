#include "ingestion_service.h"
#include "partitioned_queue.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

std::unique_ptr<metricstream::IngestionService> service;

void signal_handler(int signal) {
    if (service) {
        std::cout << "\nShutting down gracefully..." << std::endl;
        service->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // Usage: ./metricstream_server <port> [mode] [kafka_brokers] [topic]
    // Example: ./metricstream_server 8080 kafka localhost:9092 metrics
    // Example: ./metricstream_server 8080 file

    int port = 8080;
    metricstream::QueueMode queue_mode = metricstream::QueueMode::FILE_BASED;
    std::string kafka_brokers = "localhost:9092";
    std::string kafka_topic = "metrics";
    int num_partitions = 4;

    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    if (argc > 2) {
        std::string mode_arg = argv[2];
        if (mode_arg == "kafka") {
            queue_mode = metricstream::QueueMode::KAFKA;
            if (argc > 3) {
                kafka_brokers = argv[3];
            }
            if (argc > 4) {
                kafka_topic = argv[4];
            }
        }
    }

    std::cout << "Starting MetricStream server on port " << port << std::endl;
    std::cout << "Using queue mode: " << (queue_mode == metricstream::QueueMode::FILE_BASED ? "file-based" : "kafka") << "\n";
    if (queue_mode == metricstream::QueueMode::KAFKA) {
        std::cout << "Kafka brokers: " << kafka_brokers << ", topic: " << kafka_topic << "\n";
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    service = std::make_unique<metricstream::IngestionService>(port, 10000, num_partitions, queue_mode, kafka_brokers);
    service->start();
    
    // Keep running until signal with periodic stats
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // TODO(human): Add performance monitoring here
        // Consider tracking and logging:
        // - Active connection count
        // - Request processing time distribution
        // - Queue depths and memory usage
        // - Failed vs successful request ratios
        // This will help identify bottlenecks at high load
        
        // Phase 3 Analysis: JSON parsing optimization needed
        // Current bottleneck: Multiple string::find() calls and substr() allocations
        // Target: Single-pass parser with string views and pre-allocated containers
    }
    
    return 0;
}