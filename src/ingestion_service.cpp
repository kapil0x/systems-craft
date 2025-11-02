#include "ingestion_service.h"
#include <iostream>
#include <thread>
#include <cmath>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <vector>

namespace metricstream {

// TODO(human): Implement sliding window rate limiting algorithm
// Consider using a token bucket or sliding window approach
// Store client request counts with timestamps

//

RateLimiter::RateLimiter(size_t max_requests_per_second) 
    : max_requests_(max_requests_per_second) {
}

// Hash-based per-client mutex selection (Phase 4 optimization)
std::mutex& RateLimiter::get_client_mutex(const std::string& client_id) {
    std::hash<std::string> hasher;
    size_t hash_value = hasher(client_id);
    size_t mutex_index = hash_value % MUTEX_POOL_SIZE;
    return client_mutex_pool_[mutex_index];
}

bool RateLimiter::allow_request(const std::string& client_id) {
    auto function_start = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::steady_clock::now();

    // PHASE 5 OPTIMIZATION: Lock-free ring buffer for metrics collection
    // Mutex only protects rate limiting state, not metrics (which use atomics)
    bool decision;

    auto lock_start = std::chrono::high_resolution_clock::now();
    {
        std::mutex& client_lock = get_client_mutex(client_id);
        std::lock_guard<std::mutex> lock(client_lock);
        auto lock_acquired = std::chrono::high_resolution_clock::now();

        // Rate limiting logic - under per-client lock
        auto& client_queue = client_requests_[client_id];

        // Remove old timestamps (older than 1 second)
        auto cleanup_start = std::chrono::high_resolution_clock::now();
        size_t removed_count = 0;
        while (!client_queue.empty()) {
            auto oldest_timestamp = client_queue.front();
            auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - oldest_timestamp);

            if (time_diff.count() >= 1) {
                client_queue.pop_front();
                removed_count++;
            } else {
                break;
            }
        }
        auto cleanup_end = std::chrono::high_resolution_clock::now();

        auto decision_start = std::chrono::high_resolution_clock::now();
        if(client_queue.size() < max_requests_){
            client_queue.push_back(now);
            decision = true;
        } else {
            decision = false;
        }
        auto decision_end = std::chrono::high_resolution_clock::now();

        // Profile logging (every 1000 requests)
        static std::atomic<int> profile_counter{0};
        if (profile_counter.fetch_add(1) % 1000 == 0) {
            auto wait_time = std::chrono::duration_cast<std::chrono::microseconds>(
                lock_acquired - lock_start).count();
            auto cleanup_time = std::chrono::duration_cast<std::chrono::microseconds>(
                cleanup_end - cleanup_start).count();
            auto decision_time = std::chrono::duration_cast<std::chrono::microseconds>(
                decision_end - decision_start).count();
            auto total_lock_time = std::chrono::duration_cast<std::chrono::microseconds>(
                decision_end - lock_acquired).count();

            std::cerr << "[PROFILE] Wait: " << wait_time << "μs | "
                      << "Cleanup: " << cleanup_time << "μs (" << removed_count << " items) | "
                      << "Decision: " << decision_time << "μs | "
                      << "Total-in-lock: " << total_lock_time << "μs | "
                      << "Queue-size: " << client_queue.size() << "\n";
        }
    } // Lock released here

    // LOCK-FREE metrics collection using atomic ring buffer
    // Single-writer (this thread) / single-reader (flush thread) pattern
    auto& metrics = client_metrics_[client_id];
    size_t write_idx = metrics.write_index.load(std::memory_order_relaxed);

    // Write event to ring buffer
    metrics.ring_buffer[write_idx % ClientMetrics::BUFFER_SIZE] = MetricEvent{now, decision};

    // Publish write with release semantics - ensures buffer write visible before index update
    metrics.write_index.store(write_idx + 1, std::memory_order_release);

    return decision;
}

