# AGENTS.md - Multi-Project Workspace

## Projects Overview
- **metricstream/** - C++ monitoring platform (Phase 0 PoC + Craft #1 optimized ingestion). Main focus.
- **blog/** - Astro.js static site

## MetricStream Commands (C++17, CMake)
```bash
# Build: mkdir build && cd build && cmake .. && make
# Run server: ./build/metricstream_server [port]
# Load test: ./build/load_test 8080 50 10  # 50 clients, 10 requests
# Performance test: ./performance_test.sh
# Run single test: cd build && ctest -R test_name -V
```

## Blog Commands (Astro, TypeScript)
```bash
# From blog/ directory:
# Dev: npm run dev
# Build: npm run build
# Preview: npm run preview
```

## MetricStream Architecture
- **phase0/** - Complete 600-line PoC (5 components in one file). Start here.
- **src/** - Craft #1 optimized ingestion: http_server.cpp, ingestion_service.cpp, thread_pool.cpp, event_loop.cpp
- **Storage**: Append-only `metrics.jsonl` (JSON Lines format)
- **Performance focus**: Measure → Bottleneck → Optimize → Document (see performance_results.txt)

## Code Style (C++)
- **Zero dependencies**: Custom HTTP & JSON parsing
- **C++17**: `std::thread`, `std::mutex`, smart pointers
- **Headers**: `#include "local.h"` then `<system>`
- **Naming**: snake_case functions/vars, PascalCase classes, UPPER_SNAKE constants
- **Error handling**: Return codes or exceptions with context
- **Optimization methodology**: Profile first, document before/after metrics, target measured bottlenecks

## Code Style (TypeScript/Astro)
- **TypeScript strict mode**: Full type safety
- **Imports**: Astro components, then local modules, then external packages
- **Components**: Astro components in components/, content in content/

## Feature Development Workflow (MANDATORY)

**⚠️ Branch protection is strictly enforced. All changes MUST go through PR workflow - no exceptions, even for admins!**

### Starting a New Feature
```bash
# 1. Create worktree with feature branch (from metricstream/)
git worktree add .worktrees/feature-name -b feature/feature-name

# 2. Work in the worktree
cd .worktrees/feature-name

# 3. Make changes, commit frequently
git add <files>
git commit -m "feat: descriptive message"

# 4. Push to remote
git push origin feature/feature-name

# 5. Create PR (use GitHub CLI or web UI)
gh pr create --title "Feature: description" --body "Details of changes"

# 6. After PR is merged, cleanup worktree
cd ../..  # Back to main repo
git worktree remove .worktrees/feature-name
git branch -d feature/feature-name
```

### Worktree Rules
- **Location**: Always use `.worktrees/` directory (must be in .gitignore)
- **Never create manually**: Use skill or `/worktree` command, or git worktree command
- **Branch naming**: `feature/name`, `fix/name`, `docs/name`, `refactor/name`
- **Cleanup**: Always remove worktree after PR is merged

### PR Guidelines
- **Title**: Use conventional commits (feat:, fix:, docs:, refactor:, test:, chore:)
- **Description**: Explain what and why, include performance metrics if relevant
- **Tests**: Run build/tests before creating PR
- **Review**: Wait for CI checks and review before merging

## Important Notes (from metricstream/CLAUDE.md)
- This is a learning platform: engineers build systems phase-by-phase with labs/exercises
- Always measure performance before optimizing (use load_test and document results)
- Skills workflow: Before ANY task, run `find-skills [keyword]`, read relevant skill with Read tool, announce usage, follow exactly
- Current focus: Phase 4 hash-based per-client mutex optimization
