#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export XENONFIN_SETTINGS=${XENONFIN_SETTINGS:-/workspace/setup/user/settings.json}
exec "$project_dir/build/xenonfin" "$@"