void RateLimiter::flush_metrics() {
    // PHASE 5 OPTIMIZATION: Lock-free metrics reading
    // No longer need complex lock ordering since metrics use atomic ring buffer
    // Only client_metrics_ map iteration needs protection (for concurrent insertions)

    // Get snapshot of current clients
    std::vector<std::string> client_ids;
    {
        static std::mutex client_list_mutex;
        std::lock_guard<std::mutex> list_lock(client_list_mutex);

        client_ids.reserve(client_metrics_.size());
        for (const auto& [client_id, _] : client_metrics_) {
            client_ids.push_back(client_id);
        }
    }

    // Process each client's metrics using lock-free reads
    for (const auto& client_id : client_ids) {
        auto it = client_metrics_.find(client_id);
        if (it == client_metrics_.end()) {
            continue;
        }

        auto& metrics = it->second;

        // LOCK-FREE READ: Use acquire semantics to see all writes before write_index update
        size_t read_idx = metrics.read_index.load(std::memory_order_acquire);
        size_t write_idx = metrics.write_index.load(std::memory_order_acquire);

        // Process all events between read and write indices
        size_t events_processed = 0;
        for (size_t i = read_idx; i < write_idx; ++i) {
            const auto& event = metrics.ring_buffer[i % ClientMetrics::BUFFER_SIZE];

            // Send to monitoring (I/O operation outside any critical section)
            send_to_monitoring(client_id, event.timestamp, event.allowed);
            events_processed++;
        }

        // Update read index with release semantics to mark events as processed
        if (events_processed > 0) {
            metrics.read_index.store(write_idx, std::memory_order_release);
        }
    }
}

void RateLimiter::send_to_monitoring(const std::string& client_id,
                                   const std::chrono::time_point<std::chrono::steady_clock>& timestamp,
                                   bool allowed) {
    // Convert timestamp to milliseconds since epoch for monitoring
    auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    
    // In production, this would send to your monitoring system (Prometheus, DataDog, etc.)
    std::cout << "[METRICS] client=" << client_id 
              << " timestamp=" << ms_since_epoch 
              << " allowed=" << (allowed ? "true" : "false") << std::endl;
}

MetricValidator::ValidationResult MetricValidator::validate_metric(const Metric& metric) const {
    ValidationResult result;
    result.valid = true;
    
    if (metric.name.empty()) {
        result.valid = false;
        result.error_message = "Metric name cannot be empty";
        return result;
    }
    
    if (metric.name.length() > 255) {
        result.valid = false;
        result.error_message = "Metric name too long (max 255 characters)";
        return result;
    }
    
    if (std::isnan(metric.value) || std::isinf(metric.value)) {
        result.valid = false;
        result.error_message = "Metric value must be a finite number";
        return result;
    }
    
    return result;
}

MetricValidator::ValidationResult MetricValidator::validate_batch(const MetricBatch& batch) const {
    ValidationResult result;
    result.valid = true;
    
    if (batch.empty()) {
        result.valid = false;
        result.error_message = "Batch cannot be empty";
        return result;
    }
    
    if (batch.size() > 1000) {
        result.valid = false;
        result.error_message = "Batch size exceeds maximum (1000 metrics)";
        return result;
    }
    
    for (const auto& metric : batch.metrics) {
        ValidationResult metric_result = validate_metric(metric);
        if (!metric_result.valid) {
            result.valid = false;
            result.error_message = "Invalid metric: " + metric_result.error_message;
            return result;
        }
    }
    
    return result;
}

IngestionService::IngestionService(int port, size_t rate_limit, int num_partitions,
                                 QueueMode mode, const std::string& kafka_brokers)
    : metrics_received_(0), batches_processed_(0), validation_errors_(0), rate_limited_(0),
      queue_mode_(mode) {
    
    server_ = std::make_unique<HttpServer>(port);
    validator_ = std::make_unique<MetricValidator>();
    rate_limiter_ = std::make_unique<RateLimiter>(rate_limit);

    // Initialize the appropriate queue based on mode
    if (queue_mode_ == QueueMode::FILE_BASED) {
        file_queue_ = std::make_unique<PartitionedQueue>("queue", num_partitions);
        std::cout << "Initialized file-based partitioned queue with " << num_partitions << " partitions\n";
    } else if (queue_mode_ == QueueMode::KAFKA) {
        kafka_producer_ = std::make_unique<KafkaProducer>(kafka_brokers, "metrics");
        std::cout << "Initialized Kafka producer: brokers=" << kafka_brokers << ", topic=metrics\n";
    }

    // Start async writer thread
    writer_thread_ = std::thread(&IngestionService::async_writer_loop, this);
    
    // Register HTTP endpoints
    server_->add_handler("/metrics", "POST", 
        [this](const HttpRequest& req) { return handle_metrics_post(req); });
    server_->add_handler("/health", "GET", 
        [this](const HttpRequest& req) { return handle_health_check(req); });
    server_->add_handler("/metrics", "GET", 
        [this](const HttpRequest& req) { return handle_metrics_get(req); });
}

