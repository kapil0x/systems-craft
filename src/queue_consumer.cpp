#include "queue_consumer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

QueueConsumer::QueueConsumer(const std::string& queue_path,
                           const std::string& consumer_group,
                           int num_partitions)
    : queue_path_(queue_path),
      consumer_group_(consumer_group),
      num_partitions_(num_partitions),
      running_(false) {

    // Initialize read offsets
    read_offsets_.resize(num_partitions_, 0);

    // Create consumer offset directory
    std::string offset_dir = "consumer_offsets/" + consumer_group_;
    fs::create_directories(offset_dir);

    // Load committed offsets from disk
    load_offsets();
}

void QueueConsumer::start() {
    running_ = true;

    std::cout << "Starting consumer with " << num_partitions_ << " partitions...\n";

    // Spawn one thread per partition (simple parallelism)
    std::vector<std::thread> threads;
    for (int i = 0; i < num_partitions_; i++) {
        threads.emplace_back([this, i]() {
            consume_partition(i);
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
}

void QueueConsumer::stop() {
    running_ = false;
}

void QueueConsumer::consume_partition(int partition) {
    std::cout << "Consumer thread for partition " << partition << " started\n";

    while (running_) {
        auto msg = read_next(partition);

        if (msg) {
            // Process message (Phase 9: just log it)
            std::cout << "[Partition " << msg->partition
                      << " | Offset " << msg->offset << "] "
                      << msg->data.substr(0, 100)  // First 100 chars
                      << (msg->data.size() > 100 ? "..." : "") << "\n";

            // Commit offset (mark as processed)
            commit_offset(partition, msg->offset);

        } else {
            // No new messages, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "Consumer thread for partition " << partition << " stopped\n";
}

std::optional<Message> QueueConsumer::read_next(int partition) {
    uint64_t next_offset = read_offsets_[partition] + 1;

    // Build filename
    std::string filename = queue_path_ + "/partition-" + std::to_string(partition)
                         + "/" + format_offset(next_offset) + ".msg";

    // Check if file exists
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;  // No message yet (caught up to producer)
    }

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    // Update read offset
    read_offsets_[partition] = next_offset;

    return Message{partition, next_offset, content};
}

void QueueConsumer::commit_offset(int partition, uint64_t offset) {
    std::string offset_file = "consumer_offsets/" + consumer_group_
                            + "/partition-" + std::to_string(partition) + ".offset";

    std::ofstream file(offset_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open offset file: " << offset_file << "\n";
        return;
    }
    file << offset;
    file.flush();
}

void QueueConsumer::load_offsets() {
    for (int i = 0; i < num_partitions_; i++) {
        std::string offset_file = "consumer_offsets/" + consumer_group_
                                + "/partition-" + std::to_string(i) + ".offset";

        std::ifstream file(offset_file);
        if (file.is_open()) {
            file >> read_offsets_[i];
            std::cout << "Loaded offset for partition " << i
                      << ": " << read_offsets_[i] << "\n";
        }
    }
}

std::string QueueConsumer::format_offset(uint64_t offset) const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(20) << offset;
    return oss.str();
}
