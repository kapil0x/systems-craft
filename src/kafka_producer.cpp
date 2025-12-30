#include "kafka_producer.h"
#include <iostream>
#include <chrono>
#include <thread>

KafkaProducer::KafkaProducer(const std::string& brokers, const std::string& topic, size_t num_partitions)
    : brokers_(brokers), topic_(topic), num_partitions_(num_partitions) {

    std::cout << "Initializing " << num_partitions << " parallel Kafka producers for maximum throughput...\n";

    // Reserve space to avoid reallocations
    partition_producers_.reserve(num_partitions);

    // Create one producer per partition for true parallelism
    for (size_t i = 0; i < num_partitions; ++i) {
        // Create Kafka configuration for this partition producer
        std::string errstr;
        RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

        // Set brokers
        if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
            throw std::runtime_error("Failed to set bootstrap.servers: " + errstr);
        }

        // Configure for MAXIMUM THROUGHPUT on 8-core, 16GB machine
        // Aggressive queue buffering to handle bursts
        conf->set("queue.buffering.max.messages", "1000000", errstr);  // 1M messages
        conf->set("queue.buffering.max.kbytes", "2097152", errstr);  // 2GB internal buffer

        // Batching settings optimized for throughput
        conf->set("batch.num.messages", "10000", errstr);  // Larger batches = better compression
        conf->set("batch.size", "1000000", errstr);  // 1MB batch size
        conf->set("linger.ms", "5", errstr);  // Lower latency, still allows batching

        // Compression for network efficiency
        conf->set("compression.type", "lz4", errstr);  // Fast compression, good ratio
        conf->set("compression.level", "1", errstr);  // Low CPU overhead

        // Network and I/O optimization
        conf->set("socket.send.buffer.bytes", "1048576", errstr);  // 1MB socket buffer
        conf->set("socket.receive.buffer.bytes", "1048576", errstr);

        // Increase in-flight requests for pipelining
        conf->set("max.in.flight.requests.per.connection", "5", errstr);

        // Reliability settings
        conf->set("message.send.max.retries", "10", errstr);
        conf->set("retry.backoff.ms", "100", errstr);
        conf->set("request.required.acks", "1", errstr);  // Leader ack only (not all replicas)

        // Create producer for this partition
        std::unique_ptr<RdKafka::Producer> producer(RdKafka::Producer::create(conf, errstr));
        if (!producer) {
            throw std::runtime_error("Failed to create producer " + std::to_string(i) + ": " + errstr);
        }

        delete conf;

        // Create PartitionProducer and add to vector
        auto pp = std::make_unique<PartitionProducer>();
        pp->producer = std::move(producer);
        pp->poll_running = true;
        pp->poll_thread = std::make_unique<std::thread>(&KafkaProducer::poll_loop, this, i);

        partition_producers_.push_back(std::move(pp));
    }

    std::cout << "Kafka producer pool initialized: " << num_partitions << " producers, "
              << "brokers=" << brokers << ", topic=" << topic << "\n";
}

KafkaProducer::~KafkaProducer() {
    std::cout << "Shutting down Kafka producer pool (" << num_partitions_ << " producers)...\n";

    // Stop all background polling threads
    for (auto& pp : partition_producers_) {
        pp->poll_running = false;
        if (pp->poll_thread && pp->poll_thread->joinable()) {
            pp->poll_thread->join();
        }
    }

    // Flush all producers
    std::cout << "Flushing all producers (may take a few seconds)...\n";
    flush(std::chrono::milliseconds(10000));  // 10 seconds

    // Final poll to drain callbacks
    for (auto& pp : partition_producers_) {
        if (pp->producer) {
            int polls = 0;
            while (pp->producer->outq_len() > 0 && polls < 100) {
                std::lock_guard<std::mutex> lock(pp->mutex);
                pp->producer->poll(100);
                polls++;
            }

            if (pp->producer->outq_len() > 0) {
                std::cerr << "Warning: Producer has " << pp->producer->outq_len()
                          << " messages still in queue at shutdown\n";
            }
        }
    }

    std::cout << "Kafka producer pool shutdown complete. Total sent: "
              << total_message_count_ << " messages.\n";
}

