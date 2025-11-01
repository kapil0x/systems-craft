# Defense-in-Depth: Ensuring Skills Are Used

## Problem

Skills in the superpowers system require **active discipline** from Claude - they're not automatic hooks. This creates a risk that Claude might skip using a skill and make preventable mistakes (like creating worktrees without .gitignore setup).

## Solution: Four Layers of Defense

We've implemented a defense-in-depth strategy with four complementary layers:

### Layer 1: Session Start Hook (System-Level)
**What**: The superpowers system includes a session-start hook that loads the using-skills guide at the start of every conversation.

**How it helps**:
- Reminds Claude to check for skills before ANY task
- Provides the mandatory workflow: `find-skills` → Read skill → Follow skill
- Lists all available skills with descriptions

**Limitation**: This is injected by the superpowers system, not directly editable. To enhance it, you'd need to contribute to the upstream superpowers-skills repository.

### Layer 2: CLAUDE.md Skills Checklist (Project-Level)
**What**: Added a "Skills Checklist" section to CLAUDE.md that explicitly lists mandatory skills for this project.

**Location**: `CLAUDE.md` lines 144-172

**Content**:
- Table of mandatory skills with paths and usage triggers
- Workflow discipline steps
- Worktree configuration preferences
- Bold statement: "Skills exist and you didn't use them = failed task"

**How it helps**:
- Project-specific reminder that appears in every conversation context
- Makes expectations explicit
- Documents worktree preferences so Claude doesn't need to ask

### Layer 3: Slash Command (Convenience)
**What**: Created `/worktree` slash command that explicitly requires using the skill.

**Location**: `.claude/commands/worktree.md`

**How it helps**:
- User can type `/worktree` to trigger worktree creation
- Command prompt explicitly requires reading and following the skill
- Lists all the mistakes the skill prevents
- Provides exact Read tool path to load the skill

**Usage**: When you want to create a worktree, type `/worktree` and I'll be forced to load and follow the skill.

### Layer 4: .gitignore Protection (Safety Net)
**What**: Pre-added worktree directories to .gitignore

**Location**: `.gitignore` lines 44-49

**How it helps**:
- Even if worktrees are created manually (bypassing the skill), they won't pollute git status
- Reduces damage from workflow violations
- .claude/ directory also ignored (contains slash commands and personal settings)

## Testing the System

### Test 1: Does `/worktree` command exist?
```bash
cat .claude/commands/worktree.md
```
✅ **Result**: Command exists and explicitly requires using the skill

### Test 2: Is CLAUDE.md clear about skills?
```bash
grep -A 10 "Skills Checklist" CLAUDE.md
```
✅ **Result**: Section exists with mandatory skills table and workflow discipline

### Test 3: Is .gitignore protecting us?
```bash
grep -E "(worktree|\.claude)" .gitignore
```
✅ **Result**: Both `.worktrees/`, `worktrees/`, and `.claude/` are ignored

### Test 4: Can we find the skill?
```bash
/Users/kapiljain/.config/superpowers/skills/skills/using-skills/find-skills worktree
```
✅ **Result**: Skill is discoverable through find-skills

## How It Works Together

**Scenario: User asks Claude to create a worktree**

1. **Session hook** reminds Claude to check for skills
2. **CLAUDE.md** explicitly lists using-git-worktrees as mandatory
3. **Slash command** (if used) forces Claude to read the skill
4. **.gitignore** protects repo even if Claude bypasses everything

**Defense-in-depth means**: Even if one layer fails, others catch the mistake.

## What About the Session Start Hook?

The session-start hook is controlled by the superpowers system itself (it's what loads the using-skills guide). We can't directly edit it, but we can:

1. **Suggest improvements** to the obra/superpowers-skills repository
2. **Rely on the existing reminder** that already tells Claude to check skills before tasks
3. **Use the other three layers** (CLAUDE.md, slash command, .gitignore) to reinforce the message

The session hook already says:
> "**Mandatory Workflow 2: Before ANY Task**
> 1. Check skills list at session start, or run find-skills [PATTERN] to filter.
> 2. If relevant skill exists, YOU MUST use it"

Our additional layers make it **project-specific** and **harder to miss**.

## Maintenance

- **CLAUDE.md**: Update when adding new mandatory skills
- **Slash commands**: Add more for other common workflows
- **.gitignore**: Add new patterns as needed
- **Session hook**: Suggest improvements to upstream if patterns emerge

## Key Insight

Skills aren't automatic - they require discipline. But we can make that discipline easier by:
- Making expectations explicit (CLAUDE.md)
- Providing convenient triggers (slash commands)
- Building safety nets (.gitignore)
- Relying on system-level reminders (session hook)

**No single layer is perfect, but together they create a robust system.**
