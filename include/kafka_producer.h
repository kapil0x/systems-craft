#pragma once

#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>

class KafkaProducer {
private:
    std::string brokers_;
    std::string topic_;
    std::unique_ptr<RdKafka::Producer> producer_;
    std::atomic<int> message_count_{0};
    mutable std::mutex producer_mutex_;  // Protect concurrent produce() calls

public:
    KafkaProducer(const std::string& brokers, const std::string& topic);
    ~KafkaProducer();

    // Send message to Kafka topic
    RdKafka::ErrorCode produce(const std::string& key, const std::string& message);

    // Flush pending messages
    RdKafka::ErrorCode flush(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Get message count for monitoring
    int get_message_count() const { return message_count_; }

    // Get broker and topic info
    const std::string& get_brokers() const { return brokers_; }
    const std::string& get_topic() const { return topic_; }

private:
    // Delivery report callback
    static void delivery_report_cb(RdKafka::Message& message);

    // Error callback
    static void error_cb(RdKafka::ErrorCode err, const std::string& reason);
};
