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
| [`/add-osd`](skills/add-osd/) | Add, modify, or remove AI analytics overlays (bboxes, masks, landmarks). Burned into the stream on-board or drawn host-side. |
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

## Agents

Agents are not invoked directly — skills call them when they need scoped
expertise. Their full definitions are in [`agents/`](agents/).

| Agent | Role |
|---|---|
| [`doc-explorer`](agents/doc-explorer.md) | Reads the official Hailo PDF user guides (media library, imaging, OS, model zoo, HailoRT, board quickstarts) and returns concise excerpts with page citations. Never invents content. |
| [`pipeline-expert`](agents/pipeline-expert.md) | Owns the pipeline architecture — `generate_*_pipeline` patterns, stage types, frontend/encoder/UDP wiring, tiling+detection+aggregator structure, ZMQ metadata sender. Returns concrete `file:line` citations. |
| [`apps-expert`](agents/apps-expert.md) | Knows the reference apps under `hailo-analytics/apps/` and picks the closest one to copy/modify for a given task. Returns the app path, the files that matter inside it, and what makes it the right (or wrong) base. |

