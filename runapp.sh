#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat <<EOF
Usage:
  $0 [h264|h265]
  $0 camera [h264|h265] [server options...]
  $0 mkv FILE [server options...]
  $0 --mkv FILE [server options...]
  $0 [h264|h265] --sub-mkv FILE [server options...]

Examples:
  $0 h264
  $0 h264 --mpp
  $0 mkv 1000_55_60.mkv
  $0 mkv 0391_53_50.mkv --port 8555
  $0 mkv 1000_55_60.mkv --sub-resize --secondary-mount /preview --secondary-width 640 --secondary-height 360
  $0 mkv 0391_53_50.mkv --sub-mkv 1000_55_60.mkv
  $0 --mpp --sub-mkv 1000_55_60_360p.mkv
EOF
}

run_server() {
    if [[ -n "${GST_DEBUG:-}" ]]; then
        exec sudo GST_DEBUG="$GST_DEBUG" ./build/rtsp_server "$@"
    fi
    exec sudo ./build/rtsp_server "$@"
}

case "${1:-camera}" in
    h264|h265)
        codec="$1"
        shift
        run_server --both --codec "$codec" "$@"
        ;;
    camera)
        if (( $# > 0 )); then
            shift
        fi
        codec="h264"
        if [[ "${1:-}" == "h264" || "${1:-}" == "h265" ]]; then
            codec="$1"
            shift
        elif [[ -n "${1:-}" && "$1" != --* ]]; then
            echo "Invalid camera codec: $1" >&2
            usage >&2
            exit 1
        fi
        run_server --both --codec "$codec" "$@"
        ;;
    mkv)
        if (( $# < 2 )); then
            echo "MKV file name is required" >&2
            usage >&2
            exit 1
        fi
        mkv_file="$2"
        shift 2
        if [[ -n "${1:-}" && "$1" != --* ]]; then
            echo "Unknown MKV option: $1" >&2
            usage >&2
            exit 1
        fi
        run_server --mkv "$mkv_file" "$@"
        ;;
    --help|-h)
        usage
        ;;
    --*)
        run_server "$@"
        ;;
    *)
        echo "Unknown mode: $1" >&2
        usage >&2
        exit 1
        ;;
esac
