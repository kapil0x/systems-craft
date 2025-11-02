#pragma once

#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <cstdint>

struct Message {
    int partition;
    uint64_t offset;
    std::string data;
};

class QueueConsumer {
private:
    std::string queue_path_;
    std::string consumer_group_;
    int num_partitions_;
    std::vector<uint64_t> read_offsets_;
    bool running_;

public:
    QueueConsumer(const std::string& queue_path,
                  const std::string& consumer_group,
                  int num_partitions);

    // Start consuming (spawns threads for each partition)
    void start();

    // Stop consuming gracefully
    void stop();

    // Process single partition (runs in thread)
    void consume_partition(int partition);

    // Read next message from partition
    std::optional<Message> read_next(int partition);

    // Commit offset after successful processing
    void commit_offset(int partition, uint64_t offset);

    // Load committed offsets from disk
    void load_offsets();

private:
    // Format offset as zero-padded string: 1 → "00000000000001"
    std::string format_offset(uint64_t offset) const;
};
