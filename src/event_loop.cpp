#include "event_loop.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>

namespace metricstream {

EventLoop::EventLoop(size_t thread_pool_size)
    : epoll_fd_(-1), running_(false) {
    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    // Initialize thread pool for CPU-bound work
    thread_pool_ = std::make_unique<ThreadPool>(thread_pool_size);
}

EventLoop::~EventLoop() {
    stop();
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
}

void EventLoop::run(int listen_fd, RequestHandler handler) {
    if (running_.load()) {
        return;
    }

    request_handler_ = std::move(handler);
    running_ = true;

    // Set listen socket to non-blocking
    if (!set_nonblocking(listen_fd)) {
        std::cerr << "Failed to set listen socket non-blocking" << std::endl;
        running_ = false;
        return;
    }

    // Add listen socket to epoll
    if (!add_to_epoll(listen_fd, EPOLLIN)) {
        std::cerr << "Failed to add listen socket to epoll" << std::endl;
        running_ = false;
        return;
    }

    std::cout << "Event loop started with epoll (Phase 8)" << std::endl;

    event_loop(listen_fd);
}

void EventLoop::stop() {
    running_ = false;

    // Close all active connections
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& [fd, conn] : connections_) {
        close(fd);
    }
    connections_.clear();
}

void EventLoop::event_loop(int listen_fd) {
    struct epoll_event events[MAX_EVENTS];

    while (running_.load()) {
        // Wait for events (timeout: 100ms to allow graceful shutdown)
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);

        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
            break;
        }

        // Process all ready events
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            // Listen socket - new connection
            if (fd == listen_fd) {
                handle_accept(listen_fd);
            }
            // Error or hangup
            else if (ev & (EPOLLERR | EPOLLHUP)) {
                close_connection(fd);
            }
            // Ready to read
            else if (ev & EPOLLIN) {
                handle_read(fd);
            }
            // Ready to write
            else if (ev & EPOLLOUT) {
                handle_write(fd);
            }
        }
    }

    std::cout << "Event loop stopped" << std::endl;
}

void EventLoop::handle_accept(int listen_fd) {
    // Accept all pending connections (edge-triggered semantics)
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more connections to accept
                break;
            } else {
                std::cerr << "Accept failed: " << strerror(errno) << std::endl;
                break;
            }
        }

        // Set client socket to non-blocking
        if (!set_nonblocking(client_fd)) {
            std::cerr << "Failed to set client socket non-blocking" << std::endl;
            close(client_fd);
            continue;
        }

        // Add to epoll for reading (edge-triggered for efficiency)
        if (!add_to_epoll(client_fd, EPOLLIN | EPOLLET)) {
            std::cerr << "Failed to add client to epoll" << std::endl;
            close(client_fd);
            continue;
        }

        // Track connection state
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[client_fd] = std::make_unique<Connection>(client_fd);
        }
    }
}

void EventLoop::handle_read(int client_fd) {
    Connection* conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it == connections_.end()) {
            return; // Connection already closed
        }
        conn = it->second.get();
    }

    // Read all available data (edge-triggered requires draining)
    while (true) {
        char buffer[READ_BUFFER_SIZE];
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            conn->read_buffer.append(buffer, bytes_read);
        } else if (bytes_read == 0) {
            // Connection closed by client
            close_connection(client_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available
                break;
            } else {
                // Read error
                std::cerr << "Read error: " << strerror(errno) << std::endl;
                close_connection(client_fd);
                return;
            }
        }
    }

    // Check if we have a complete HTTP request
    // HTTP requests have headers ending with \r\n\r\n
    size_t header_end = conn->read_buffer.find("\r\n\r\n");

    if (header_end == std::string::npos) {
        // Incomplete request - wait for more data
        // Don't clear buffer, keep accumulating
        return;
    }

    // We have complete headers. Check if there's a body.
    size_t content_length = 0;
    size_t cl_pos = conn->read_buffer.find("Content-Length:");
    if (cl_pos != std::string::npos && cl_pos < header_end) {
        // Parse Content-Length value
        size_t value_start = cl_pos + 15; // Length of "Content-Length:"
        while (value_start < header_end && conn->read_buffer[value_start] == ' ') {
            value_start++;
        }
        size_t value_end = conn->read_buffer.find("\r\n", value_start);
        if (value_end != std::string::npos) {
            std::string cl_str = conn->read_buffer.substr(value_start, value_end - value_start);
            try {
                content_length = std::stoull(cl_str);
            } catch (...) {
                // Invalid Content-Length, close connection
                close_connection(client_fd);
                return;
            }
        }
    }

    // Calculate expected total request size
    size_t body_start = header_end + 4; // After \r\n\r\n
    size_t expected_size = body_start + content_length;

    if (conn->read_buffer.size() < expected_size) {
        // Incomplete body - wait for more data
        return;
    }

    // We have a complete request!
    std::string request_data = conn->read_buffer.substr(0, expected_size);

    // Check for Connection: keep-alive header
    conn->keep_alive = (conn->read_buffer.find("Connection: keep-alive") != std::string::npos);

    // Remove processed request from buffer (might have pipelined requests)
    conn->read_buffer.erase(0, expected_size);

    // Delegate CPU-bound work (parsing, validation) to thread pool
    thread_pool_->enqueue([this, client_fd, request_data]() {
        if (request_handler_) {
            request_handler_(client_fd, request_data);
        }
    });
}

void EventLoop::handle_write(int client_fd) {
    Connection* conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it == connections_.end()) {
            return;
        }
        conn = it->second.get();
    }

    // Write buffered data
    while (!conn->write_buffer.empty()) {
        ssize_t bytes_written = write(client_fd,
                                      conn->write_buffer.c_str(),
                                      conn->write_buffer.size());

        if (bytes_written > 0) {
            conn->write_buffer.erase(0, bytes_written);
        } else if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, wait for next EPOLLOUT
                return;
            } else {
                // Write error
                std::cerr << "Write error: " << strerror(errno) << std::endl;
                close_connection(client_fd);
                return;
            }
        }
    }

    // All data written, check keep-alive
    if (conn->keep_alive) {
        // Switch back to reading mode
        modify_epoll(client_fd, EPOLLIN | EPOLLET);
    } else {
        // Close connection
        close_connection(client_fd);
    }
}

void EventLoop::close_connection(int client_fd) {
    remove_from_epoll(client_fd);
    close(client_fd);

    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(client_fd);
}

bool EventLoop::add_to_epoll(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cerr << "epoll_ctl ADD failed: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool EventLoop::modify_epoll(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        std::cerr << "epoll_ctl MOD failed: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void EventLoop::remove_from_epoll(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

bool EventLoop::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "fcntl F_GETFL failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "fcntl F_SETFL failed: " << strerror(errno) << std::endl;
        return false;
    }

    return true;
}

size_t EventLoop::active_connections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

} // namespace metricstream
