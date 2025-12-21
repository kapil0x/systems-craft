# Starter Code Templates - Systems Craft

**Overcome blank page paralysis.** These templates provide the boilerplate so you can focus on learning core concepts.

## 🎯 Philosophy

Instead of starting from scratch, you get:
- ✅ **Structure in place** - Headers, includes, main() already written
- ✅ **TODO(human) markers** - Exactly where YOU implement the optimization
- ✅ **Inline explanations** - Why this matters, what you'll learn
- ✅ **Testing instructions** - How to compile, run, measure improvement
- ✅ **Expected results** - Know if you're on the right track

---

## 📚 Available Templates

### **Craft #1: Metrics Ingestion Optimization**

| Phase | Template | Goal | Improvement |
|-------|----------|------|-------------|
| **Phase 1** | `craft1_phase1_starter.cpp` | Add threading per request | 50 → 200 RPS (4x) |
| **Phase 2** | `craft1_phase2_starter.cpp` | Async I/O with producer-consumer | 200 → 500+ RPS (2.5x) |
| **Phase 3** | `craft1_phase3_starter.cpp` | Optimize JSON parsing O(n²) → O(n) | 500 → 1000+ RPS (2x) |

**Coming soon:** Phase 4 (lock-free), Phase 5 (ring buffers), Phase 6 (rate limiting), Phase 7 (HTTP keep-alive)

---

## 🚀 How to Use

### **Step 1: Choose Your Phase**

Start with Phase 1 if you're new. Each phase builds on the previous.

```bash
cd templates/
```

### **Step 2: Read the Template**

Open the starter file and read:
- 📋 **Header comments** - What you'll build and learn
- 🎯 **TODO(human) markers** - Where you write code
- 💡 **Inline hints** - Guidance on implementation approach
- 📊 **Expected results** - Performance targets

### **Step 3: Implement the TODOs**

```cpp
// Example from Phase 1:
for (int i = 0; i < num_requests; i++) {
    // TODO(human): Spawn thread here
    // Uncomment and complete:
    threads.emplace_back(handle_metric, sample_json);  // <- YOU WRITE THIS
}
```

**Focus areas marked with `TODO(human)`:**
- Core algorithm implementation
- Threading/synchronization logic
- Performance-critical paths
- Optimization trade-offs

### **Step 4: Compile & Run**

Each template includes compilation instructions:

```bash
# Phase 1: Threading
g++ -std=c++17 -pthread craft1_phase1_starter.cpp -o phase1
./phase1

# Phase 2: Async I/O
g++ -std=c++17 -pthread craft1_phase2_starter.cpp -o phase2
./phase2

# Phase 3: Parser optimization
g++ -std=c++17 -O2 craft1_phase3_starter.cpp -o phase3
./phase3
```

### **Step 5: Benchmark**

Measure your implementation:

```bash
# Option A: Standalone benchmark (in template)
./phase1
# Shows: "Expected: ~200 RPS"

# Option B: Integrated server benchmark
# After integrating into full server:
# Open website/benchmark.html
# Run load test against http://localhost:8080
```

### **Step 6: Compare Results**

Did you hit the target?
- ✅ **At target:** Move to next phase
- ⚠️ **Below target:** Review hints, check implementation
- 🚀 **Above target:** Congrats! Document what you did differently

---

## 📖 Learning Flow

### **Phase 1: Threading (Beginner-Friendly)**

```
Read template (10 min)
  ↓
Implement 3 TODOs (20 min)
  ↓
Compile & test (5 min)
  ↓
Measure: 50 → 200 RPS ✅
  ↓
Understand: Why threading helps
```

**Key Concepts:**
- `std::thread` basics
- Thread join() and why it matters
- Concurrent request handling
- Mutex for shared resources

### **Phase 2: Async I/O (Intermediate)**

```
Read template (15 min)
  ↓
Implement producer-consumer (30 min)
  ↓
Test queue behavior (10 min)
  ↓
Measure: 200 → 500+ RPS ✅
  ↓
Understand: Decoupling I/O from requests
```

**Key Concepts:**
- Producer-consumer pattern
- Thread-safe queues
- Condition variables (cv_.wait)
- Background worker threads
- Batching for efficiency

### **Phase 3: Parser Optimization (Advanced)**

