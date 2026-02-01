#!/bin/bash

# Script to manage config_tuning_gst_application without waiting
# Usage: 
#   ./manage_config_tuning.sh start <mode>  - Start the application
#   ./manage_config_tuning.sh stop          - Stop the application
#   ./manage_config_tuning.sh status        - Check application status
#   ./manage_config_tuning.sh restart <mode> - Restart with new mode

set -e

PID_FILE="/tmp/config_tuning_app.pid"
LOG_FILE="/tmp/config_tuning_app.log"
LOG_DIR="/tmp/config_tuning_app"
APP_NAME="config_tuning_gst_application"
APP_PATH="/usr/bin/${APP_NAME}"

# Function to check if process is running
is_running() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            return 0  # Running
        else
            rm -f "$PID_FILE"
            return 1  # Not running
        fi
    fi
    return 1  # Not running
}

# Function to show usage
show_usage() {
    echo "Usage: $0 {start|run|stop|status|restart} [mode]"
    echo ""
    echo "Commands:"
    echo "  start <mode>    - Start the application in the background with specified mode"
    echo "  stop            - Stop the running application"
    echo "  status          - Check if application is running"
    echo "  restart <mode>  - Stop and start with new mode"
    echo ""
    echo "Example modes: High_Dynamic_Range, Daylight, etc."
}

start_app() {
    if [ $# -ne 1 ]; then
        echo "Error: Mode required for start command"
        show_usage
        exit 1
    fi
    
    MODE="$1"
    
    if is_running; then
        echo "Application is already running (PID: $(cat $PID_FILE))"
        echo "Use 'stop' command first or 'restart' to change mode"
        exit 1
    fi
    
    # Check if the application exists
    if [ ! -x "$APP_PATH" ]; then
        echo "Error: Application not found at $APP_PATH"
        echo "Make sure the application is built and installed."
        exit 1
    fi
    
    echo "Starting config tuning application with mode: $MODE"
    
    # remove dir if exists
    rm -rf "$LOG_DIR"
    mkdir -p "$LOG_DIR"
    # Start the application directly in background and capture its PID
    nohup env MEDIALIB_LOGGER_PATH="$LOG_DIR" $APP_PATH "$MODE" > "$LOG_FILE" 2>&1 &
    APP_PID=$!
    
    # Store the application PID
    echo "$APP_PID" > "$PID_FILE"
    
    echo "Config tuning application started with PID: $APP_PID"
    echo "Stdout and Stderr log output: $LOG_FILE, medialib log output: $LOG_DIR/medialib.log"
    echo "Use '$0 stop' to stop the application"
    echo "Use '$0 status' to check status"
}

# Function to stop the application
stop_app() {
    if ! is_running; then
        echo "Application is not running"
        return 0
    fi
    
    PID=$(cat "$PID_FILE")
    echo "Stopping application (PID: $PID)..."
    
    # Send SIGTERM to the application
    if kill -TERM "$PID" 2>/dev/null; then
        # Wait up to 15 seconds for graceful shutdown
        for i in {1..15}; do
            if ! kill -0 "$PID" 2>/dev/null; then
                echo "Application stopped successfully"
                rm -f "$PID_FILE"
                return 0
            fi
            sleep 1
        done
        
        # Force kill if still running
        echo "Force killing application..."
        kill -KILL "$PID" 2>/dev/null || true
    fi
    
    rm -f "$PID_FILE"
    echo "Application stopped"
}

# Function to show status
show_status() {
    if is_running; then
        PID=$(cat "$PID_FILE")
        echo "Application is running (PID: $PID)"
        echo "Log file: $LOG_FILE"
        
        # Show last few lines of log
        if [ -f "$LOG_FILE" ]; then
            echo ""
            echo "Last 5 lines of log:"
            tail -n 5 "$LOG_FILE"
        fi
    else
        echo "Application is not running"
    fi
}

# Function to restart
restart_app() {
    if [ $# -ne 1 ]; then
        echo "Error: Mode required for restart command"
        show_usage
        exit 1
    fi
    
    MODE="$1"
    echo "Restarting application with mode: $MODE"
    
    stop_app
    sleep 2
    start_app "$MODE"
}

# Main script logic
case "${1:-}" in
    start)
        start_app "${2:-}"
        ;;
    stop)
        stop_app
        ;;
    status)
        show_status
        ;;
    restart)
        restart_app "${2:-}"
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
