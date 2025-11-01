#pragma once

#include "thread_pool.h"
#include <sys/epoll.h>
#include <functional>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace metricstream {

// Connection state for each client socket
struct Connection {
    int fd;
    std::string read_buffer;
    std::string write_buffer;
    bool keep_alive;

    Connection(int socket_fd)
        : fd(socket_fd), keep_alive(false) {}
};

class EventLoop {
public:
    using RequestHandler = std::function<void(int client_fd, const std::string& request_data)>;

    explicit EventLoop(size_t thread_pool_size = 16);
    ~EventLoop();

    // Start the event loop with the given listen socket
    void run(int listen_fd, RequestHandler handler);

    // Stop the event loop
    void stop();

    // Get statistics
    size_t active_connections() const;

private:
    // Core event loop
    void event_loop(int listen_fd);

    // Handle new connection
    void handle_accept(int listen_fd);

    // Handle client data ready to read
    void handle_read(int client_fd);

    // Handle client ready for write
    void handle_write(int client_fd);

    // Close and cleanup connection
    void close_connection(int client_fd);

    // Add socket to epoll monitoring
    bool add_to_epoll(int fd, uint32_t events);

    // Modify epoll events for socket
    bool modify_epoll(int fd, uint32_t events);

    // Remove socket from epoll
    void remove_from_epoll(int fd);

    // Set socket to non-blocking mode
    static bool set_nonblocking(int fd);

    // epoll file descriptor
    int epoll_fd_;

    // Thread pool for CPU-bound work (parsing, validation)
    std::unique_ptr<ThreadPool> thread_pool_;

    // Connection state tracking
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
    mutable std::mutex connections_mutex_;

    // Request handler callback
    RequestHandler request_handler_;

    // Running state
    std::atomic<bool> running_;

    // Constants
    static constexpr int MAX_EVENTS = 1024;
    static constexpr int READ_BUFFER_SIZE = 4096;
};

} // namespace metricstream
