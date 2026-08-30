#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${ALPHA_SPIKE_BUILD_DIR:-$repo_root/build/event-loop}"
result_file="${ALPHA_SPIKE_RESULT:-$build_dir/result.json}"

if command -v brew >/dev/null 2>&1; then
  brew_prefix="$(brew --prefix)"
  export PKG_CONFIG_PATH="$brew_prefix/opt/libpq/lib/pkgconfig:$brew_prefix/opt/curl/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

cmake -S "$repo_root/spike/event_loop" -B "$build_dir"
cmake --build "$build_dir" --parallel
python3 -m http.server 58080 --bind 127.0.0.1 --directory "$repo_root/spike/event_loop" \
  >"$build_dir/http.log" 2>&1 &
http_pid=$!
trap 'kill "$http_pid" 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do
  if curl --fail --silent http://127.0.0.1:58080/ >/dev/null; then
    break
  fi
  sleep 0.05
done
"$build_dir/alpha_event_loop_spike" >"$result_file"
python3 "$repo_root/spike/event_loop/verify_result.py" \
  "$result_file" "$build_dir/alpha_event_loop_spike"
