---
description: Create a git worktree using the using-git-worktrees skill
---

I need you to create a new git worktree for a feature branch.

**IMPORTANT**: You MUST use the Using Git Worktrees skill to do this. Follow these steps exactly:

1. Read the skill: `/Users/kapiljain/.config/superpowers/skills/skills/collaboration/using-git-worktrees/SKILL.md`
2. Announce: "I've read the Using Git Worktrees skill and I'm using it to create an isolated workspace"
3. Follow ALL steps in the skill:
   - Check for existing .worktrees/ or worktrees/ directories
   - Verify .gitignore contains the directory (add if missing)
   - Ask me for the branch name if not already specified
   - Create the worktree with proper directory structure
   - Run project setup (cmake, make, etc.)
   - Verify tests pass as baseline
   - Report the location and readiness

**Never skip the skill** - it prevents critical mistakes like:
- Creating worktrees outside the standard directory
- Forgetting to add the directory to .gitignore
- Not running baseline tests
- Creating detached HEAD worktrees by accident

After reading this command prompt, use the Read tool to load the skill and follow it.
