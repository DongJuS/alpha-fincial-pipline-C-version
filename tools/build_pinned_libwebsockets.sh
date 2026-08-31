#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
lock_file="$repo_root/third_party/libwebsockets.lock.json"
work_dir="${ALPHA_LWS_WORK_DIR:-$repo_root/build/deps/libwebsockets}"
source_dir="$work_dir/source"
build_dir="$work_dir/build"
install_dir="${ALPHA_LWS_PREFIX:-$work_dir/install}"

read_lock() {
  python3 - "$lock_file" "$1" <<'PY'
import json
import sys

value = json.loads(open(sys.argv[1], encoding="utf-8").read())
for component in sys.argv[2].split("."):
    value = value[component]
print(value)
PY
}

repository="$(read_lock repository)"
commit="$(read_lock commit)"

if [[ ! -d "$source_dir/.git" ]]; then
  mkdir -p "$work_dir"
  git clone --filter=blob:none --no-checkout "$repository" "$source_dir"
fi

git -C "$source_dir" fetch --depth 1 origin "$commit"
git -C "$source_dir" checkout --detach --force "$commit"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$commit"

cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$install_dir" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DLWS_WITH_SHARED=OFF \
  -DLWS_WITH_STATIC=ON \
  -DLWS_WITH_SSL=OFF \
  -DLWS_WITH_LIBUV=OFF \
  -DLWS_WITH_LIBEVENT=OFF \
  -DLWS_WITH_GLIB=OFF \
  -DLWS_WITH_EVLIB_PLUGINS=OFF \
  -DLWS_WITHOUT_TESTAPPS=ON
cmake --build "$build_dir" --parallel
cmake --install "$build_dir"
python3 "$repo_root/tools/verify_pinned_libwebsockets.py" \
  --lock "$lock_file" --source "$source_dir" --build "$build_dir" \
  --prefix "$install_dir"

printf '%s\n' "$install_dir"
