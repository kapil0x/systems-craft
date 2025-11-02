#include "kafka_producer.h"
#include <iostream>
#include <chrono>
#include <thread>

KafkaProducer::KafkaProducer(const std::string& brokers, const std::string& topic)
    : brokers_(brokers), topic_(topic) {

    // Create Kafka configuration
    std::string errstr;
    RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    // Set brokers
    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set bootstrap.servers: " + errstr);
    }

    // Configure for reliability and performance
    // Increase queue buffering to handle bursts
    conf->set("queue.buffering.max.messages", "100000", errstr);
    conf->set("queue.buffering.max.kbytes", "1048576", errstr);  // 1GB

    // Batch settings for throughput
    conf->set("batch.num.messages", "1000", errstr);
    conf->set("linger.ms", "10", errstr);  // Wait up to 10ms to batch

    // Retry settings for reliability
    conf->set("message.send.max.retries", "10", errstr);
    conf->set("retry.backoff.ms", "100", errstr);

    // Create producer
    producer_.reset(RdKafka::Producer::create(conf, errstr));
    if (!producer_) {
        throw std::runtime_error("Failed to create producer: " + errstr);
    }

    delete conf;

    std::cout << "Kafka producer initialized: brokers=" << brokers
              << ", topic=" << topic << "\n";
}

KafkaProducer::~KafkaProducer() {
    if (producer_) {
        std::cout << "Flushing Kafka producer (may take a few seconds)...\n";

        // Flush with longer timeout to ensure all messages are sent
        flush(std::chrono::milliseconds(10000));  // 10 seconds

        // Continue polling until queue is empty
        int polls = 0;
        while (producer_->outq_len() > 0 && polls < 100) {
            producer_->poll(100);
            polls++;
        }

        if (producer_->outq_len() > 0) {
            std::cerr << "Warning: " << producer_->outq_len()
                      << " messages still in queue at shutdown\n";
        }

        std::cout << "Kafka producer shutdown complete. Sent "
                  << message_count_ << " messages.\n";
    }
}

RdKafka::ErrorCode KafkaProducer::produce(const std::string& key, const std::string& message) {
    std::lock_guard<std::mutex> lock(producer_mutex_);

    if (!producer_) {
        return RdKafka::ERR__STATE;
    }

    // Check if queue is getting full
    int queue_len = producer_->outq_len();
    if (queue_len > 50000) {
        std::cerr << "Warning: Kafka queue backlog: " << queue_len << " messages\n";
    }

    // Use the correct produce method signature for librdkafka
    RdKafka::ErrorCode err = producer_->produce(
        topic_,                           // topic name
        RdKafka::Topic::PARTITION_UA,     // partition (unassigned - hash by key)
        RdKafka::Producer::RK_MSG_COPY,   // IMPORTANT: copy payload immediately
        const_cast<char*>(message.data()), message.size(), // payload
        key.empty() ? nullptr : key.data(), key.size(), // key for partitioning
        0,                               // timestamp (0 = now)
        nullptr                           // opaque pointer
    );

    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to produce message: " << RdKafka::err2str(err) << "\n";

        // If queue is full, try polling to make space
        if (err == RdKafka::ERR__QUEUE_FULL) {
            producer_->poll(10);  // Block up to 10ms waiting for delivery

            // Retry once
            err = producer_->produce(
                topic_,
                RdKafka::Topic::PARTITION_UA,
                RdKafka::Producer::RK_MSG_COPY,
                const_cast<char*>(message.data()), message.size(),
                key.empty() ? nullptr : key.data(), key.size(),
                0,
                nullptr
            );

            if (err != RdKafka::ERR_NO_ERROR) {
                std::cerr << "Retry also failed: " << RdKafka::err2str(err) << "\n";
                return err;
            }
        } else {
            return err;
        }
    }

    message_count_++;

    // Poll for delivery reports and errors (non-blocking)
    producer_->poll(0);

    return RdKafka::ERR_NO_ERROR;
}

RdKafka::ErrorCode KafkaProducer::flush(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(producer_mutex_);

    if (!producer_) {
        return RdKafka::ERR__STATE;
    }

    RdKafka::ErrorCode err = producer_->flush(timeout.count());
    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to flush producer: " << RdKafka::err2str(err) << "\n";
    }
    return err;
}
