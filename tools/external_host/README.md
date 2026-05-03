# External Host Tools

This directory contains tools designed to run on the host development machine (not on the embedded device). These tools enable remote debugging and video streaming from the embedded target device.

## Overview

The tools are organized into two categories:

1. **UDP Streaming Scripts** - For receiving and displaying video streams from the embedded device
2. **Debugging Tools** - For analyzing coredumps from the embedded device on the host machine

---

## UDP Streaming Tools

These scripts use GStreamer to receive UDP video streams from the embedded device and either display them or save them to a file.

### udp_stream_display.sh

Receives and displays a UDP video stream in real-time.

**Usage:**

```bash
./udp_stream_display.sh [OPTIONS]
```

**Options:**

-   `--help, -h` - Show help message
-   `--port PORT, -p PORT` - Set UDP port (default: 5000)
-   `--address ADDRESS, -a ADDRESS` - Set source IP address (default: 10.0.0.2)
-   `--encoder ENCODER, -e ENCODER` - Select encoder type: h264 or h265 (default: h264)
-   `--show-fps` - Print FPS information
-   `--print-gst-launch` - Print the GStreamer pipeline command without executing it

**Example:**

```bash
# Display H.264 stream from default address and port
./udp_stream_display.sh

# Display H.265 stream from custom address and port with FPS info
./udp_stream_display.sh -a 192.168.1.100 -p 5001 -e h265 --show-fps
```

### udp_stream_to_file.sh

Receives a UDP video stream and saves it to an MP4 file.

**Usage:**

```bash
./udp_stream_to_file.sh [OPTIONS]
```

**Options:**

-   `--help, -h` - Show help message
-   `--port PORT, -p PORT` - Set UDP port (default: 5000)
-   `--address ADDRESS, -a ADDRESS` - Set source IP address (default: 10.0.0.2)
-   `--encoder ENCODER, -e ENCODER` - Select encoder type: h264 or h265 (default: h264)
-   `--output OUTPUT, -o OUTPUT` - Set output filename (default: output.mp4)
-   `--show-fps` - Print FPS information
-   `--print-gst-launch` - Print the GStreamer pipeline command without executing it

**Example:**

```bash
# Save stream to default output.mp4
./udp_stream_to_file.sh

# Save H.265 stream to custom file
./udp_stream_to_file.sh -e h265 -o recording.mp4 -a 192.168.1.100
```

---

## Debugging Tools

These tools enable debugging of coredumps generated on the embedded device directly from the host machine, avoiding the need to debug on the resource-constrained embedded system.

### Prerequisites

-   **NFS Mount**: The embedded device's filesystem must be mounted on the host (default: `/mnt/hailo15_nfs`)
-   **Cross Toolchain**: The SDK environment must be set up with the cross-compiler toolchain
-   **GDB**: ARM GDB from the toolchain (`aarch64-poky-linux-gdb`)

### run_gdb_coredump.sh

A bash script that automates launching GDB with proper configuration for analyzing coredumps from the embedded device.

**Usage:**

```bash
./run_gdb_coredump.sh <path_to_core_dump>
```

**Example:**

```bash
# Debug a coredump from the NFS mount
./run_gdb_coredump.sh /mnt/hailo15_nfs/home/root/core

# Debug a locally copied coredump
./run_gdb_coredump.sh /tmp/camera-viewer-s.2407.core
```

**What it does:**

1. Validates the coredump file exists
2. Copies coredump to `/tmp` if it's on NFS (for faster access)
3. Extracts the executable path from the coredump
4. Sources the SDK environment
5. Configures pretty printers for STL containers
6. Launches GDB with correct paths and settings

### launch_example.json

A VS Code launch configuration for debugging coredumps directly in the IDE.

**Setup:**

1. Copy this file to your `.vscode/launch.json` or merge it with your existing configuration
2. Ensure the NFS mount point matches your setup (default: `/mnt/hailo15_nfs`)
3. Update the toolchain path if different

**Usage in VS Code:**

1. Open VS Code in the workspace
2. Go to Run and Debug (Ctrl+Shift+D)
3. Select "Debug Coredump" from the dropdown
4. Press F5 or click the green play button
5. Enter the coredump path when prompted (e.g., `/mnt/hailo15_nfs/home/root/core`)
6. Enter the executable path when prompted (e.g., `/mnt/hailo15_nfs/usr/bin/camera-viewer-server`)

**Configuration Details:**

-   **Debugger**: Cross GDB from toolchain
-   **Sysroot**: `/mnt/hailo15_nfs` (embedded device root filesystem)
-   **Debug Symbols**: `/mnt/hailo15_nfs/usr/lib/debug`
-   **Libraries**: `/mnt/hailo15_nfs/usr/lib` and `/mnt/hailo15_nfs/lib`

---

## Common Setup

### NFS Mount Setup

The debugging tools assume the embedded device filesystem is accessible via NFS. To set this up:

```bash
# Mount the embedded device filesystem
sudo mkdir -p /mnt/hailo15_nfs
sudo mount -t nfs <device_ip>:/path/to/root /mnt/hailo15_nfs
```

### SDK Environment

The debugging tools source the SDK environment from:

```
/home/nitzano/GitRepositories/media-library/tools/cross_compiler/toolchain/environment-setup-armv8a-poky-linux
```

Update the path in `run_gdb_coredump.sh` if your SDK is located elsewhere.

---

## Troubleshooting

### UDP Streaming Issues

**No video displayed:**

-   Verify the embedded device is streaming to the correct IP and port
-   Check network connectivity: `ping <device_ip>`
-   Ensure firewall allows UDP traffic on the specified port

**Poor video quality or stuttering:**

-   Check network bandwidth
-   Try the `--show-fps` option to monitor frame rate
-   Adjust queue parameters in the script if needed

### Debugging Issues

**GDB can't find symbols:**

-   Ensure NFS mount is working: `ls /mnt/hailo15_nfs`
-   Verify debug symbols exist: `ls /mnt/hailo15_nfs/usr/lib/debug`
-   Check that the executable matches the coredump version

**Pretty printers not working:**

-   Ensure Python printers exist at `/usr/share/gcc/python` in the SDK
-   Check the `PP_PATH` variable in `run_gdb_coredump.sh`

**Coredump analysis is slow:**

-   The script automatically copies NFS coredumps to `/tmp` for faster access
-   Ensure you have sufficient space in `/tmp`

---

## Examples

### Complete UDP Streaming Workflow

On the embedded device:

```bash
# Start streaming with UDP sink
gst-launch-1.0 videotestsrc ! x264enc ! rtph264pay ! udpsink host=10.0.0.1 port=5000
```

On the host machine:

```bash
# Display the stream
./udp_stream_display.sh -a 10.0.0.2 -p 5000
```

### Complete Debugging Workflow

On the embedded device:

```bash
# Enable core dumps
ulimit -c unlimited
echo "/home/root/core" > /proc/sys/kernel/core_pattern

# Run application (when it crashes, generates core dump)
./camera-viewer-server
```

On the host machine:

```bash
# Debug using command line
./run_gdb_coredump.sh /mnt/hailo15_nfs/home/root/core

# Or debug using VS Code
# 1. Copy launch_example.json to .vscode/launch.json
# 2. Press F5 in VS Code
# 3. Enter coredump and executable paths when prompted
```

---

## Notes

-   These tools are designed for development and debugging purposes
-   UDP streaming assumes the host can receive UDP packets from the embedded device
-   Debugging requires the embedded device filesystem to be accessible via NFS
-   The cross-compilation toolchain must match the binaries on the embedded device
