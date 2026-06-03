---
name: update-claude-beta
description: Pull the latest beta version of the Claude-related skills, agents, and tooling from the external `hailo-ai/hailo15-agentic-coding` repo into this hailo-media-library checkout. Use when the user says "update the skills", "pull the latest Claude beta", "get newer skills from the agentic-coding repo", or wants the in-between-SDK-release updates that this repo doesn't carry. Compares last-commit dates first and skips if local is already up to date. Overwrites matching files under `.claude/` and the root `CLAUDE.md`; never deletes local-only files.
tools: Bash, Read, Glob, Grep, Edit, Write
---

# /update-claude-beta — sync from `hailo15-agentic-coding`

This `hailo-media-library` repo ships an `.claude/` directory (skills + agents + settings) and a root `CLAUDE.md`. Those are frozen at each SDK release. Between releases, fresher versions are pushed to <https://github.com/hailo-ai/hailo15-agentic-coding>. This skill pulls those newer files in.

**Semantics:** "newer" is decided by last-commit date — if the external repo's `HEAD` is committed strictly *after* this repo's `HEAD`, the external files win. Files that exist locally but not in the external repo are **left alone** (no `--delete`). Files that exist in the external repo are **overwritten** in the local working tree.

The skill does *not* commit anything. The user reviews `git status` / `git diff` afterwards and commits manually.

## External repo

```
https://github.com/hailo-ai/hailo15-agentic-coding
```

Assumed layout (mirrors this repo for the Claude-relevant files):
- `.claude/`  — skills, agents, settings
- `CLAUDE.md` — root project instructions

If the layout has changed, adjust the sync paths in step 4 accordingly.

## Procedure

1. **Sanity-check the working tree.** Refuse to overwrite uncommitted local edits to `.claude/` or `CLAUDE.md` without an explicit OK from the user.
   ```bash
   git status --porcelain .claude CLAUDE.md
   ```
   If non-empty, list the dirty paths and ask the user whether to proceed (their changes will be overwritten for any file the external repo also has).

2. **Fetch the external repo's last commit date.** No clone yet — just the metadata, via the unauthenticated GitHub API:
   ```bash
   EXT_JSON=$(curl -fsSL "https://api.github.com/repos/hailo-ai/hailo15-agentic-coding/commits/HEAD") || {
     echo "Cannot reach hailo15-agentic-coding — check network or whether the repo is public/accessible."
     exit 1
   }
   EXT_DATE=$(python3 -c 'import sys, json; print(json.loads(sys.argv[1])["commit"]["committer"]["date"])' "$EXT_JSON")
   EXT_SHA=$(python3 -c 'import sys, json; print(json.loads(sys.argv[1])["sha"][:12])' "$EXT_JSON")
   ```
   If the API returns `404` or `403`, tell the user (repo may not be public yet, or rate-limited) and stop.

3. **Get this repo's last commit date.**
   ```bash
   LOC_DATE=$(git -C . log -1 --format=%cI HEAD)
   LOC_SHA=$(git -C . log -1 --format=%h HEAD)
   ```
   Compare with `EXT_DATE`. If `EXT_DATE <= LOC_DATE`, report "up to date (local $LOC_SHA @ $LOC_DATE ≥ external $EXT_SHA @ $EXT_DATE)" and stop.

4. **Shallow clone the external repo to a temp dir.**
   ```bash
   TMP=$(mktemp -d)
   git clone --depth=1 https://github.com/hailo-ai/hailo15-agentic-coding.git "$TMP/agentic"
   ```
   Bail out if the clone fails.

5. **Sync the files in.** Use `rsync -a` (no `--delete`) so we *overwrite + add*, never remove. Two sources, both rooted at the repo top:
   ```bash
   REPO_ROOT=$(git rev-parse --show-toplevel)

   # .claude/ (recursively)
   if [ -d "$TMP/agentic/.claude" ]; then
     rsync -a "$TMP/agentic/.claude/" "$REPO_ROOT/.claude/"
   fi

   # CLAUDE.md at the root, only if the external repo carries one
   if [ -f "$TMP/agentic/CLAUDE.md" ]; then
     cp -f "$TMP/agentic/CLAUDE.md" "$REPO_ROOT/CLAUDE.md"
   fi
   ```

6. **Cleanup + summary.**
   ```bash
   rm -rf "$TMP"
   git -C "$REPO_ROOT" status --short .claude CLAUDE.md
   git -C "$REPO_ROOT" diff --stat .claude CLAUDE.md | tail -20
   ```
   Tell the user:
   - external commit pulled (`$EXT_SHA @ $EXT_DATE`),
   - the list of changed paths from `git status`,
   - that they should review the diff and commit themselves; this skill does *not* commit.

## Inputs to ask the user

Ask only when something blocks the sync:
- "Your working tree under `.claude/` has uncommitted changes — overwrite them?" (only if step 1 reports dirty paths)
- "External repo isn't reachable / returns 404 — should I retry, or are you offline?" (only on API failure)

Otherwise the skill runs end-to-end.

## Gotchas

- **No `--delete`.** A file that you deleted locally but that still exists in the external repo will reappear after sync. If the user wants strict mirroring, they have to ask explicitly — this skill doesn't do destructive removal by default.
- **The external repo is beta.** Newer ≠ better. After sync, run a couple of the affected skills end-to-end (e.g. `/explain-pipeline`, `/swap-model`) to confirm nothing regressed before committing.
- **Don't auto-commit.** The user owns the commit message and decides which subset of the sync to keep. The skill stops at `git status`/`git diff`.
- **API rate limit.** Unauthenticated `api.github.com` is limited to ~60 req/h per IP. If hit, wait or use a `GITHUB_TOKEN` env var (`curl -H "Authorization: Bearer $GITHUB_TOKEN" …`).
- **Submodules / LFS.** The shallow clone above does not init submodules or pull LFS. If `hailo15-agentic-coding` ever adds either, this skill will need updating.

## Delegate

None — this is a self-contained file-sync skill.
