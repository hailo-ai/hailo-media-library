#!/bin/bash

DEFAULT_HOST="root@10.0.0.1"
REMOTE_PATH="/tmp/medialib_snapshots"
LOCAL_PATH="/tmp"
USER_HOST="$DEFAULT_HOST"
REMOVE_AFTER_DOWNLOAD=false
LATEST_SNAPSHOT=false
ALL_SNAPSHOTS=false

# Function to get the latest snapshot
get_latest_snapshot() {
    echo "Fetching the latest snapshot from $USER_HOST..."
    
    SNAPSHOT=$(ssh "$USER_HOST" "ls -d $REMOTE_PATH/* 2>/dev/null | sort | tail -n 1 | awk -F'/' '{print \$NF}'")
    
    if [[ -z "$SNAPSHOT" ]]; then
        echo "No snapshots found."
        exit 1
    fi

    echo "Automatically selected latest snapshot: $SNAPSHOT"
}

# Function to interactively select a snapshot
select_snapshot() {
    echo "Fetching available snapshots from $USER_HOST..."
    
    SNAPSHOTS=$(ssh "$USER_HOST" "ls -d $REMOTE_PATH/* 2>/dev/null" | awk -F'/' '{print $NF}')
    
    if [[ -z "$SNAPSHOTS" ]]; then
        echo "No snapshots found."
        exit 1
    fi

    echo "Available snapshots:"
    select choice in "All snapshots" $SNAPSHOTS; do
        if [[ "$choice" == "All snapshots" ]]; then
            ALL_SNAPSHOTS=true
            SNAPSHOT_LIST=$SNAPSHOTS
            echo "Selected: All snapshots"
            break
        elif [[ -n "$choice" ]]; then
            SNAPSHOT=$choice
            echo "Selected snapshot: $SNAPSHOT"
            break
        else
            echo "Invalid selection. Try again."
        fi
    done
}

# Function to download the selected snapshot
download_snapshot() {
    if $ALL_SNAPSHOTS; then
        echo "Downloading all snapshots from $USER_HOST..."
        
        # Create a directory to store all snapshots
        ALL_SNAPSHOTS_DIR="$LOCAL_PATH/all_snapshots_$(date +%Y%m%d_%H%M%S)"
        mkdir -p "$ALL_SNAPSHOTS_DIR"
        
        for snap in $SNAPSHOT_LIST; do
            echo "Downloading snapshot '$snap'..."
            scp -r "$USER_HOST:$REMOTE_PATH/$snap" "$ALL_SNAPSHOTS_DIR/"
            
            # Remove snapshot from remote if flag is set
            if $REMOVE_AFTER_DOWNLOAD; then
                echo "Removing snapshot '$snap' from $USER_HOST..."
                ssh "$USER_HOST" "rm -rf '$REMOTE_PATH/$snap'"
            fi
            
            # Convert NV12 files for this snapshot
            LOCAL_SNAPSHOT_PATH="$ALL_SNAPSHOTS_DIR/$snap"
            convert_nv12_to_rgb
        done
        
        echo "All snapshots downloaded to: $ALL_SNAPSHOTS_DIR"
    else
        echo "Downloading snapshot '$SNAPSHOT' from $USER_HOST..."
        scp -r "$USER_HOST:$REMOTE_PATH/$SNAPSHOT" "$LOCAL_PATH/"
        
        LOCAL_SNAPSHOT_PATH="$LOCAL_PATH/$SNAPSHOT"
        echo "Snapshot downloaded to: $LOCAL_SNAPSHOT_PATH"

        # Remove snapshot from remote if flag is set
        if $REMOVE_AFTER_DOWNLOAD; then
            echo "Removing snapshot '$SNAPSHOT' from $USER_HOST..."
            ssh "$USER_HOST" "rm -rf '$REMOTE_PATH/$SNAPSHOT'"
            echo "Snapshot removed from remote machine."
        fi
    fi
}

# Function to convert NV12 files to RGB PNG
convert_nv12_to_rgb() {
    echo "Converting NV12 files to RGB PNG..."
    
    for file in "$LOCAL_SNAPSHOT_PATH"/*.nv12; do
        [ -e "$file" ] || continue  # Skip if no matching files

        if [[ "$file" =~ ([0-9]+)x([0-9]+) ]]; then
            WIDTH=${BASH_REMATCH[1]}
            HEIGHT=${BASH_REMATCH[2]}
            output_file="${file%.nv12}.png"

            gst-launch-1.0 filesrc location="$file" ! \
                rawvideoparse width=$WIDTH height=$HEIGHT format=nv12 ! \
                queue ! videoconvert ! pngenc ! filesink location="$output_file"

            echo "Converted: $file -> $output_file"
        else
            echo "Skipping: $file (resolution not detected)"
        fi
    done
}

# Parse command-line flags
while getopts "h:lr" opt; do
    case $opt in
        h) USER_HOST="$OPTARG" ;;
        l) LATEST_SNAPSHOT=true ;;
        r) REMOVE_AFTER_DOWNLOAD=true ;;
        *) echo "Usage: $0 [-h user@host] [-l (latest)] [-r (remove remote)]"; exit 1 ;;
    esac
done

# Determine snapshot selection method
if $LATEST_SNAPSHOT; then
    get_latest_snapshot
else
    select_snapshot
fi

# Execute functions
download_snapshot
# Only need to convert here if not handling all snapshots
# (for all snapshots, conversion is done in the download loop)
if ! $ALL_SNAPSHOTS; then
    convert_nv12_to_rgb
fi

echo "All operations complete."

