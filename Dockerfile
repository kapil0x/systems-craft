# Systems Craft - Docker Quickstart
# Build and run the complete monitoring PoC in one command

FROM gcc:13 AS builder

# Install CMake
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Phase 0 PoC source
COPY phase0/ ./phase0/

# Build the PoC
WORKDIR /app/phase0
RUN chmod +x build.sh && ./build.sh

# Runtime stage (smaller image)
FROM debian:bookworm-slim

WORKDIR /app

# Copy built binaries
COPY --from=builder /app/phase0/build/phase0_poc ./phase0_poc
COPY --from=builder /app/phase0/build/simple_benchmark ./simple_benchmark

# Expose ingestion port
EXPOSE 8080

# Health check endpoint
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Run the server
CMD ["./phase0_poc"]
