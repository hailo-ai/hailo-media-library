---
name: deploy
description: Push artifacts (binary, configs, HEFs) to the H15 SBC and verify the app runs. Use when the user says "deploy it", "push to the board", or "run it on the device". Coordinates backup → scp → permissions → smoke test.
tools: Bash, Read
---

# /deploy — push artifacts to the H15 SBC and run

The board image already has the libs, postprocess `.so`s, and a default set of HEFs. /deploy's job is to push the *changed* artifacts (binary, edited configs, sometimes a new HEF) without breaking what's already there.

## Preconditions

- `/connect` succeeded — `ssh -o BatchMode=yes <board>` works non-interactively.
- A binary/set of config files staged on the host, ready to push.

## Procedure

1. **Decide what's changing.** Common cases:
   - Binary only (after `/swap-model` or another `main.cpp` change).
   - Configs only (after `/add-stream` or other medialib edits).
   - Both.
   - HEF (less common — usually pre-shipped on the board image).

2. **Back up first.** For anything you're overwriting, make a `.bak` on the board side. Cheap insurance. Paths below are an example (the detection case study) — substitute the actual app path you're deploying:
   ```bash
   ssh root@<board> '
     cp /home/root/apps/case_studies/detection/detection_case_study \
        /home/root/apps/case_studies/detection/detection_case_study.bak
     cp <profile_dir>/application_settings.json{,.bak}
     cp <profile_dir>/<profile>.json{,.bak}
   '
   ```

3. **Push.** Substitute the actual app/binary names and resource paths — the values below are examples (detection case study, face_landmarks HEF dir).
   - **Binary**:
     ```bash
     scp <local>/<app_name>.aarch64 root@<board>:<board_app_path>
     ssh root@<board> 'chmod +x <board_app_path>'
     # example: <board_app_path> = /home/root/apps/case_studies/detection/detection_case_study
     ```
   - **Configs** (medialib JSONs go into the active profile dir, which is determined by the medialib config — typically `/etc/imaging/cfg/hailo15<x>/<sensor>/<lens>/<res>/profiles/<name>/`):
     ```bash
     scp <local>/configs/*.json root@<board>:<profile_dir>/
     ```
   - **HEF** (if pushing a new one) — destination is the app's resources dir:
     ```bash
     scp <local>/<model>.hef root@<board>:<app_resources_dir>/
     # example: <app_resources_dir> = /home/root/apps/face_landmarks/resources
     ```

4. **Stop any running app instance.** Check for a running instance and kill it before re-running:
   ```bash
   ssh root@<board> 'pgrep -af "<app_name>" || echo no app running'
   ```

5. **Smoke test.** Run the app for a short window with FPS printing. **Important: edited medialib configs need the hash bypass** until they're hash-regenerated:
   ```bash
   ssh root@<board> '
     HAILO_MEDIA_LIB_SKIP_METADATA_CONFIG_VALIDATION=1 \
     <board_app_path> -t 12 -p 2>&1 | tail -40
   '
   # example: <board_app_path> = /home/root/apps/case_studies/detection/detection_case_study
   ```
   Look for:
   - No `Failed to initialize media library` errors.
   - `Starting.` line.
   - FPS lines for each expected `udp_sinkN` and `fpsdisplaysink_sensor0_sinkN` reaching their target framerate.
   - `Stopping.` at the end.

6. **Tell the user how to run it themselves.** Print the full one-liner including the env var, the host-side UDP ports (computed via `base_port + sink_num*2`), and any app-specific metadata ports (e.g. ZMQ for detection metadata).

## Rollback

If the smoke test fails (paths below are an example — substitute the actual app/config paths):
```bash
ssh root@<board> '
  cp <board_app_path>.bak <board_app_path>
  cp <profile_dir>/application_settings.json.bak <profile_dir>/application_settings.json
  cp <profile_dir>/<profile>.json.bak <profile_dir>/<profile>.json
'
```

## Gotchas

- **`scp` overwrites without warning.** Always back up first.
- **The metadata content_hash will reject your edits** unless you set `HAILO_MEDIA_LIB_SKIP_METADATA_CONFIG_VALIDATION=1`. Tell the user this isn't a workaround for production — for a long-lived deployment, regenerate hashes with the Tuning tool.
- **Don't push to `/usr/`** unless you know what you're doing — it can shadow the board image's libs and break unrelated tools. Apps and resources live under `/home/root/apps/`; configs under `/etc/imaging/cfg/`.
- **Don't run a HEF whose postprocess `.so` isn't on the board.** If pushing a custom postprocess, push the `.so` to `/usr/lib/hailo-post-processes/` too.
