#pragma once

#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>

class KafkaConsumer {
private:
    std::string brokers_;
    std::string topic_;
    std::string group_id_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
    std::atomic<bool> running_{false};
    std::atomic<int> message_count_{0};

public:
    KafkaConsumer(const std::string& brokers, const std::string& topic, const std::string& group_id);
    ~KafkaConsumer();

    // Start consuming messages
    void start(std::function<void(const std::string& key, const std::string& message)> message_handler);

    // Stop consuming
    void stop();

    // Get message count for monitoring
    int get_message_count() const { return message_count_; }

    // Get connection info
    const std::string& get_brokers() const { return brokers_; }
    const std::string& get_topic() const { return topic_; }
    const std::string& get_group_id() const { return group_id_; }

private:
    // Rebalance callback
    static void rebalance_cb(RdKafka::KafkaConsumer* consumer,
                            RdKafka::ErrorCode err,
                            std::vector<RdKafka::TopicPartition*>& partitions);

    // Error callback
    static void error_cb(RdKafka::ErrorCode err, const std::string& reason);
};
