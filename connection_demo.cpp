#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

void demonstrate_connections() {
    std::cout << "=== TCP Connection Mechanics Demo ===\n\n";

    // Create listening socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9999);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));

    // Listen with small backlog to see queue behavior
    int backlog = 3;
    listen(server_fd, backlog);

    std::cout << "Server listening on port 9999 with backlog=" << backlog << "\n";
    std::cout << "This means the kernel can queue up to " << backlog
              << " pending connections\n\n";

    std::cout << "What's in the listen queue?\n";
    std::cout << "- Half-open connections (SYN received, SYN-ACK sent)\n";
    std::cout << "- Fully established connections waiting for accept()\n\n";

    std::cout << "Let's simulate what happens when clients connect...\n\n";

    // Simulate 5 client connections (more than backlog)
    std::cout << "Simulating 5 clients connecting to port 9999:\n";

    for (int i = 1; i <= 5; i++) {
        std::thread([i]() {
            int client_fd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(9999);
            inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

            std::cout << "Client " << i << ": Attempting to connect...\n";

            int result = connect(client_fd, (struct sockaddr*)&server_addr,
                               sizeof(server_addr));

            if (result == 0) {
                // Get the local port assigned to this client
                sockaddr_in local_addr{};
                socklen_t len = sizeof(local_addr);
                getsockname(client_fd, (struct sockaddr*)&local_addr, &len);

                std::cout << "Client " << i << ": Connected! "
                         << "Using local port " << ntohs(local_addr.sin_port)
                         << "\n";
                std::cout << "  → Connection 4-tuple: "
                         << "127.0.0.1:" << ntohs(local_addr.sin_port)
                         << " → 127.0.0.1:9999\n";

                // Keep connection alive
                std::this_thread::sleep_for(std::chrono::seconds(30));
            } else {
                std::cout << "Client " << i << ": Connection FAILED (queue full)\n";
            }

            close(client_fd);
        }).detach();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nWaiting 2 seconds before accepting connections...\n";
    std::cout << "(First " << backlog
              << " clients should succeed, rest should fail)\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n=== Now accepting connections from the queue ===\n\n";

    // Accept connections one by one
    for (int i = 1; i <= 3; i++) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);

        if (client_fd >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

            std::cout << "Accepted connection " << i << " from "
                     << client_ip << ":" << ntohs(client_addr.sin_port) << "\n";
            std::cout << "  → This connection was sitting in the listen queue!\n";
            std::cout << "  → Connection descriptor (fd): " << client_fd << "\n\n";

            close(client_fd);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close(server_fd);

    std::cout << "\n=== Key Insights ===\n";
    std::cout << "1. Each client gets a DIFFERENT source port (assigned by OS)\n";
    std::cout << "2. Server always uses port 9999, but each connection is unique\n";
    std::cout << "3. Listen queue stores PENDING connections (before accept())\n";
    std::cout << "4. Each accepted connection gets its own file descriptor\n";
    std::cout << "5. After accept(), data for each connection is independent\n";

    std::this_thread::sleep_for(std::chrono::seconds(1));
}

int main() {
    demonstrate_connections();
    return 0;
}
