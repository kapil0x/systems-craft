# Craft #5: Alerting & Notification System

**Component:** PagerDuty-like alerting platform with rules and notifications
**Learning Goal:** Build an alerting system from first principles
**Total Time:** 6-10 hours across 4 phases (estimated)
**Target Performance:** 10K+ rules/sec, <1 min detection latency, 99.99% delivery

---

## Overview

Craft #5 teaches you how to build a production alerting system that evaluates rules, manages alert lifecycle, and delivers notifications reliably. You'll learn the patterns used by PagerDuty, Opsgenie, and VictoriaMetrics alerting.

**Key Learning:** Build rule engines, state machines, notification systems, and escalation policies.

---

## Proposed Phase Breakdown

### Phase 1: Event-Driven Rule Evaluation 📝
- **Goal:** Real-time alert detection
- **Components:** Rule DSL, streaming evaluation, threshold detection
- **Time:** 2-3 hours

### Phase 2: Alert State Machine 📝
- **Goal:** Manage alert lifecycle
- **Components:** State transitions, deduplication, silencing
- **Time:** 1-2 hours

### Phase 3: Multi-Channel Notifications 📝
- **Goal:** Deliver alerts reliably
- **Components:** Email, Slack, PagerDuty integrations, retry logic
- **Time:** 2-3 hours

### Phase 4: Escalation Policies 📝
- **Goal:** Route alerts to right people
- **Components:** Escalation paths, on-call schedules, routing rules
- **Time:** 1-2 hours

---

## Status

📝 **Design Phase** - This craft is planned but not yet designed or implemented.

**To contribute:** If you'd like to help design or implement this craft, please:
1. Read [docs/PHASES_TO_CRAFTS_MAPPING.md](../docs/PHASES_TO_CRAFTS_MAPPING.md) for the overall structure
2. Review existing alerting systems (Alertmanager, PagerDuty, Opsgenie)
3. Create design documents following the pattern from Craft #2

---

## Prerequisites

- **Craft #1:** Understand metrics ingestion
- **Craft #4:** Understand query engine (alerts query for threshold violations)
- **Patterns:** Familiarity with state machines and event-driven systems helpful

---

## Related Documentation

- **[Phases to Crafts Mapping](../docs/PHASES_TO_CRAFTS_MAPPING.md)** - Master reference
- **[Craft #4 README](../craft4/README.md)** - Query engine (alerts use queries)
- **[Phase 0 Alerting](../phase0/README.md)** - Simple polling-based alerting (baseline)

---

**This is Craft #5 of Systems Craft.** Once complete, you'll understand how PagerDuty, Opsgenie, and Alertmanager work internally.