IngestionService::~IngestionService() {
    stop();
    
    // Shutdown async writer thread
    writer_running_ = false;
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    
    if (metrics_file_.is_open()) {
        metrics_file_.close();
    }
}

void IngestionService::start() {
    server_->start();
    std::cout << "Ingestion service started" << std::endl;
}

void IngestionService::stop() {
    if (server_) {
        server_->stop();
    }
    std::cout << "Ingestion service stopped" << std::endl;
}

HttpResponse IngestionService::handle_metrics_post(const HttpRequest& request) {
    HttpResponse response;
    response.set_json_content();
    
    // Extract client ID from headers or use IP as fallback
    std::string client_id = "default";
    auto auth_header = request.headers.find("Authorization");
    if (auth_header != request.headers.end()) {
        client_id = auth_header->second;
    }
    
    // Check rate limiting
    if (!rate_limiter_->allow_request(client_id)) {
        rate_limited_++;
        response.status_code = 429;
        response.body = create_error_response("Rate limit exceeded");
        return response;
    }
    
    try {
        MetricBatch batch = parse_json_metrics_optimized(request.body);
        
        auto validation_result = validator_->validate_batch(batch);
        if (!validation_result.valid) {
            validation_errors_++;
            response.status_code = 400;
            response.body = create_error_response(validation_result.error_message);
            return response;
        }
        
        metrics_received_ += batch.size();
        batches_processed_++;
        
        // Queue metrics for asynchronous writing to queue
        queue_metrics_for_async_write(batch, client_id);
        
        response.body = create_success_response(batch.size());
        
    } catch (const std::exception& e) {
        validation_errors_++;
        response.status_code = 400;
        response.body = create_error_response("Invalid JSON: " + std::string(e.what()));
    }
    
    return response;
}

HttpResponse IngestionService::handle_health_check(const HttpRequest& request) {
    HttpResponse response;
    response.set_json_content();
    response.body = R"({"status":"healthy","service":"ingestion"})";
    return response;
}

HttpResponse IngestionService::handle_metrics_get(const HttpRequest& request) {
    HttpResponse response;
    response.set_json_content();
    
    // Return service statistics
    response.body = "{"
        "\"metrics_received\":" + std::to_string(metrics_received_) + ","
        "\"batches_processed\":" + std::to_string(batches_processed_) + ","
        "\"validation_errors\":" + std::to_string(validation_errors_) + ","
        "\"rate_limited_requests\":" + std::to_string(rate_limited_) +
        "}";
    
    return response;
}

