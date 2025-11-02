#include "partitioned_queue.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

PartitionedQueue::PartitionedQueue(const std::string& path, int num_partitions)
    : base_path_(path), num_partitions_(num_partitions) {

    // Create directory structure
    fs::create_directories(base_path_);

    for (int i = 0; i < num_partitions_; i++) {
        std::string partition_path = base_path_ + "/partition-" + std::to_string(i);
        fs::create_directories(partition_path);

        // Initialize mutex and offset for this partition
        mutexes_.push_back(std::make_unique<std::mutex>());
        offsets_.push_back(0);
    }

    // Load existing offsets from disk
    load_offsets();
}

std::pair<int, uint64_t> PartitionedQueue::produce(const std::string& key,
                                                   const std::string& message) {
    // 1. Determine partition using hash
    int partition = get_partition(key);

    // 2. Lock only this partition (allows parallel writes to other partitions)
    std::lock_guard<std::mutex> lock(*mutexes_[partition]);

    // 3. Get next offset
    uint64_t offset = ++offsets_[partition];

    // 4. Write message to file
    std::string filename = base_path_ + "/partition-" + std::to_string(partition)
                         + "/" + format_offset(offset) + ".msg";

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    file << message;
    file.flush();

    // 5. Ensure durability (flush is sufficient for learning implementation)
    // Note: In production, you'd want fsync() for guaranteed durability

    // 6. Update offset tracking file
    update_offset_file(partition, offsets_[partition]);

    return {partition, offset};
}

int PartitionedQueue::get_partition(const std::string& key) const {
    // Use std::hash for deterministic partitioning
    std::hash<std::string> hasher;
    size_t hash_value = hasher(key);
    return hash_value % num_partitions_;
}

void PartitionedQueue::load_offsets() {
    for (int i = 0; i < num_partitions_; i++) {
        std::string offset_file = base_path_ + "/partition-" + std::to_string(i)
                                + "/offset.txt";
        std::ifstream file(offset_file);
        if (file.is_open()) {
            file >> offsets_[i];
        } else {
            offsets_[i] = 0;  // Start from 0 if no offset file
        }
    }
}

void PartitionedQueue::update_offset_file(int partition, uint64_t offset) {
    std::string offset_file = base_path_ + "/partition-" + std::to_string(partition)
                            + "/offset.txt";
    std::ofstream file(offset_file);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open offset file: " + offset_file);
    }
    file << offset;
    file.flush();
}

std::string PartitionedQueue::format_offset(uint64_t offset) const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(20) << offset;
    return oss.str();
}