size_t KafkaProducer::select_partition(const std::string& key) const {
    // Hash-based partition selection for load balancing
    std::hash<std::string> hasher;
    return hasher(key) % num_partitions_;
}

RdKafka::ErrorCode KafkaProducer::produce(const std::string& key, const std::string& message) {
    // PARALLELIZATION: Route to partition producer based on key hash
    // Multiple threads can produce to different partitions simultaneously

    size_t partition_idx = select_partition(key);
    auto& pp = *partition_producers_[partition_idx];

    if (!pp.producer) {
        return RdKafka::ERR__STATE;
    }

    RdKafka::ErrorCode err;
    {
        // Fine-grained lock - only blocks other requests to THIS partition
        std::lock_guard<std::mutex> lock(pp.mutex);

        // Use the correct produce method signature for librdkafka
        // RK_MSG_COPY means librdkafka copies the payload, so we can return immediately
        err = pp.producer->produce(
            topic_,                           // topic name
            RdKafka::Topic::PARTITION_UA,     // partition (unassigned - hash by key)
            RdKafka::Producer::RK_MSG_COPY,   // IMPORTANT: copy payload immediately
            const_cast<char*>(message.data()), message.size(), // payload
            key.empty() ? nullptr : key.data(), key.size(), // key for partitioning
            0,                               // timestamp (0 = now)
            nullptr                           // opaque pointer
        );
    } // Mutex released immediately after produce()

    if (err == RdKafka::ERR_NO_ERROR) {
        pp.message_count++;
        total_message_count_++;
        // DON'T CALL POLL HERE - let background delivery thread handle callbacks
        // Polling in hot path causes latency spikes
    } else if (err == RdKafka::ERR__QUEUE_FULL) {
        // Queue is full - this is rare. Try once more with a brief wait
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        {
            std::lock_guard<std::mutex> lock(pp.mutex);
            err = pp.producer->produce(
                topic_,
                RdKafka::Topic::PARTITION_UA,
                RdKafka::Producer::RK_MSG_COPY,
                const_cast<char*>(message.data()), message.size(),
                key.empty() ? nullptr : key.data(), key.size(),
                0,
                nullptr
            );
        }

        if (err == RdKafka::ERR_NO_ERROR) {
            pp.message_count++;
            total_message_count_++;
        } else {
            std::cerr << "Failed to produce message after retry: " << RdKafka::err2str(err) << "\n";
        }
    } else {
        std::cerr << "Failed to produce message: " << RdKafka::err2str(err) << "\n";
    }

    return err;
}

void KafkaProducer::poll_loop(size_t partition_idx) {
    // OPTIMIZED: Per-partition polling thread
    // Each producer polls independently, no contention between partitions
    auto& pp = *partition_producers_[partition_idx];

    while (pp.poll_running) {
        {
            std::lock_guard<std::mutex> lock(pp.mutex);
            if (pp.producer) {
                // Blocking poll for 100ms - handles callbacks efficiently
                // librdkafka wakes up when there are callbacks or timeout expires
                pp.producer->poll(100);
            }
        }

        // No sleep needed - poll() blocks for us
        // This reduces mutex lock/unlock frequency from 1000/sec to ~10/sec
    }
}

RdKafka::ErrorCode KafkaProducer::flush(std::chrono::milliseconds timeout) {
    // Flush all partition producers in parallel
    RdKafka::ErrorCode final_err = RdKafka::ERR_NO_ERROR;

    for (auto& pp : partition_producers_) {
        std::lock_guard<std::mutex> lock(pp->mutex);

        if (!pp->producer) {
            continue;
        }

        RdKafka::ErrorCode err = pp->producer->flush(timeout.count());
        if (err != RdKafka::ERR_NO_ERROR) {
            std::cerr << "Failed to flush producer: " << RdKafka::err2str(err) << "\n";
            final_err = err;  // Track last error
        }
    }

    return final_err;
}
