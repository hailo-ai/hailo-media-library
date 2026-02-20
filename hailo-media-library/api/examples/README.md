# Media Library API Examples

This document provides an overview of the Media Library API examples for the validation team.

## Directory Structure

```
examples/
├── basic_h264_recording_to_file/   Basic video recording
├── osd/                            On-Screen Display overlays
├── osd_privacy_example/            OSD + Privacy masks
├── profile_switching_example/      Profile switching with callbacks
├── profile_switching/              Simple profile switching loop
├── dynamic_controls_example/       Runtime encoder tuning
├── resolution_change_example/      Dynamic resolution changes
├── rotation_example/               Image rotation (0/90/180/270)
├── common/                         Shared utilities
├── calculate_text_size_osd.cpp     OSD text dimension calculator
├── config_tuning_gst_application.cpp   ISP configuration tuning
└── frontend_example.cpp            DEPRECATED - see modular examples
```

## Examples Overview

### 1. Basic H.264 Recording (`basic_h264_recording_to_file/`)

**File:** `basic_h264_recording_to_file.cpp`

**What it does:**

-   Initializes the MediaLibrary pipeline
-   Records H.264 video to file for 30 seconds
-   Monitors and displays FPS during recording

**Key APIs demonstrated:**

-   `MediaLibrary::create()`
-   `frontend->subscribe()`
-   Pipeline initialization and cleanup

**Test focus:** Verify basic video capture and encoding workflow.

---

### 2. On-Screen Display (`osd/`)

**File:** `osd_example.cpp`

**What it does:**

-   Adds text overlays on video stream
-   Dynamically updates text content (iteration counter)
-   Configures font settings and overlay positioning

**Key APIs demonstrated:**

-   `osd::add_overlay()`
-   `osd::set_overlay()`
-   OSD text configuration (font, size, color)

**Test focus:** Verify OSD text rendering and dynamic updates.

---

### 3. OSD + Privacy Masks (`osd_privacy_example/`)

**File:** `osd_privacy_example.cpp`

**What it does:**

-   Creates multiple static privacy masks (polygon regions)
-   Supports pixelization and solid color masking modes
-   Dynamically updates mask positions
-   Adds custom ARGB and A420 overlays programmatically

**Key APIs demonstrated:**

-   Privacy mask creation and configuration
-   Pixelization vs solid color masking toggle
-   Custom overlay buffer management

**Test focus:** Verify privacy mask functionality, including dynamic updates and different masking modes.

---

### 4. Profile Switching with Callbacks (`profile_switching_example/`)

**File:** `profile_switching_example.cpp`

**What it does:**

-   Switches between imaging profiles:
    -   High_Dynamic_Range (HDR)
    -   Lowlight (Night Mode)
    -   Daylight
-   Handles profile restriction callbacks
-   Displays current profile via OSD
-   Persists settings across profile switches
-   Switches encoder types (H.264 to JPEG)

**Key APIs demonstrated:**

-   `set_imaging_profile()`
-   Profile switch callbacks
-   Settings persistence

**Test focus:** Verify profile switching, callback mechanisms, and encoder type changes.

---

### 5. Simple Profile Switching (`profile_switching/`)

**File:** `profile_switching.cpp`

**What it does:**

-   Takes profile names as command-line arguments
-   Cycles through profiles with 10-second intervals
-   Demonstrates repeated profile transitions

**Usage:**

```bash
./profile_switching High_Dynamic_Range Lowlight Daylight
```

**Test focus:** Verify basic profile switching in a loop without callbacks.

---

### 6. Dynamic Encoder Controls (`dynamic_controls_example/`)

**File:** `dynamic_controls_example.cpp`

**What it does:**

-   Dynamically adjusts encoder bitrate (H.264/H.265)
-   Modifies JPEG quality at runtime
-   Configures bitrate monitor periods
-   Toggles frontend features (e.g., dewarp)

**Key APIs demonstrated:**

-   Encoder parameter modification
-   Bitrate monitoring configuration
-   Frontend feature toggling

**Test focus:** Verify runtime encoder tuning without profile switches.

---

### 7. Resolution Change (`resolution_change_example/`)

**File:** `resolution_change_example.cpp`

**What it does:**

-   Changes encoder input resolution at runtime
-   Transitions: 3840x2160 -> 1920x1080 -> 1280x720 -> back
-   Handles both H.264 and JPEG encoders
-   Recomputes OSD overlays after resolution change

**Key APIs demonstrated:**

