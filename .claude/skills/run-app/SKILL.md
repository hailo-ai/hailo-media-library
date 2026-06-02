---
name: run-app
description: Run a hailo-media-library app on the H15 board and display it. Use when the user says "run the detection app", "run single stream / face landmarks / DPM and show it", "launch app X on the board", or "display it in the viewer". Default display is the host-side analytic viewer (UDP video + ZMQ metadata overlays). Launches the app detached with a long timeout and points a viewer at each sink.
tools: Bash, Read
---

# /run-app — run a board app and display it

Run an app on the H15 SBC and show its stream via the host **analytic viewer** (`tools/analytic_viewer/`): UDP video + ZMQ metadata overlays. Needs `/connect` done and the binary already on the board (else `/cross-compile` + `/deploy`).

## Procedure

1. **Find the binary.** `/home/root/apps/case_studies/<app>/<app>_case_study` (e.g. `detection`, `single_stream`, `dynamic_privacy_mask`) or `/home/root/apps/<app>/<app>_app` (e.g. `face_landmarks`).

2. **Launch detached** (`-t` is mandatory — default is 60 s, then it exits; `100000` ≈ 27 h). The trailing `… &` detaches the ssh client so the call returns instantly:
   ```bash
   ssh root@10.0.0.1 'cd <app_dir> && nohup setsid ./<binary> -t 100000 \
       > /tmp/<app>.log 2>&1 < /dev/null & disown' < /dev/null > /dev/null 2>&1 &
   ```
   Then **verify in a separate call** (healthy = `FPS: ~30`, `DROP RATE: 0` per sink):
   ```bash
   ssh root@10.0.0.1 'ps -e -o pid,etime,comm | grep <prefix> || echo DEAD; tail -5 /tmp/<app>.log'
   ```

3. **Display it — analytic viewer.** Decide the setup path first:
   - **If you don't know whether the viewer is already set up on this host, ask the user: "Is this your first time using the analytic viewer?"**
   - **First time → install it.** Follow the media library user guide **§11.1.3** (`docs/guides/hailo_media_library_1.11.0_user_guide.pdf`, read via `doc-explorer`). In short: install the system packages PyGObject + cairo (Debian/Ubuntu: `sudo apt install python3-gi python3-gi-cairo gir1.2-gtk-3.0`), then the Python deps from `tools/analytic_viewer/requirements.txt`. **Ask whether to install those into a virtualenv (recommended) or user site-packages:**
     - **venv (recommended):** `python3 -m venv tools/analytic_viewer/.venv && tools/analytic_viewer/.venv/bin/pip install -r tools/analytic_viewer/requirements.txt` — then launch the viewer with `tools/analytic_viewer/.venv/bin/python3`.
     - **user site-packages:** `python3 -m pip install --user -r tools/analytic_viewer/requirements.txt`
   - **Not first time, and you know the venv** → just activate it and run (`tools/analytic_viewer/.venv/bin/python3`, or `source .../.venv/bin/activate`).
   - **Not first time, but you don't know which venv** → ask the user where the viewer's environment is before running.

   Launch one viewer per sink, on `DISPLAY=:1` (use the venv's `python3` if there is one):
   ```bash
   cd tools/analytic_viewer
   DISPLAY=:1 setsid python3 app_analytic_draw_client.py --udp-port <port> \
       > /tmp/viewer_<port>.log 2>&1 < /dev/null & disown
   ```
   The viewer defaults to UDP `10.0.0.2:<port>` for video and ZMQ `tcp://10.0.0.1:7000` for metadata — matching the app defaults, so usually no extra flags are needed. Useful ones: `--metadata-transport ws`, `--output-resolution 1080p`, `--face-landmark-filter {0,1,2}`.

4. **Confirm the pipe** (viewer log is block-buffered, don't trust it):
   ```bash
   ss -uap | grep ':<port>'
   ss -tnp | grep '10.0.0.1:7000'
   ```

## Sink → port math

`5000 + sink_num*2` → `sink0`=5000, `sink1`=5002, `sink2`=5004. One viewer per port.

## Gotchas

- Viewer draws boxes/labels/landmarks from ZMQ metadata, **not** segmentation masks — privacy masks are burned into the video on-board.
- No ZMQ is fine for pure-vision apps (e.g. `single_stream`) — clean video, no overlays.
- Stop viewers/app **by PID**, never `pkill -f` (self-kills the local shell).
- Window only appears once frames arrive.

Report: app PID, sink→port mapping, and which overlays to expect.