MetricBatch IngestionService::parse_json_metrics_optimized(const std::string& json_body) {
    MetricBatch batch;
    
    enum class ParseState {
        LOOKING_FOR_METRICS,
        IN_METRICS_ARRAY,
        IN_METRIC_OBJECT,
        PARSING_FIELD_NAME,
        PARSING_STRING_VALUE,
        PARSING_NUMBER_VALUE,
        IN_TAGS_OBJECT,
        DONE
    };
    
    ParseState state = ParseState::LOOKING_FOR_METRICS;
    size_t i = 0;
    const size_t len = json_body.length();
    
    // Pre-allocated buffers to avoid string allocations
    std::string current_field;
    std::string current_value;
    current_field.reserve(32);
    current_value.reserve(128);
    
    // Current metric being parsed
    std::string metric_name, metric_type = "gauge";
    double metric_value = 0.0;
    Tags metric_tags;
    metric_name.reserve(64);
    metric_type.reserve(16);
    
    auto skip_whitespace = [&]() {
        while (i < len && std::isspace(json_body[i])) i++;
    };
    
    auto parse_string = [&](std::string& result) {
        result.clear();
        if (i >= len || json_body[i] != '"') return false;
        i++; // skip opening quote
        
        while (i < len && json_body[i] != '"') {
            if (json_body[i] == '\\' && i + 1 < len) {
                i++; // skip escape char
                if (json_body[i] == 'n') result += '\n';
                else if (json_body[i] == 't') result += '\t';
                else if (json_body[i] == 'r') result += '\r';
                else result += json_body[i];
            } else {
                result += json_body[i];
            }
            i++;
        }
        
        if (i < len && json_body[i] == '"') {
            i++; // skip closing quote
            return true;
        }
        return false;
    };
    
    auto parse_number = [&]() -> double {
        size_t start = i;
        if (i < len && json_body[i] == '-') i++;
        while (i < len && (std::isdigit(json_body[i]) || json_body[i] == '.')) i++;
        
        if (start == i) return 0.0;
        
        // Use string_view equivalent to avoid allocation
        const char* start_ptr = json_body.data() + start;
        const char* end_ptr = json_body.data() + i;
        return std::strtod(start_ptr, const_cast<char**>(&end_ptr));
    };
    
    while (i < len && state != ParseState::DONE) {
        skip_whitespace();
        if (i >= len) break;
        
        char c = json_body[i];
        
        switch (state) {
            case ParseState::LOOKING_FOR_METRICS:
                if (c == '"') {
                    if (parse_string(current_field) && current_field == "metrics") {
                        skip_whitespace();
                        if (i < len && json_body[i] == ':') {
                            i++;
                            skip_whitespace();
                            if (i < len && json_body[i] == '[') {
                                i++;
                                state = ParseState::IN_METRICS_ARRAY;
                            }
                        }
                    }
                } else {
                    i++;
                }
                break;
                
            case ParseState::IN_METRICS_ARRAY:
                if (c == '{') {
                    i++;
                    state = ParseState::IN_METRIC_OBJECT;
                    // Reset metric data
                    metric_name.clear();
                    metric_type = "gauge";
                    metric_value = 0.0;
                    metric_tags.clear();
                } else if (c == ']') {
                    state = ParseState::DONE;
                } else {
                    i++;
                }
                break;
                
            case ParseState::IN_METRIC_OBJECT:
                if (c == '"') {
                    if (parse_string(current_field)) {
                        skip_whitespace();
                        if (i < len && json_body[i] == ':') {
                            i++;
                            skip_whitespace();
                            
                            if (current_field == "name" || current_field == "type") {
                                if (parse_string(current_value)) {
                                    if (current_field == "name") {
                                        metric_name = std::move(current_value);
                                    } else {
                                        metric_type = std::move(current_value);
                                    }
                                }
                            } else if (current_field == "value") {
                                metric_value = parse_number();
                            } else if (current_field == "tags" && i < len && json_body[i] == '{') {
                                i++;
                                state = ParseState::IN_TAGS_OBJECT;
                            }
                        }
                    }
                } else if (c == '}') {
                    // Finished parsing metric object
                    if (!metric_name.empty()) {
                        MetricType type = MetricType::GAUGE;
                        if (metric_type == "counter") type = MetricType::COUNTER;
                        else if (metric_type == "histogram") type = MetricType::HISTOGRAM;
                        else if (metric_type == "summary") type = MetricType::SUMMARY;
                        
                        batch.add_metric(Metric(std::move(metric_name), metric_value, type, std::move(metric_tags)));
                    }
                    i++;
                    state = ParseState::IN_METRICS_ARRAY;
                } else {
                    i++;
                }
                break;
                
            case ParseState::IN_TAGS_OBJECT:
                if (c == '"') {
                    if (parse_string(current_field)) {
                        skip_whitespace();
                        if (i < len && json_body[i] == ':') {
                            i++;
                            skip_whitespace();
                            if (parse_string(current_value)) {
                                metric_tags[std::move(current_field)] = std::move(current_value);
                            }
                        }
                    }
                } else if (c == '}') {
                    i++;
                    state = ParseState::IN_METRIC_OBJECT;
                } else {
                    i++;
                }
                break;
                
            default:
                i++;
                break;
        }
    }
    
    return batch;
}