-   `set_resolution()`
-   Encoder settings update
-   OSD recomputation for new dimensions

**Test focus:** Verify dynamic resolution changes and OSD adaptation.

---

### 8. Image Rotation (`rotation_example/`)

**File:** `rotation_example.cpp`

**What it does:**

-   Rotates image output (0, 90, 180, 270 degrees)
-   Swaps encoder width/height for 90/270 rotations
-   Updates OSD overlays for rotated dimensions

**Key APIs demonstrated:**

-   Rotation settings in application config
-   Encoder dimension swapping
-   OSD position recalculation

**Test focus:** Verify rotation functionality and coordinate transformations.

---

### 9. Text Size Calculator (`calculate_text_size_osd.cpp`)

**What it does:**

-   Calculates text dimensions for OSD elements
-   Standalone utility tool

**Usage:**

```bash
./calculate_text_size_osd <font_path> <font_size> <text> <line_thickness>
```

**Output:** Width and height required for the text overlay.

**Test focus:** Verify OSD text dimension calculations.

---

### 10. ISP Configuration Tuning (`config_tuning_gst_application.cpp`)

**What it does:**

-   Manages ISP configuration files
-   Switches imaging profiles via ConfigManagerInteractor
-   Handles symlink management for 3A configs
-   Tunes sensor settings

**Key APIs demonstrated:**

-   `ConfigManagerInteractor`
-   3A configuration (Auto Exposure, Auto Focus, Auto White Balance)
-   Symlink management

**Test focus:** Verify ISP configuration management and profile tuning.

---

## Deprecated Example

### Frontend Example (`frontend_example.cpp`)

**Status:** DEPRECATED

This monolithic example has been refactored into the modular examples listed above. It now serves as a placeholder directing users to the appropriate individual examples.

---

## Common Utilities (`common/`)

**Files:** `common.hpp`, `common.cpp`

Shared code used by the modular examples:

-   Signal handling (Ctrl+C)
-   File I/O utilities
-   Pipeline initialization helpers
-   Frontend-to-encoder connection
-   Encoder output subscription
-   Resource cleanup

---

## Running Examples on Target

Examples are built and deployed to the target device. After deployment:

```bash
# SSH to target
ssh root@<target-ip>

# Navigate to examples directory
cd /usr/bin/

# Run an example
./basic_h264_recording_to_file
```

## Configuration Files

Examples typically require configuration JSON files. Check each example directory for:

-   `*_config.json` - Application configuration
-   Ensure sensor and 3A config files are properly linked

## Output Files

All examples save H.264 encoded video files to the target device:

| Example                   | Output Location                                       |
| ------------------------- | ----------------------------------------------------- |
| basic_recording           | `/var/volatile/tmp/frontend_example_{stream_id}.h264` |
| osd_example               | `/home/root/osd_{stream_id}.h264`                     |
| osd_privacy               | `/home/root/osd_privacy_{stream_id}.h264`             |
| profile_switching_example | `/home/root/profile_switching_{stream_id}.h264`       |
| profile_switching         | `/var/volatile/tmp/frontend_example_{stream_id}.h264` |
| dynamic_controls          | `/home/root/dynamic_controls_{stream_id}.h264`        |
| resolution_change         | `/home/root/resolution_change_{stream_id}.h264`       |
| rotation                  | `/home/root/rotation_{stream_id}.h264`                |

## Validation Checklist

| Example                   | How to Validate                                                          |
| ------------------------- | ------------------------------------------------------------------------ |
| basic_h264_recording      | Play output file, verify ~30s video, check FPS in console                |
| osd                       | Play output file, verify text overlay visible and counter updating       |
| osd_privacy               | Play output file, verify blurred/masked regions visible                  |
| profile_switching_example | Play output file, observe color/brightness changes between HDR/Night/Day |
| profile_switching         | No crash during run, profile switch logs in console                      |
| dynamic_controls          | Compare file sizes or visual quality at different bitrates               |
| resolution_change         | Use `ffprobe` to verify resolution changes in the stream                 |
| rotation                  | Play output file, verify image rotates correctly                         |
| calculate_text_size       | Check console prints width/height values                                 |
| config_tuning             | Verify ISP config symlinks updated                                       |

### Quick Validation Commands

```bash
# Copy file from target to host for playback
scp root@<target-ip>:/var/volatile/tmp/frontend_example_sink0.h264 .

# Check video info
ffprobe -v error -show_entries format=duration:stream=width,height output.h264

# Play video
ffplay output.h264
```
