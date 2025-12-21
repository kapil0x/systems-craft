#pragma once

#include "metric.h"
#include "http_server.h"
#include "partitioned_queue.h"
#include "kafka_producer.h"
#include <memory>
#include <atomic>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <mutex>
#include <array>
#include <atomic>
#include <fstream>
#include <queue>
#include <condition_variable>
#include <functional>
#include <thread>

namespace metricstream {

enum class QueueMode {
    FILE_BASED,  // Use partitioned file queue
    KAFKA        // Use Kafka message queue
};

class MetricValidator {
public:
    struct ValidationResult {
        bool valid;
        std::string error_message;
    };
    
    ValidationResult validate_metric(const Metric& metric) const;
    ValidationResult validate_batch(const MetricBatch& batch) const;
};

struct MetricEvent {
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
    bool allowed;
    
    MetricEvent() : timestamp{}, allowed(false) {}
    MetricEvent(std::chrono::time_point<std::chrono::steady_clock> ts, bool allow) 
        : timestamp(ts), allowed(allow) {}
};

struct ClientMetrics {
    static constexpr size_t BUFFER_SIZE = 1000;
    std::array<MetricEvent, BUFFER_SIZE> ring_buffer;
    std::atomic<size_t> write_index{0};
    std::atomic<size_t> read_index{0};
};

class RateLimiter {
public:
    RateLimiter(size_t max_requests_per_second);
    bool allow_request(const std::string& client_id);
    void flush_metrics();
    
private:
    size_t max_requests_;
    
    // Hash-based per-client mutex pool (Phase 4 optimization)
    static constexpr size_t MUTEX_POOL_SIZE = 10007;  // Prime number for better distribution
    std::array<std::mutex, MUTEX_POOL_SIZE> client_mutex_pool_;
    
    // Rate limiting data structures
    std::unordered_map<std::string, std::deque<std::chrono::time_point<std::chrono::steady_clock>>> client_requests_;
    
    // Metrics collection data structures  
    std::unordered_map<std::string, ClientMetrics> client_metrics_;
    
    // Helper method to get client-specific mutex
    std::mutex& get_client_mutex(const std::string& client_id);
    
    void send_to_monitoring(const std::string& client_id, 
                           const std::chrono::time_point<std::chrono::steady_clock>& timestamp, 
                           bool allowed);
};

class IngestionService {
public:
    IngestionService(int port, size_t rate_limit = 10000, int num_partitions = 4,
                     QueueMode mode = QueueMode::FILE_BASED,
                     const std::string& kafka_brokers = "localhost:9092");
    ~IngestionService();
    
    void start();
    void stop();
    
    // Metrics for monitoring
    size_t get_total_metrics_received() const { return metrics_received_; }
    size_t get_total_batches_processed() const { return batches_processed_; }
    size_t get_validation_errors() const { return validation_errors_; }
    size_t get_rate_limited_requests() const { return rate_limited_; }
    
private:
    std::unique_ptr<HttpServer> server_;
    std::unique_ptr<MetricValidator> validator_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    
    std::atomic<size_t> metrics_received_;
    std::atomic<size_t> batches_processed_;
    std::atomic<size_t> validation_errors_;
    std::atomic<size_t> rate_limited_;

    // Queue storage options
    std::unique_ptr<PartitionedQueue> file_queue_;  // File-based message queue
    std::unique_ptr<KafkaProducer> kafka_producer_; // Kafka producer
    QueueMode queue_mode_;                          // Which queue to use

    // Legacy file storage (for compatibility)
    std::ofstream metrics_file_;
    std::mutex file_mutex_;
    
    // Asynchronous batch writer infrastructure
    std::queue<std::pair<MetricBatch, std::string>> write_queue_;  // pair<batch, client_id>
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread writer_thread_;
    std::atomic<bool> writer_running_{true};
    
    // HTTP handlers
    HttpResponse handle_metrics_post(const HttpRequest& request);
    HttpResponse handle_health_check(const HttpRequest& request);
    HttpResponse handle_metrics_get(const HttpRequest& request);
    
    // Helper methods
    MetricBatch parse_json_metrics_optimized(const std::string& json_body);
    MetricBatch parse_json_metrics(const std::string& json_body);
    Metric parse_single_metric(const std::string& metric_json);
    std::string extract_string_field(const std::string& json, const std::string& field);
    double extract_numeric_field(const std::string& json, const std::string& field);
    Tags extract_tags(const std::string& json);
    void store_metrics_to_queue(const MetricBatch& batch, const std::string& client_id);
    void queue_metrics_for_async_write(const MetricBatch& batch, const std::string& client_id);
    void async_writer_loop();
    std::string serialize_metrics_batch_to_json(const MetricBatch& batch);
    std::string create_error_response(const std::string& message);
    std::string create_success_response(size_t metrics_count);
};

} // namespace metricstream