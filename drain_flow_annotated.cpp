// ANNOTATED CODE: Where the "Draining" Happens
// This is from your actual MetricStream http_server.cpp

void HttpServer::run_server() {
    // 1. CREATE LISTENING SOCKET
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // 2. BIND TO PORT 8080
    bind(server_fd, ...);

    // 3. START LISTENING - CREATE THE KERNEL QUEUE
    //    This tells the kernel: "I'm ready to accept connections"
    //    Kernel creates a queue that can hold up to 1024 pending connections
    listen(server_fd, 1024);  // backlog = 1024

    // KERNEL QUEUE NOW EXISTS:
    //   ┌────────────────────────────────────┐
    //   │  Kernel Listen Queue (max 1024)   │
    //   │  [empty] [empty] [empty] ...       │
    //   └────────────────────────────────────┘


    // 4. ACCEPT LOOP - THIS IS WHERE DRAINING HAPPENS!
    //    This is a DEDICATED THREAD that ONLY does accept()
    while (running_.load()) {

        // ═══════════════════════════════════════════════════════════
        //                    THE DRAIN POINT
        // ═══════════════════════════════════════════════════════════

        // This line removes ONE connection from kernel queue
        // and gives us a file descriptor to control it
        int client_socket = accept(server_fd, ...);

        // WHAT JUST HAPPENED:
        //
        // BEFORE accept():
        //   Kernel Queue: [C1] [C2] [C3] ... (C1 waiting)
        //   Our FDs:      (none)
        //
        // AFTER accept():
        //   Kernel Queue: [C2] [C3] ...      (C1 removed! ← "drained")
        //   Our FDs:      fd=5 → C1          (C1 now ours! ← "drained to")
        //

        // 5. IMMEDIATELY DELEGATE TO THREAD POOL
        //    This is KEY to fast draining - we don't do ANY work here!
        //    Just accept() and hand off. Loop back immediately to drain more!

        thread_pool_->enqueue([this, client_socket]() {
            // ═══════════════════════════════════════════════════════════
            //           WORKER THREAD PROCESSES CONNECTION
            //    This happens IN PARALLEL while accept loop keeps draining!
            // ═══════════════════════════════════════════════════════════

            // Set timeout
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, ...);

            bool keep_alive = true;
            while (keep_alive) {
                // Read HTTP request
                char buffer[4096];
                ssize_t bytes = read(client_socket, buffer, sizeof(buffer));

                // Parse request
                HttpRequest request = parse_request(buffer);

                // Rate limiting
                if (!rate_limiter.allow(client_id)) {
                    send_429_response(client_socket);
                    break;
                }

                // Handle request
                HttpResponse response = handler(request);

                // Send response
                write(client_socket, response.data(), response.size());

                // Check if client wants keep-alive
                keep_alive = request.has_header("Connection", "keep-alive");
            }

            // Close connection
            close(client_socket);

            // Worker returns to thread pool, ready for next task
        });

        // ← LOOP BACK IMMEDIATELY! Don't wait for worker to finish!
        //   This is why we drain so fast - accept loop never blocks
    }
}


// ═══════════════════════════════════════════════════════════════════
//                           TIMELINE VIEW
// ═══════════════════════════════════════════════════════════════════

// Time 0.0ms:  Accept loop: accept() C1 → enqueue → LOOP BACK
//              Worker 1:    [starts processing C1]
//              Kernel queue: [C2] [C3] [C4] ...
//
// Time 0.1ms:  Accept loop: accept() C2 → enqueue → LOOP BACK
//              Worker 1:    [still processing C1]
//              Worker 2:    [starts processing C2]
//              Kernel queue: [C3] [C4] ...
//
// Time 0.2ms:  Accept loop: accept() C3 → enqueue → LOOP BACK
//              Worker 1:    [still processing C1]
//              Worker 2:    [still processing C2]
//              Worker 3:    [starts processing C3]
//              Kernel queue: [C4] ...
//
// ← Accept loop drained 3 connections in 0.2ms!
// ← Workers process them in parallel (takes ~5ms each)
// ← Kernel queue stays empty or nearly empty


// ═══════════════════════════════════════════════════════════════════
//                         WHAT IF WE DIDN'T USE THREAD POOL?
// ═══════════════════════════════════════════════════════════════════

void HttpServer::run_server_OLD_WAY() {
    listen(server_fd, 128);

    while (running_) {
        // Accept connection
        int client_socket = accept(server_fd, ...);

        // Process it RIGHT HERE (blocks accept loop!)
        char buffer[4096];
        read(client_socket, buffer, sizeof(buffer));  // ← BLOCKS here

        HttpRequest request = parse_request(buffer);   // ← BLOCKS here

        if (!rate_limiter.allow(client_id)) {          // ← BLOCKS here
            send_429_response(client_socket);
        }

        HttpResponse response = handler(request);      // ← BLOCKS here

        write(client_socket, response.data(), ...);    // ← BLOCKS here

        close(client_socket);

        // ← NOW we loop back to accept() - 5ms later!
        //   Meanwhile, 100 connections piled up in kernel queue
    }

    // DRAIN RATE: ~200 connections/second
    // If clients arrive faster than 200/sec → queue fills → refused connections
}


// ═══════════════════════════════════════════════════════════════════
//                              SUMMARY
// ═══════════════════════════════════════════════════════════════════

/*
    DRAINING = Moving connections from kernel queue to application control

    FROM:  Kernel listen queue (limited, backlog=1024)
    TO:    Thread pool task queue (dynamic, unlimited*)

    HOW:   accept() system call
           - Removes entry from kernel queue
           - Creates file descriptor in app
           - App now owns the connection

    WHY FAST DRAINING MATTERS:
           - Kernel queue has fixed limit (1024)
           - If full, new connections refused
           - Fast accept() → queue stays empty → no refusals

    THE TRICK:
           - Accept loop does ONLY accept()
           - Immediately delegates to thread pool
           - Loops back in ~0.1ms
           - Can drain 10,000+ connections/second
           - Workers process in parallel (8 at a time)
*/
