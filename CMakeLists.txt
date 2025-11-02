cmake_minimum_required(VERSION 3.16)
project(MetricStream VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Find librdkafka
find_path(RDKAFKA_INCLUDE_DIR librdkafka/rdkafkacpp.h PATHS /opt/homebrew/include)
find_library(RDKAFKA_LIBRARY rdkafka++ PATHS /opt/homebrew/lib)
find_library(RDKAFKA_C_LIBRARY rdkafka PATHS /opt/homebrew/lib)

if(NOT RDKAFKA_INCLUDE_DIR OR NOT RDKAFKA_LIBRARY)
    message(FATAL_ERROR "librdkafka not found. Please install with: brew install librdkafka")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include ${RDKAFKA_INCLUDE_DIR})

# Add subdirectories
add_subdirectory(src)

# Enable testing
enable_testing()
add_subdirectory(tests)

# Create main executable
add_executable(metricstream_server
    src/main.cpp
)

target_link_libraries(metricstream_server
    ingestion_lib
    common_lib
    ${RDKAFKA_LIBRARY}
    ${RDKAFKA_C_LIBRARY}
    Threads::Threads
)

# Create test client
add_executable(test_client
    test_client.cpp
)

target_link_libraries(test_client
    Threads::Threads
)

# Create load test client
add_executable(load_test
    load_test.cpp
)

target_link_libraries(load_test
    Threads::Threads
)

# Create persistent connection load test client
add_executable(load_test_persistent
    load_test_persistent.cpp
)

target_link_libraries(load_test_persistent
    Threads::Threads
)

# Create queue consumer executable
add_executable(metricstream_consumer
    src/consumer_main.cpp
)

target_link_libraries(metricstream_consumer
    queue_consumer_lib
    kafka_consumer_lib
    partitioned_queue_lib
    ${RDKAFKA_LIBRARY}
    ${RDKAFKA_C_LIBRARY}
    Threads::Threads
)