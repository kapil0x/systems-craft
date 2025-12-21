#include "queue_consumer.h"
#include "kafka_consumer.h"
#include "partitioned_queue.h"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping consumer...\n";
    running = false;
}

void message_handler(const std::string& key, const std::string& message) {
    // Process message (for now, just log it)
    std::cout << "[Consumer] Key: " << key << ", Message: " << message.substr(0, 200)
              << (message.size() > 200 ? "..." : "") << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n";
        std::cerr << "  File-based: " << argv[0] << " file <queue_path> <consumer_group> <num_partitions>\n";
        std::cerr << "  Kafka:       " << argv[0] << " kafka <brokers> <topic> <group_id>\n";
        std::cerr << "Examples:\n";
        std::cerr << "  " << argv[0] << " file queue storage-writer 4\n";
        std::cerr << "  " << argv[0] << " kafka localhost:9092 metrics consumer-group-1\n";
        return 1;
    }

    std::string mode = argv[1];

    // Set up signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        if (mode == "file") {
            if (argc != 5) {
                std::cerr << "File mode requires: <queue_path> <consumer_group> <num_partitions>\n";
                return 1;
            }

            std::string queue_path = argv[2];
            std::string consumer_group = argv[3];
            int num_partitions = std::stoi(argv[4]);

            std::cout << "Starting file-based message queue consumer...\n";
            std::cout << "Queue path: " << queue_path << "\n";
            std::cout << "Consumer group: " << consumer_group << "\n";
            std::cout << "Partitions: " << num_partitions << "\n";
            std::cout << "Press Ctrl+C to stop\n\n";

            QueueConsumer consumer(queue_path, consumer_group, num_partitions);
            consumer.start();

        } else if (mode == "kafka") {
            if (argc != 5) {
                std::cerr << "Kafka mode requires: <brokers> <topic> <group_id>\n";
                return 1;
            }

            std::string brokers = argv[2];
            std::string topic = argv[3];
            std::string group_id = argv[4];

            std::cout << "Starting Kafka message consumer...\n";
            std::cout << "Brokers: " << brokers << "\n";
            std::cout << "Topic: " << topic << "\n";
            std::cout << "Group ID: " << group_id << "\n";
            std::cout << "Press Ctrl+C to stop\n\n";

            KafkaConsumer consumer(brokers, topic, group_id);

            // Run consumer in a separate thread so we can handle signals
            std::thread consumer_thread([&consumer]() {
                consumer.start(message_handler);
            });

            // Wait for stop signal
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            consumer.stop();
            consumer_thread.join();

        } else {
            std::cerr << "Unknown mode: " << mode << ". Use 'file' or 'kafka'\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Consumer stopped gracefully.\n";
    return 0;
}
