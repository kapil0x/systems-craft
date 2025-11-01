# Conversation Capture Template

## Quick Capture Command
```bash
# When you're ready to capture a conversation, just say:
"capture this conversation - [brief topic description]"
```

## Information I'll Need From You

### Session Basics
- **Primary topic/focus** (e.g., "Phase 5 performance optimization", "deadlock debugging")
- **Key outcome** (what did we accomplish?)
- **Duration estimate** (optional)

### Technical Content I'll Extract Automatically
- Problems identified and solutions chosen
- Code files modified
- Concepts learned and difficulty level
- Technical decisions with rationale
- Performance impacts
- Cross-references to previous work

### Optional Context You Can Provide
- **Specific insights** you want emphasized
- **Follow-up items** to track
- **Related conversations** to link to
- **Difficulty rating** for concepts learned (1-5)

## Example Capture Request

**You:**
```
"capture this conversation - Phase 4 mutex optimization
Key outcome: eliminated double mutex bottleneck with hash-based approach
Main insight: constructor races are about undefined behavior, not sync failures
Follow-up: need to implement flush_metrics TODO for community"
```

**My Response:**
```
✅ Session captured: conversation/sessions/2024-10-01_phase4_mutex_optimization.md
📊 Database updated: 1 session, 3 technical decisions, 2 concepts learned
🔗 Cross-references: Links to ring buffer discussion, performance optimization journey
📁 Topic files updated: concurrency/mutex_fundamentals.md
```

## Capture Triggers

### When You Should Request Capture
- ✅ **Solved complex technical problems**
- ✅ **Made architecture decisions**  
- ✅ **Learned new concepts** (especially difficulty 3+)
- ✅ **Completed optimization phases**
- ✅ **Created TODOs for future work**
- ✅ **Had debugging breakthroughs**

### When You Can Skip
- ❌ Simple troubleshooting (< 10 minutes)
- ❌ Basic Q&A without new insights
- ❌ Routine code reviews
- ❌ Administrative tasks

## Automated Processing I'll Do

### Database Entries
```python
# I'll automatically create entries like:
db.add_conversation(
    session_date="2024-10-01",
    topic="Phase 4 Mutex Optimization", 
    phase="Phase 4",
    summary="Analyzed double mutex bottleneck...",
    duration_minutes=120
)

db.add_technical_decision(
    decision_type="performance_optimization",
    problem_description="Double mutex serialization...",
    solution_chosen="Hash-based pre-allocated mutex pool...",
    code_files_affected=["include/ingestion_service.h", "src/ingestion_service.cpp"]
)
```

### File Organization
- **Session file**: `conversation/sessions/YYYY-MM-DD_topic.md`
- **Topic updates**: Add insights to relevant topic files
- **Database export**: Update `conversation/exports/`
- **Cross-references**: Link to related conversations

## Workflow Integration

### During Development
```
You: "Let's optimize the ring buffer performance"
[Technical discussion happens...]
You: "capture this conversation - ring buffer optimization
Key outcome: reduced memory allocation by 40%
Main insight: cache line alignment matters for concurrent access"

Me: [Processes and stores everything automatically]
```

### For Future Sessions
```bash
# Query what we learned about any topic
python3 tools/conversation_db.py --query-topic "ring buffer"
python3 tools/conversation_db.py --query-topic "optimization" 

# See learning progression
python3 tools/conversation_db.py --export-md conversation/exports
```

## Pro Tips

### Better Captures
- **Be specific about outcomes**: "reduced latency by 50%" vs "improved performance"
- **Mention key insights**: What was the "aha!" moment?
- **Note difficulty**: Was this concept hard to understand?
- **Connect to goals**: How does this fit the bigger picture?

### Efficient Workflow
- **Capture after major work**, not every small interaction
- **Group related discussions** into single sessions when possible
- **Use consistent topic names** for better cross-referencing
- **Export regularly** to keep markdown files updated

---

This template ensures we capture the right conversations at the right level of detail, building a comprehensive knowledge base of our MetricStream technical journey.