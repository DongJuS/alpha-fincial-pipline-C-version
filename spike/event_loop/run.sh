#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${ALPHA_SPIKE_BUILD_DIR:-$repo_root/build/event-loop}"
result_file="${ALPHA_SPIKE_RESULT:-$build_dir/result.json}"
lws_prefix="${ALPHA_LWS_PREFIX:-$repo_root/build/deps/libwebsockets/install}"

if command -v brew >/dev/null 2>&1; then
  brew_prefix="$(brew --prefix)"
  export PKG_CONFIG_PATH="$brew_prefix/opt/libpq/lib/pkgconfig:$brew_prefix/opt/curl/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

ALPHA_LWS_PREFIX="$lws_prefix" "$repo_root/tools/build_pinned_libwebsockets.sh"

cmake -S "$repo_root/spike/event_loop" -B "$build_dir" \
  -DALPHA_LWS_PREFIX="$lws_prefix"
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