MetricBatch IngestionService::parse_json_metrics(const std::string& json_body) {
    MetricBatch batch;
    
    try {
        // Simple JSON parsing for metrics array
        // Expected format: {"metrics": [{"name": "cpu_usage", "value": 75.5, "type": "gauge", "tags": {"host": "server1"}}]}
        
        size_t metrics_pos = json_body.find("\"metrics\"");
        if (metrics_pos == std::string::npos) {
            throw std::runtime_error("Missing 'metrics' field");
        }
        
        size_t array_start = json_body.find("[", metrics_pos);
        size_t array_end = json_body.find("]", array_start);
        if (array_start == std::string::npos || array_end == std::string::npos) {
            throw std::runtime_error("Invalid metrics array format");
        }
        
        std::string metrics_array = json_body.substr(array_start + 1, array_end - array_start - 1);
        
        // Parse individual metric objects
        size_t pos = 0;
        while (pos < metrics_array.length()) {
            size_t obj_start = metrics_array.find("{", pos);
            if (obj_start == std::string::npos) break;
            
            size_t obj_end = metrics_array.find("}", obj_start);
            if (obj_end == std::string::npos) break;
            
            std::string metric_obj = metrics_array.substr(obj_start, obj_end - obj_start + 1);
            
            Metric metric = parse_single_metric(metric_obj);
            batch.add_metric(std::move(metric));
            
            pos = obj_end + 1;
        }
        
    } catch (const std::exception& e) {
        throw std::runtime_error("JSON parsing error: " + std::string(e.what()));
    }
    
    return batch;
}

Metric IngestionService::parse_single_metric(const std::string& metric_json) {
    // Extract name
    std::string name = extract_string_field(metric_json, "name");
    
    // Extract value
    double value = extract_numeric_field(metric_json, "value");
    
    // Extract type (default to GAUGE)
    std::string type_str = extract_string_field(metric_json, "type");
    MetricType type = MetricType::GAUGE;
    if (type_str == "counter") type = MetricType::COUNTER;
    else if (type_str == "histogram") type = MetricType::HISTOGRAM;
    else if (type_str == "summary") type = MetricType::SUMMARY;
    
    // Extract tags (simple key-value parsing)
    Tags tags = extract_tags(metric_json);
    
    return Metric(name, value, type, tags);
}

std::string IngestionService::extract_string_field(const std::string& json, const std::string& field) {
    std::string search_pattern = "\"" + field + "\"";
    size_t field_pos = json.find(search_pattern);
    if (field_pos == std::string::npos) {
        return "";
    }
    
    size_t colon_pos = json.find(":", field_pos);
    size_t quote_start = json.find("\"", colon_pos);
    size_t quote_end = json.find("\"", quote_start + 1);
    
    if (quote_start == std::string::npos || quote_end == std::string::npos) {
        return "";
    }
    
    return json.substr(quote_start + 1, quote_end - quote_start - 1);
}

double IngestionService::extract_numeric_field(const std::string& json, const std::string& field) {
    std::string search_pattern = "\"" + field + "\"";
    size_t field_pos = json.find(search_pattern);
    if (field_pos == std::string::npos) {
        throw std::runtime_error("Missing field: " + field);
    }
    
    size_t colon_pos = json.find(":", field_pos);
    size_t number_start = colon_pos + 1;
    
    // Skip whitespace
    while (number_start < json.length() && std::isspace(json[number_start])) {
        number_start++;
    }
    
    size_t number_end = number_start;
    while (number_end < json.length() && 
           (std::isdigit(json[number_end]) || json[number_end] == '.' || json[number_end] == '-')) {
        number_end++;
    }
    
    std::string number_str = json.substr(number_start, number_end - number_start);
    return std::stod(number_str);
}

