# Craft #4: Query & Aggregation Engine

**Component:** PromQL-like query processor with aggregations and optimizations
**Learning Goal:** Build a query engine for time-series data from first principles
**Total Time:** 8-12 hours across 4 phases (estimated)
**Target Performance:** Query 100M+ data points in <1 second

---

## Overview

Craft #4 teaches you how to build a query engine that efficiently processes large-scale time-series data. You'll learn query parsing, execution planning, parallel aggregation, and caching strategies used by systems like Prometheus, Grafana, and Datadog.

**Key Learning:** Build a query language, optimize execution plans, and implement distributed aggregations.

---

## Proposed Phase Breakdown

### Phase 1: Query Parser & AST 📝
- **Goal:** Parse query language into executable form
- **Components:** Lexer, tokenizer, parser, AST
- **Time:** 2-3 hours

### Phase 2: Execution Planning 📝
- **Goal:** Optimize query execution
- **Components:** Query optimizer, physical plans, cost-based optimization
- **Time:** 2-3 hours

### Phase 3: Parallel Aggregation 📝
- **Goal:** Fast aggregations over large datasets
- **Components:** Map-reduce aggregations, thread pool, streaming
- **Time:** 2-3 hours

### Phase 4: Result Caching & Optimization 📝
- **Goal:** Improve repeated query performance
- **Components:** LRU cache, partial results, materialized views
- **Time:** 2-3 hours

---

## Status

📝 **Design Phase** - This craft is planned but not yet designed or implemented.

**To contribute:** If you'd like to help design or implement this craft, please:
1. Read [docs/PHASES_TO_CRAFTS_MAPPING.md](../docs/PHASES_TO_CRAFTS_MAPPING.md) for the overall structure
2. Review existing query engines (PromQL, InfluxQL, SQL)
3. Create design documents following the pattern from Craft #2

---

## Prerequisites

- **Craft #1:** Understand metrics ingestion
- **Craft #3:** Understand storage engine (queries read from storage)
- **Algorithms:** Familiarity with parsing, trees, and parallel algorithms helpful

---

## Related Documentation

- **[Phases to Crafts Mapping](../docs/PHASES_TO_CRAFTS_MAPPING.md)** - Master reference
- **[Craft #3 README](../craft3/README.md)** - Storage engine (query reads from here)
- **[Architecture](../docs/ARCHITECTURE.md)** - Overall system design

---

**This is Craft #4 of Systems Craft.** Once complete, you'll understand how Prometheus, Grafana, and Datadog query engines work internally.
