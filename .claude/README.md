# Claude × Hailo Media Library

An agentic layer on top of `hailo-media-library`. It lets you explore docs, build and modify Media Library applications, and debug running deployments on the H15 board through Claude, without needing
deep knowledge of the codebase.

> **Updates.** This repo is updated with each Hailo SDK release. For more frequent, beta releases of the Claude-related skills, agents, and tooling, see the dedicated repo: <https://github.com/hailo-ai/hailo15-agentic-coding>. Run [`/update-claude-beta`](skills/update-claude-beta/) to pull those newer files into this checkout.


## Prerequisites

- Claude Code installed and authenticated.
- This repo cloned locally. Run `claude` from the repo root. Skills and agents under `.claude/` load automatically.
- An H15 SBC reachable over ethernet.
- The Yocto SDK installed on the host (only required for `/cross-compile`
  and `/deploy`).

## How to use it

Just describe what you want in plain English. Claude scans the skills
list on every prompt and invokes the matching one.
You can also call a skill directly with `/skill-name` when you want to be explicit.

Examples:

```
> what does the detection app do?
  → runs /explain-pipeline

> explain this pipeline, add a FHD(1920×1080@30FPS) stream, replace the detection network to be YOLOv8n and deploy it to my connected h15l
  → runs /explain-pipeline, then /add-stream then /get-model and /swap-model, /cross-compile, /deploy

> is the board overheating?
  → runs /board-status
```

## Documentation

The user guides shipped with this repo (under `docs/guides/`) are read
on demand by the `doc-explorer` agent and cited with page numbers when
referenced.

---

## Skills

### Connection

| Skill | What it does |
|---|---|
| [`/connect`](skills/connect/) | Configure IP + SSH keys and connect to the H15 over ethernet. Default `root@10.0.0.1`. |

### Explore

| Skill | What it does |
|---|---|
| [`/explain-pipeline`](skills/explain-pipeline/) | Walk through what an app does: sources, AI stages and the configuration files that feed it.|

### Modification

| Skill | What it does |
|---|---|
| [`/add-stream`](skills/add-stream/) | Add another output stream. Edits the application/profile JSON and pushes to the board. |
| [`/add-overlay`](skills/add-overlay/) | Add, modify, or remove AI analytics overlays (bboxes, masks, landmarks). Burned into the stream on-board or drawn host-side. |
| [`/swap-model`](skills/swap-model/) | Replace the AI model in an app with a different HEF (e.g. YOLOv8s → YOLOv8n).|
| [`/edit-pipeline`](skills/edit-pipeline/) | Modify the pipeline of an existing app. |
| [`/get-model`](skills/get-model/) | Propose and download a HEF from the public Hailo Model Zoo|

### Build

| Skill | What it does |
|---|---|
| [`/cross-compile`](skills/cross-compile/) | Cross-compile an app for the H15 using the Yocto SDK on the host.|

### Deploy & run

| Skill | What it does |
|---|---|
| [`/deploy`](skills/deploy/) | Push artifacts (binary, configs, HEFs) to the H15 and verify the app runs.|

### Debug

| Skill | What it does |
|---|---|
| [`/board-status`](skills/board-status/) | Snapshot the board's runtime health — temperature, power, CPU load, DRAM use, NN core utilization. Read-only over SSH. |

### Maintenance

| Skill | What it does |
|---|---|
| [`/update-claude-beta`](skills/update-claude-beta/) | Pull newer beta versions of skills/agents/`CLAUDE.md` from <https://github.com/hailo-ai/hailo15-agentic-coding> if its `HEAD` is committed after this repo's. Overwrites matching files; never deletes local-only files; never auto-commits. |

## Agents

Agents are not invoked directly — skills call them when they need scoped
expertise. Their full definitions are in [`agents/`](agents/).

| Agent | Role |
|---|---|
| [`doc-explorer`](agents/doc-explorer.md) | Reads the official Hailo PDF user guides (media library, imaging, OS, model zoo, HailoRT, board quickstarts) and returns concise excerpts with page citations. Never invents content. |
| [`pipeline-expert`](agents/pipeline-expert.md) | Owns the pipeline architecture — `generate_*_pipeline` patterns, stage types, frontend/encoder/UDP wiring, tiling+detection+aggregator structure, ZMQ metadata sender. Returns concrete `file:line` citations. |
| [`apps-expert`](agents/apps-expert.md) | Knows the reference apps under `hailo-analytics/apps/` and picks the closest one to copy/modify for a given task. Returns the app path, the files that matter inside it, and what makes it the right (or wrong) base. |

