#include "kafka_consumer.h"
#include <iostream>
#include <chrono>
#include <thread>

KafkaConsumer::KafkaConsumer(const std::string& brokers, const std::string& topic, const std::string& group_id)
    : brokers_(brokers), topic_(topic), group_id_(group_id) {

    // Create Kafka configuration
    std::string errstr;
    RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    // Set brokers
    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set bootstrap.servers: " + errstr);
    }

    // Set group id
    if (conf->set("group.id", group_id, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set group.id: " + errstr);
    }

    // Callbacks removed for simplicity - can be added back if needed

    // Enable auto commit
    if (conf->set("enable.auto.commit", "true", errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set auto commit: " + errstr);
    }

    // Set auto commit interval
    if (conf->set("auto.commit.interval.ms", "1000", errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set auto commit interval: " + errstr);
    }

    // Create consumer
    consumer_.reset(RdKafka::KafkaConsumer::create(conf, errstr));
    if (!consumer_) {
        throw std::runtime_error("Failed to create consumer: " + errstr);
    }

    delete conf;

    std::cout << "Kafka consumer initialized: brokers=" << brokers
              << ", topic=" << topic << ", group=" << group_id << "\n";
}

KafkaConsumer::~KafkaConsumer() {
    stop();
}

void KafkaConsumer::start(std::function<void(const std::string& key, const std::string& message)> message_handler) {
    running_ = true;

    // Subscribe to topic
    std::vector<std::string> topics = {topic_};
    RdKafka::ErrorCode err = consumer_->subscribe(topics);
    if (err != RdKafka::ERR_NO_ERROR) {
        throw std::runtime_error("Failed to subscribe to topic: " + RdKafka::err2str(err));
    }

    std::cout << "Subscribed to topic: " << topic_ << "\n";

    // Main consumption loop
    while (running_) {
        RdKafka::Message* msg = consumer_->consume(1000); // 1 second timeout

        switch (msg->err()) {
            case RdKafka::ERR_NO_ERROR:
                // Process message
                {
                    std::string key;
                    if (msg->key()) {
                        key.assign(static_cast<const char*>(msg->key_pointer()), msg->key_len());
                    }

                    std::string payload;
                    if (msg->payload()) {
                        payload.assign(static_cast<const char*>(msg->payload()), msg->len());
                    }

                    message_count_++;
                    message_handler(key, payload);
                }
                break;

            case RdKafka::ERR__TIMED_OUT:
                // Timeout is normal, just continue
                break;

            case RdKafka::ERR__PARTITION_EOF:
                // End of partition, continue
                break;

            default:
                std::cerr << "Consume error: " << msg->errstr() << "\n";
                break;
        }

        delete msg;
    }
}

void KafkaConsumer::stop() {
    running_ = false;
    if (consumer_) {
        consumer_->close();
    }
}

// Callback methods removed for simplicity - can be added back if needed
