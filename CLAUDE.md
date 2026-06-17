# Claude & hailo-media-library integration

A layer on top of the media-library repo. 
The goal is to let VPU customers explore docs, build and modify applications, and debug running deployments through Claude - without needing deep knowledge of the code base.

## On every prompt — check for a relevant skill first

Skills turn multi-page procedures into a single command. Before answering any
user request, scan the available-skills list for one that matches the user's
intent and invoke it via the Skill tool. Only fall back to free-form work
when no skill fits.

## Skill phases

| Phase | Examples |
|---|---|
| **Connection** | `/connect` |
| **Explore** | `/explain-pipeline`, `/get-model`|
| **Modification** | `/add-stream`, `/add-overlay`, `/swap-model`, `/edit-pipeline` |
| **Build** | `/cross-compile` |
| **Deploy & run** | `/deploy`, `/run-app` |
| **Debug** | `/board-status`|
| **Maintenance** | `/update-h15-agentic-coding` |

## Agents (called by skills for scoped expertise)

`doc-explorer`, `pipeline-expert`, `apps-expert`,`perf-expert`. 
Use them so a skill can return summarized context to the main session without flooding it.

## Documentation Reference

User guides live at `docs/guides/`. They are NOT shipped with this repo.
**At the start of every session** (on the first prompt, before anything else):
check that all 4 PDFs below exist in `docs/guides/`; download any missing one
from its URL (e.g. `curl -sfSL -o docs/guides/<name>.pdf <url>`). Do this in one command.

| Workflow | Read | Download from |
|---|---|---|
| **Media library / demo building** | `docs/guides/hailo_media_library_1.12.0_user_guide.pdf` | https://hailo-csdata.s3.eu-west-2.amazonaws.com/resources/documentation/documentation/hailo_media_library_1.12.0_user_guide.pdf |
| **ISP / imaging bugs** | `docs/guides/hailo_imaging_1.12.0_user_guide.pdf` | https://hailo-csdata.s3.eu-west-2.amazonaws.com/resources/documentation/documentation/hailo_imaging_1.12.0_user_guide.pdf |
| **OS / boot / system config** | `docs/guides/hailo_os_guide_1.12.0.pdf` | https://hailo-csdata.s3.eu-west-2.amazonaws.com/resources/documentation/documentation/hailo_os_guide_1.12.0.pdf |
| **Board setup / quickstart** | `docs/guides/hailo15l_sbc_2.x_quick_start_guide_1.3.pdf` | https://hailo-csdata.s3.eu-west-2.amazonaws.com/resources/documentation/documentation/hailo15l_sbc_2.x_quick_start_guide_1.3.pdf |

When a session involves one of these workflows, use 'doc-explorer' to read the relevant PDF at the start.