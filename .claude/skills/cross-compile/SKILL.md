---
name: cross-compile
description: Cross-compile a hailo-media-library app for the H15 SBC using the Yocto SDK on the host. Use when the user has modified main.cpp (e.g. via /swap-model) and needs an aarch64 binary, or asks "build it for the board". Does NOT require building the whole library tree — links against the prebuilt libs in the SDK sysroot.
tools: Bash, Read, Glob
---

# /cross-compile — build aarch64 binary for the H15 SBC

The Yocto SDK ships a full sysroot with all hailo-media-library libs prebuilt. For a single-app rebuild (typical case after `/swap-model` or any small `main.cpp` change), you do **not** need to build the whole library — just compile the one source file against the SDK's sysroot.

## Inputs

- Path to the modified source file.
- Optional: target board (h15h vs h15l). Default to whichever SDK is installed on the host.

## Procedure

1. **Locate the SDK.** Look for `environment-setup-armv8a-poky-linux` somewhere under the user's working area.
   ```
   /opt/poky/4.0.23/                    # default install location
   ```
   If the search fails (no `environment-setup-armv8a-poky-linux` found in the usual places), **ask the user where the toolchain is located** before proceeding — do not assume a path or jump to installing a new SDK. Only if the user confirms no SDK is installed, install it from the BSP package — look for `.../prebuilt/sbc/sdk/poky-glibc-x86_64-core-image-minimal-armv8a-hailo15<arch>-sbc-toolchain-*.sh`. It's a 2.5 GB self-extractor:
   ```bash
   <installer>.sh -y -d <destination-path>
   ```
   Install time is several minutes.

2. **Source the env.** This sets `CC`, `CXX`, `CXXFLAGS`, `PKG_CONFIG_PATH`, `PKG_CONFIG_SYSROOT_DIR`, etc.
   ```bash
   . <sdk>/environment-setup-armv8a-poky-linux
   ```
   `$CXX` will be `aarch64-poky-linux-g++` plus a long list of hardening flags and `--sysroot=$SDKTARGETSYSROOT`.

3. **Compile.** A single-file build needs:
   ```bash
   $CXX $CXXFLAGS -std=c++20 -DPERFETTO_NOT_FOUND \
     $(pkg-config --cflags hailo-analytics hailo-medialib gstreamer-1.0 gstreamer-app-1.0 spdlog) \
     main.cpp -o <app_name>.aarch64 \
     $(pkg-config --libs hailo-analytics hailo-medialib gstreamer-1.0 gstreamer-app-1.0 spdlog) \
     -lpthread
   ```
   Pick `<app_name>` based on the source — e.g. the case-study name, or whatever the user calls the demo.

   - The perfetto define `-DPERFETTO_NOT_FOUND` must match the library build, otherwise headers `#error`.
   - gstreamer pkg-configs are load-bearing even if main.cpp has no `<gst/gst.h>` — analytics headers pull it in transitively.

4. **Verify the artifact.**
   ```bash
   file <app_name>.aarch64
   # → ELF 64-bit LSB pie executable, ARM aarch64, …
   ```

5. **Hand off to /deploy.**

## When to build the whole tree instead

If the user changed something inside `hailo_analytics_api/`, `media_library/`, or `hailo-postprocess/` — i.e. a library, not just a top-level `main.cpp` — single-file compile won't help; the library needs rebuilding. The Media Library User Guide §6 documents the canonical Meson flow: source the SDK env first (so `CC`/`CXX`/`PKG_CONFIG_PATH` point at the cross toolchain), then run meson with the `-Dplatform=` flag matching the target board. **The `-Dplatform` flag is required and board-specific** — defaults to `15h`, so H15L users must pass `15l` explicitly or they'll get the wrong binary.

Each tree has a slightly different build, all per §6.2–§6.4:
```bash
. <sdk>/environment-setup-armv8a-poky-linux

# hailo-media-library (§6.2)
cd hailo-media-library
meson setup build --buildtype=release --prefix=/usr -Dplatform=15h   # or -Dplatform=15l
ninja -C build
DESTDIR=/tmp/install ninja -C build install
scp -r /tmp/install/* root@<target-ip>:/

# hailo-analytics (§6.3) — heavier, can OOM
cd hailo-analytics
meson setup build --buildtype=release --prefix=/usr -Dplatform=15h \
  -Dapps_install_dir=/home/root/apps
ninja -C build -j 10 -l 10           # job-limit prevents memory exhaustion
DESTDIR=/tmp/install ninja -C build install
scp -r /tmp/install/* root@<target-ip>:/

# hailo-postprocess (§6.4)
cd hailo-postprocess
meson setup build --buildtype=release --prefix=/usr -Dplatform=15h \
  -Dpost_processes_install_dir=/usr/lib/hailo-post-processes/
ninja -C build
DESTDIR=/tmp/install ninja -C build install
scp -r /tmp/install/* root@<target-ip>:/
```
This is many minutes and produces `.so` files that also need to be deployed (replacing `/usr/lib/libhailo_analytics.so.1.11.0` etc on the board). Confirm with the user before going down that path — it's heavier. (soon there will be a /build skill for that. check if that exists already)

## Gotchas

- `meson` and `pkg-config` from the SDK come from `sysroots/x86_64-pokysdk-linux/usr/bin/`. Sourcing the env script puts them on `PATH` ahead of system tools — don't undo this.
- The SDK is **board-specific**: Always cross-compile with the SDK that matches the target.
- The sysroot pkg-configs use `prefix=/usr` — that's the *target* /usr, not the host's. `$PKG_CONFIG_SYSROOT_DIR` rewrites paths automatically.
- **Don't try to compile on the board.** It has no `gcc`/`g++`/`pkg-config` installed.