```
Read template (15 min)
  ↓
Profile slow parser (10 min)
  ↓
Implement single-pass parser (45 min)
  ↓
Benchmark: 3x faster ✅
  ↓
Understand: Algorithm complexity matters
```

**Key Concepts:**
- Profiling bottlenecks
- O(n²) vs O(n) complexity
- Zero-copy techniques
- Pointer-based parsing
- String allocation costs

---

## 🎨 Template Structure

Every template follows this pattern:

```cpp
/**
 * Phase X Starter Template
 * Goal: [What you'll build]
 * Performance: [Baseline → Target RPS]
 * What you'll learn: [Key concepts]
 */

#include <necessary headers>

// Boilerplate code (already written for you)
class SomeClass { ... }

void function_with_todo() {
    // TODO(human): Implement this part
    //
    // Guidance:
    // - Step 1: Do this
    // - Step 2: Then this
    // - Hint: Consider this approach
    //
    // Why: Explanation of why this matters
}

int main() {
    // Testing code (already written)
    // Shows expected output
}

/**
 * Testing Instructions:
 * 1. Compile: ...
 * 2. Run: ...
 * 3. Expected: ...
 *
 * Key Learning: [Takeaways]
 * Trade-offs: [Pros and cons]
 */
```

---

## 💡 Pro Tips

### **1. Don't Skip Reading**
The header comments and inline guidance teach the "why" behind each optimization.

### **2. Measure Before Optimizing**
Run the baseline benchmark first. You can't improve what you don't measure.

### **3. Implement Incrementally**
- Get it compiling first
- Make it work second
- Measure third
- Optimize fourth

### **4. Compare with Solutions**
After implementing, compare your code with the full solution in `src/` to see alternative approaches.

### **5. Experiment**
Try different approaches! The templates are starting points, not the only way.

---

## 🔗 Integration with Full Server

These templates are **standalone learning tools**. To integrate into the full server:

1. **Test the concept** with the template
2. **Understand the pattern** (threading, async, parsing, etc.)
3. **Apply to server code** in `src/`
4. **Benchmark the server** with real HTTP load
5. **Compare results** to expected targets

---

## 🧪 Example Session

```bash
# Day 1: Phase 1 (Threading)
cd templates/
g++ -std=c++17 -pthread craft1_phase1_starter.cpp -o phase1
./phase1
# Output: "All requests processed! Expected: ~200 RPS"

# Integrate into server (src/main.cpp)
# Add thread pool, benchmark
# Result: 200 RPS achieved ✅

# Day 2: Phase 2 (Async I/O)
g++ -std=c++17 -pthread craft1_phase2_starter.cpp -o phase2
./phase2
# Output: "All requests submitted to queue"

# Integrate async queue into server
# Benchmark again
# Result: 500+ RPS achieved ✅

# Day 3: Phase 3 (Parser)
g++ -std=c++17 -O2 craft1_phase3_starter.cpp -o phase3
./phase3
# Output: "Speedup: 3x faster"

# Replace parser in server
# Benchmark again
# Result: 1000+ RPS achieved ✅

# Total improvement: 50 → 1000 RPS (20x!)
```

---

## ❓ FAQ

**Q: Do I need to use these templates?**
A: No! They're optional. You can start from scratch if you prefer. Templates just reduce boilerplate and focus on learning.

**Q: Can I modify the templates?**
A: Absolutely! These are starting points. Experiment and make them your own.

**Q: What if I get stuck on a TODO?**
A: Check the hints in comments, review the related concept in craft pages, or compare with the solution code in `src/`.

**Q: Are solutions available?**
A: Yes! The full implementations are in `src/` directory. But try implementing first—you learn more from struggling.

**Q: Can I skip phases?**
A: You can, but each phase builds on the previous. Skipping means missing foundational concepts.

**Q: How long does each phase take?**
A: Phase 1: 30-60 min, Phase 2: 1-2 hours, Phase 3: 1-2 hours. But everyone learns at their own pace!

---

## 🚀 Next Steps

1. Start with `craft1_phase1_starter.cpp`
2. Read the architecture explorer first: `website/architecture.html`
3. Benchmark baseline: `website/benchmark.html`
4. Track progress: `website/progress.html`
5. Join the community: [GitHub Discussions](https://github.com/kapil0x/systems-craft/discussions)

---

**Happy Building!** Remember: Blank pages are intimidating. Starter templates are your scaffold. Build, learn, iterate.
