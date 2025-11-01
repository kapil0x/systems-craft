# Phase 7 RPS Capacity Analysis

**Date:** 2025-10-05
**Current Phase:** Phase 7 (HTTP Keep-Alive + Thread Pool)
**Goal:** Measure maximum RPS and identify next bottleneck

---

## Current Performance Metrics

### Test Results Summary

| Test | Clients | Req/Client | Total Req | Success Rate | RPS | Avg Latency |
|------|---------|------------|-----------|--------------|-----|-------------|
| Light Load | 20 | 50 | 1,000 | 100% | 200 | 0.01ms |
| Medium Load | 50 | 100 | 5,000 | 100% | 500 | 0.13ms |
| High Load | 100 | 100 | 10,000 | 100% | **1,000** | 0.41ms |
| Max Throughput | 200 | 50 | 10,000 | **99.51%** | **2,000** | 0.53ms |
| Stress Test | 500 | 20 | 10,000 | 64.08% | 3,333 | 37.41ms |

### Key Findings

#### ✅ Sweet Spot: ~2,000 RPS with 99.5% Success Rate

**At 200 concurrent clients:**
- **2,000 RPS** sustained
- **99.51% success rate**
- **0.53ms average latency**
- **49 failures out of 10,000 requests** (0.49%)

This is our **reliable operating point** - high throughput with minimal failures.

####  Breaking Point: ~3,300 RPS with 64% Success Rate

**At 500 concurrent clients:**
- 3,333 RPS attempted
- **Only 64.08% success** (3,592 failures!)
- Latency jumps to **37.41ms** (70x increase!)
- System saturated

---

## Bottleneck Analysis

### What's Limiting Us?

Looking at the performance degradation:

1. **100 clients → 200 clients:** Success stays at 99.5%+ ✓
2. **200 clients → 500 clients:** Success drops to 64% ✗

This suggests we're hitting a **resource limit** around 200-300 concurrent connections.

### Possible Bottlenecks (ordered by likelihood):

#### 1. **File Descriptor Limit** ⭐ MOST LIKELY
```bash
# Check current limits
ulimit -n  # Probably 256 or 1024
```

**Evidence:**
- Clean performance up to 200 clients
- Sudden degradation at 500 clients
- Classic sign of FD exhaustion

**Impact:** Each client needs 1 FD. Server also needs FDs for:
- Listening socket
- Metrics file
- Background tasks
- → 500 clients + overhead ≈ 520 FDs needed

**Solution:** Increase OS file descriptor limit

#### 2. **Thread Pool Saturation**

Current: 16 worker threads

**Evidence:**
- Latency jump from 0.53ms → 37.41ms (queueing delay)
- Workers can't keep up with 500 concurrent requests

**Calculation:**
- If each request takes ~1ms to process
- 16 threads can handle 16,000 RPS theoretically
- But we're only at ~2,000 RPS
- → Thread count is NOT the bottleneck (yet)

#### 3. **Network Buffer Exhaustion**

**Evidence:**
- macOS `somaxconn=128` (kernel queue limit)
- We increased to `backlog=1024` in code
- But actual limit capped at 128 by kernel

**Test needed:**
```bash
sysctl kern.ipc.somaxconn  # Check actual limit
```

####  4. **Memory Pressure**

**Current usage:** Likely low at 10,000 requests

**Not the bottleneck yet** - would see gradual degradation, not cliff

#### 5. **File I/O Bottleneck**

Background writer thread to `metrics.jsonl`

**Evidence against:**
- Good performance up to 200 clients
- Would see gradual slowdown, not sudden failure

---

## Comparative Performance

### Phase-by-Phase Progress

| Phase | Architecture | Max RPS | Success @ 100 clients |
|-------|--------------|---------|----------------------|
| Phase 1-3 | Single-threaded | ~200 | 80.2% |
| Phase 6 | Thread Pool (8 workers) | ~1,500 | 95%+ |
| **Phase 7** | **Thread Pool (16 workers) + Keep-Alive** | **~2,000** | **100%** |

**Total improvement:** **10x RPS increase** from Phase 1 to Phase 7!

---

## Phase 8 Optimization Recommendations

### Primary Target: File Descriptor Limits

**Hypothesis:** We're hitting OS FD limits around 250-300 concurrent connections.

**Validation:**
```bash
# Check current limit
ulimit -n

# Check server FD usage under load
lsof -p <server_pid> | wc -l
```

**Implementation:**
1. Increase OS limits:
   ```bash
   # Temporary (current session)
   ulimit -n 10000

   # Permanent (macOS)
   launchctl limit maxfiles 10000 unlimited
   ```

2. Add FD monitoring to server

3. Re-test with 500, 1000 clients

**Expected gain:** **5-10x RPS increase** → 10,000-20,000 RPS

### Secondary Targets (if FD isn't the issue):

1. **Increase Kernel Listen Queue**
   ```bash
   sudo sysctl -w kern.ipc.somaxconn=4096
   ```

2. **Optimize Thread Pool Size**
   - Test 32, 64 workers
   - Find optimal based on core count

3. **Connection Pooling on Client Side**
   - Reuse connections more effectively
   - Reduce connection overhead

4. **Zero-Copy I/O**
   - Use `sendfile()` or memory-mapped files
   - Reduce copy operations

---

## Next Steps for Phase 8

1. ✅ Measure current RPS → **DONE: ~2,000 RPS**
2. 🔄 Validate FD limit hypothesis → **IN PROGRESS**
3. ⏳ Increase FD limits and re-test
4. ⏳ Measure improvement
5. ⏳ If still limited, profile to find next bottleneck

**Goal for Phase 8:** **Achieve 10,000+ RPS with 99%+ success rate**

---

## Performance Characteristics

### Latency vs Load

```
Load (clients)    RPS      Latency    Success
─────────────────────────────────────────────
20               200      0.01ms     100%
50               500      0.13ms     100%
100            1,000      0.41ms     100%
200            2,000      0.53ms     99.5%   ← Sweet spot
500            3,333     37.41ms     64.0%   ← Breaking point
```

### Latency Budget Breakdown (estimated)

At 0.53ms total latency:
- Network (localhost): ~0.01ms
- Accept + read: ~0.10ms
- Rate limiting: ~0.05ms
- JSON parsing: ~0.15ms
- Queue write: ~0.10ms
- HTTP response: ~0.12ms

**No single dominant bottleneck** - well-balanced!

---

## Conclusion

**Current Capacity:** ~2,000 RPS reliably
**Bottleneck:** Likely file descriptor limit
**Next Phase:** Increase FD limits, target 10,000+ RPS
**System State:** Healthy, well-optimized, ready for scaling

The Phase 6 thread pool and Phase 7 keep-alive optimizations have delivered excellent results. We've achieved 10x improvement from the baseline. Now we need to address OS-level limits to unlock the next 5-10x gain.

