#!/usr/bin/env bash
set -euo pipefail

NFS_ROOT="/mnt/hailo15_nfs"
SDK_ENV="/home/nitzano/GitRepositories/media-library/tools/cross_compiler/toolchain/environment-setup-armv8a-poky-linux"
PP_PATH="/usr/share/gcc/python" # Typical location in Yocto SDKs

CORE_INPUT="${1:-}"

if [[ -z "$CORE_INPUT" || ! -f "$CORE_INPUT" ]]; then
    echo "Usage: $0 <path_to_core_dump>"
    exit 1
fi

# 1. Detection & Local Copy
WORKING_CORE="$CORE_INPUT"
if [[ "$CORE_INPUT" == "$NFS_ROOT"* ]]; then
    WORKING_CORE="/tmp/$(basename "$CORE_INPUT")"
    echo ">> Copying core to local /tmp for speed..."
    cp "$CORE_INPUT" "$WORKING_CORE"
fi

TARGET_EXE_PATH=$(file --parameter elf_phnum=4096 "$WORKING_CORE" | grep -oP "execfn: '\K[^']+")
FULL_EXE_PATH="${NFS_ROOT}${TARGET_EXE_PATH}"

# 2. Pretty Printer Pre-flight Check
PP_EXISTS=true
if [ ! -d "$PP_PATH" ]; then
    PP_EXISTS=false
    echo "!! Warning: Pretty printers not found at $PP_PATH"
    echo "!! STL containers (vectors, maps) will look messy."
fi

# 3. Create the GDB script
GDB_INIT=$(mktemp)
cat <<EOF > "$GDB_INIT"
# Security: Allow GDB to load configs from the NFS
set auto-load safe-path /

# Pretty printing logic
python
import os
import sys
pp_path = '$PP_PATH'
if os.path.exists(pp_path):
    sys.path.insert(0, pp_path)
    try:
        from libstdcxx.v6.printers import register_libstdcxx_printers
        register_libstdcxx_printers(None)
        print(">> Pretty printers loaded successfully.")
    except Exception as e:
        print(f">> Failed to register printers: {e}")
else:
    print(">> Skipping pretty printers (path not found).")
end

# Essential Stability Settings
set pagination off
set print pretty on
set print frame-arguments all
set print object on
set print static-members on

# Path Logic
set sysroot $NFS_ROOT
set debug-file-directory $NFS_ROOT/usr/lib/debug
# Extra search path in case libraries are in weird NFS spots
set solib-search-path $NFS_ROOT/usr/lib:$NFS_ROOT/lib

file $FULL_EXE_PATH
core-file $WORKING_CORE

# Crucial missing piece: Solib absolute prefix
set solib-absolute-prefix $NFS_ROOT

echo \n--- BACKTRACE ---\n
bt
EOF

# 4. Launch
source "$SDK_ENV"
aarch64-poky-linux-gdb -x "$GDB_INIT"

rm "$GDB_INIT"
