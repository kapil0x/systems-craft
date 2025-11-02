#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

class PartitionedQueue {
private:
    std::string base_path_;
    int num_partitions_;
    std::vector<std::unique_ptr<std::mutex>> mutexes_;
    std::vector<uint64_t> offsets_;

public:
    // Initialize queue directory structure
    PartitionedQueue(const std::string& path, int num_partitions);

    // Write message to appropriate partition
    // Returns: partition number and offset where written
    std::pair<int, uint64_t> produce(const std::string& key,
                                     const std::string& message);

    // Determine partition for a key
    int get_partition(const std::string& key) const;

    // Load offsets from disk on startup
    void load_offsets();

    // Update offset tracking file
    void update_offset_file(int partition, uint64_t offset);

private:
    // Format offset as zero-padded string: 1 → "00000000000001"
    std::string format_offset(uint64_t offset) const;
};
