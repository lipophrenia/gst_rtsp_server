#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export GST_DEBUG=5
exec "$SCRIPT_DIR/runapp.sh" "$@"
