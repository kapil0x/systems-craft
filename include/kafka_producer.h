#pragma once

#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

class KafkaProducer {
private:
    // Per-partition producer pool for parallelization
    struct PartitionProducer {
        std::unique_ptr<RdKafka::Producer> producer;
        std::mutex mutex;  // Fine-grained lock per partition
        std::unique_ptr<std::thread> poll_thread;
        std::atomic<bool> poll_running{false};
        std::atomic<int> message_count{0};
    };

    std::string brokers_;
    std::string topic_;
    std::vector<std::unique_ptr<PartitionProducer>> partition_producers_;  // Pointers for mutex compatibility
    size_t num_partitions_;
    std::atomic<int> total_message_count_{0};

public:
    KafkaProducer(const std::string& brokers, const std::string& topic, size_t num_partitions = 8);
    ~KafkaProducer();

    // Send message to Kafka topic
    RdKafka::ErrorCode produce(const std::string& key, const std::string& message);

    // Flush pending messages
    RdKafka::ErrorCode flush(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Get message count for monitoring
    int get_message_count() const { return total_message_count_; }

    // Get broker and topic info
    const std::string& get_brokers() const { return brokers_; }
    const std::string& get_topic() const { return topic_; }

private:
    // Background polling thread for each partition producer
    void poll_loop(size_t partition_idx);

    // Select partition producer based on key hash
    size_t select_partition(const std::string& key) const;

    // Delivery report callback
    static void delivery_report_cb(RdKafka::Message& message);

    // Error callback
    static void error_cb(RdKafka::ErrorCode err, const std::string& reason);
};
