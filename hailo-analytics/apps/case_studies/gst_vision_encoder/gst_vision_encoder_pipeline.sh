#!/bin/bash
set -e

CURRENT_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

function init_variables() {
    readonly RESOURCES_DIR="${CURRENT_DIR}/resources"
    readonly DEFAULT_MEDIALIB_CONFIG_PATH="/etc/imaging/cfg/medialib_configs/gst_example_medialib_config.json"
    readonly DEFAULT_UDP_HOST_IP="10.0.0.2"
    readonly DEFAULT_UDP_PORT_SINK0=5000
    readonly DEFAULT_UDP_PORT_SINK1=5002
    medialib_config_path=$DEFAULT_MEDIALIB_CONFIG_PATH
    udp_host_ip=$DEFAULT_UDP_HOST_IP
    udp_port_sink0=$DEFAULT_UDP_PORT_SINK0
    udp_port_sink1=$DEFAULT_UDP_PORT_SINK1
    sync_pipeline=false

    max_buffers_size=5

    print_gst_launch_only=false
    additional_parameters=""
}

function print_usage() {
    echo "GStreamer Vision + Encoder pipeline usage:"
    echo ""
    echo "Options:"
    echo "  --help                          Show this help"
    echo "  --show-fps                      Print fps"
    echo "  --print-gst-launch              Print gst-launch command without running"
    echo "  --config-file-path CONFIG       Media library configuration path (default: $DEFAULT_MEDIALIB_CONFIG_PATH)"
    echo "  --host-ip IP                    Host IP address for UDP output (default: $DEFAULT_UDP_HOST_IP)"
    exit 0
}

function parse_args() {
    while test $# -gt 0; do
        case "$1" in
            --help|-h)
                print_usage
                ;;
            --print-gst-launch)
                print_gst_launch_only=true
                ;;
            --show-fps)
                echo "Printing fps"
                additional_parameters="-v | grep hailo_display"
                ;;
            --config-file-path)
                medialib_config_path="$2"
                shift
                ;;
            --host-ip)
                udp_host_ip="$2"
                shift
                ;;
            *)
                echo "Received invalid argument: $1"
                print_usage
                ;;
        esac
        shift
    done
}

init_variables $@
parse_args $@

UDP_SINK0="udpsink host=$udp_host_ip port=$udp_port_sink0"
UDP_SINK1="udpsink host=$udp_host_ip port=$udp_port_sink1"

PIPELINE="gst-launch-1.0 \
    gsthailovision name=vision config-path=$medialib_config_path \
    vision.sink0 ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    gsthailoencoder stream-id=sink0 ! \
    tee name=udp_tee_sink0 \
    udp_tee_sink0. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' ! \
        $UDP_SINK0 name=udp_sink0 sync=$sync_pipeline \
    udp_tee_sink0. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        fpsdisplaysink fps-update-interval=2000 video-sink=fakesink name=hailo_display_sink0 sync=$sync_pipeline text-overlay=false \
    vision.sink1 ! \
    queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
    gsthailoencoder stream-id=sink1 ! \
    tee name=udp_tee_sink1 \
    udp_tee_sink1. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        rtph264pay ! 'application/x-rtp, media=(string)video, encoding-name=(string)H264' ! \
        $UDP_SINK1 name=udp_sink1 sync=$sync_pipeline \
    udp_tee_sink1. ! \
        queue leaky=no max-size-buffers=$max_buffers_size max-size-bytes=0 max-size-time=0 ! \
        fpsdisplaysink fps-update-interval=2000 video-sink=fakesink name=hailo_display_sink1 sync=$sync_pipeline text-overlay=false \
    ${additional_parameters}"

echo "Running GStreamer Vision + Encoder pipeline"
echo "  Config: $medialib_config_path"
echo "  UDP sink0 -> $udp_host_ip:$udp_port_sink0"
echo "  UDP sink1 -> $udp_host_ip:$udp_port_sink1"
echo ""
echo "${PIPELINE}"

if [ "$print_gst_launch_only" = true ]; then
    exit 0
fi

eval ${PIPELINE}
