# Craft #3: Time-Series Storage Engine

**Component:** InfluxDB-like time-series database with compression and indexing
**Learning Goal:** Build a storage engine optimized for time-series data from first principles
**Total Time:** 10-15 hours across 4 phases (estimated)
**Target Performance:** 1M+ writes/sec, 10:1 compression, sub-second queries

---

## Overview

Craft #3 teaches you how to build a specialized storage engine for time-series data. You'll learn why general-purpose databases aren't optimal for metrics, and how to leverage time-series characteristics (sequential writes, immutable data, time-based queries) for massive performance gains.

**Key Learning:** Build LSM trees, implement compression algorithms, and understand query optimization for time-series workloads.

---

## Proposed Phase Breakdown

### Phase 1: Basic LSM Tree Storage 📝
- **Goal:** Understand write-optimized storage
- **Components:** Memtable, WAL, SSTable, compaction
- **Time:** 3-4 hours

### Phase 2: Gorilla Compression 📝
- **Goal:** Achieve 10:1 compression for time-series data
- **Components:** Delta-of-delta encoding, XOR compression
- **Time:** 2-3 hours

### Phase 3: Tag-Based Indexing 📝
- **Goal:** Fast lookups by metric name and tags
- **Components:** Inverted index, bloom filters
- **Time:** 3-4 hours

### Phase 4: Time-Range Query Optimization 📝
- **Goal:** Sub-second queries for 24-hour ranges
- **Components:** Block indexes, parallel execution, caching
- **Time:** 2-4 hours

---

## Status

📝 **Design Phase** - This craft is planned but not yet designed or implemented.

**To contribute:** If you'd like to help design or implement this craft, please:
1. Read [docs/PHASES_TO_CRAFTS_MAPPING.md](../docs/PHASES_TO_CRAFTS_MAPPING.md) for the overall structure
2. Review existing time-series databases (InfluxDB, TimescaleDB, VictoriaMetrics)
3. Create design documents following the pattern from Craft #2

---

## Prerequisites

- **Craft #1:** Understand metrics ingestion
- **Craft #2:** Understand message queues (consumers read from queue and write to storage)
- **Data structures:** Familiarity with trees, hash tables, and compression helpful

---

## Related Documentation

- **[Phases to Crafts Mapping](../docs/PHASES_TO_CRAFTS_MAPPING.md)** - Master reference
- **[Craft #2 README](../craft2/README.md)** - Message queue (feeds data to storage)
- **[Architecture](../docs/ARCHITECTURE.md)** - Overall system design

---

**This is Craft #3 of Systems Craft.** Once complete, you'll understand how InfluxDB, TimescaleDB, and VictoriaMetrics work internally.
