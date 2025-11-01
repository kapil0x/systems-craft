#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

// Shared partition assignment array
int partition_owners[4] = {-1, -1, -1, -1};

void claim_partition_thread(int thread_id, int partition) {
    // Thread version: works because shared memory
    partition_owners[partition] = thread_id;
    std::cout << "Thread " << thread_id << " claimed partition "
              << partition << std::endl;

    // Verify it stuck
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Thread " << thread_id << " verifies partition "
              << partition << " owner is: " << partition_owners[partition]
              << std::endl;
}

void claim_partition_process(int process_id, int partition) {
    // Process version: FAILS because isolated memory
    partition_owners[partition] = process_id;
    std::cout << "Process " << process_id << " claimed partition "
              << partition << " (in its own memory)" << std::endl;

    // Verify - but this is only visible to THIS process!
    sleep(1);
    std::cout << "Process " << process_id << " sees partition "
              << partition << " owner as: " << partition_owners[partition]
              << std::endl;
}

int main() {
    std::cout << "\n=== THREAD VERSION (WORKS) ===\n" << std::endl;

    // Reset
    memset(partition_owners, -1, sizeof(partition_owners));

    std::thread t0([](){ claim_partition_thread(0, 0); });
    std::thread t1([](){ claim_partition_thread(1, 1); });
    std::thread t2([](){ claim_partition_thread(2, 2); });

    t0.join();
    t1.join();
    t2.join();

    std::cout << "\nFinal state (main thread sees): ";
    for (int i = 0; i < 4; i++) {
        std::cout << "P" << i << "=" << partition_owners[i] << " ";
    }
    std::cout << "\n✅ All threads see the same memory!\n" << std::endl;

    std::cout << "\n=== PROCESS VERSION (FAILS) ===\n" << std::endl;

    // Reset
    memset(partition_owners, -1, sizeof(partition_owners));

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // Child process 1
        claim_partition_process(1, 0);
        exit(0);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Child process 2
        claim_partition_process(2, 0);  // Same partition!
        exit(0);
    }

    // Parent waits
    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);

    sleep(1);

    std::cout << "\nParent process sees partition 0 owner: "
              << partition_owners[0] << std::endl;
    std::cout << "❌ Each process has its own copy - no shared state!\n" << std::endl;

    std::cout << "\n=== THE PROBLEM ===\n";
    std::cout << "Both child processes think they own partition 0,\n";
    std::cout << "but neither can see the other's claim.\n";
    std::cout << "This is why you need ZooKeeper/etcd/Raft for coordination!\n";

    return 0;
}