Tags IngestionService::extract_tags(const std::string& json) {
    Tags tags;
    
    size_t tags_pos = json.find("\"tags\"");
    if (tags_pos == std::string::npos) {
        return tags; // No tags field
    }
    
    size_t obj_start = json.find("{", tags_pos);
    size_t obj_end = json.find("}", obj_start);
    
    if (obj_start == std::string::npos || obj_end == std::string::npos) {
        return tags;
    }
    
    std::string tags_obj = json.substr(obj_start + 1, obj_end - obj_start - 1);
    
    // Simple key-value parsing: "key1":"value1","key2":"value2"
    size_t pos = 0;
    while (pos < tags_obj.length()) {
        size_t key_start = tags_obj.find("\"", pos);
        if (key_start == std::string::npos) break;
        
        size_t key_end = tags_obj.find("\"", key_start + 1);
        size_t colon = tags_obj.find(":", key_end);
        size_t value_start = tags_obj.find("\"", colon);
        size_t value_end = tags_obj.find("\"", value_start + 1);
        
        if (key_end != std::string::npos && value_start != std::string::npos && value_end != std::string::npos) {
            std::string key = tags_obj.substr(key_start + 1, key_end - key_start - 1);
            std::string value = tags_obj.substr(value_start + 1, value_end - value_start - 1);
            tags[key] = value;
        }
        
        pos = value_end + 1;
    }
    
    return tags;
}

std::string IngestionService::serialize_metrics_batch_to_json(const MetricBatch& batch) {
    // Simple JSON serialization for learning - in production you'd use a proper JSON library
    std::string json = R"(
{
  "batch_timestamp": ")" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) + R"(",
  "metrics": [)";

    for (size_t i = 0; i < batch.metrics.size(); ++i) {
        const auto& metric = batch.metrics[i];
        json += "\n    {\n";
        json += "      \"name\": \"" + metric.name + "\",\n";
        json += "      \"value\": " + std::to_string(metric.value) + ",\n";

        std::string type_str;
        switch (metric.type) {
            case MetricType::COUNTER: type_str = "counter"; break;
            case MetricType::GAUGE: type_str = "gauge"; break;
            case MetricType::HISTOGRAM: type_str = "histogram"; break;
            case MetricType::SUMMARY: type_str = "summary"; break;
        }
        json += "      \"type\": \"" + type_str + "\"\n    }";

        if (i < batch.metrics.size() - 1) {
            json += ",";
        }
    }

    json += "\n  ]\n}\n";
    return json;
}

void IngestionService::store_metrics_to_queue(const MetricBatch& batch, const std::string& client_id) {
// Serialize entire batch as JSON message
std::string message = serialize_metrics_batch_to_json(batch);

try {
    if (queue_mode_ == QueueMode::FILE_BASED) {
        // Write to partitioned file queue
        auto [partition, offset] = file_queue_->produce(client_id, message);
    std::cout << "Queued metrics batch (file): partition=" << partition
              << ", offset=" << offset
              << ", client=" << client_id
          << ", metrics=" << batch.size() << "\n";

} else if (queue_mode_ == QueueMode::KAFKA) {
    // Write to Kafka
    RdKafka::ErrorCode err = kafka_producer_->produce(client_id, message);
    if (err != RdKafka::ERR_NO_ERROR) {
        throw std::runtime_error("Kafka produce failed: " + RdKafka::err2str(err));
    }
std::cout << "Queued metrics batch (kafka): client=" << client_id
          << ", metrics=" << batch.size() << "\n";
}

} catch (const std::exception& e) {
std::cerr << "Failed to write metrics batch to queue: " << e.what() << "\n";
// In production, you might want to retry or write to a fallback location
}
}

std::string IngestionService::create_error_response(const std::string& message) {
    return "{\"error\":\"" + message + "\"}";
}

std::string IngestionService::create_success_response(size_t metrics_count) {
    return "{\"success\":true,\"metrics_processed\":" + std::to_string(metrics_count) + "}";
}

void IngestionService::queue_metrics_for_async_write(const MetricBatch& batch, const std::string& client_id) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        write_queue_.push({batch, client_id});
    }
    queue_cv_.notify_one(); // Wake up writer thread
}

void IngestionService::async_writer_loop() {
    while (writer_running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Wait for batches to write or shutdown signal
        queue_cv_.wait(lock, [this] { 
            return !write_queue_.empty() || !writer_running_; 
        });
        
        // Process all pending batches
        while (!write_queue_.empty() && writer_running_) {
            auto [batch, client_id] = write_queue_.front();
            write_queue_.pop();

            // Release lock before expensive I/O operation
            lock.unlock();

            // Write batch to queue (file-based or Kafka)
            store_metrics_to_queue(batch, client_id);

            // Reacquire lock for next iteration
            lock.lock();
        }
    }
}

} // namespace metricstream