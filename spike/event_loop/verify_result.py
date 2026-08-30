#!/usr/bin/env python3
"""Verify the P0 event-loop spike's result and linked loop ownership."""

import json
import platform
import subprocess
import sys
from pathlib import Path

REQUIRED_TRUE = ("backpressure", "cancellation", "request_result_match", "clean_shutdown")


def verify(result_path: Path, binary_path: Path) -> None:
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["schema_version"] == 1
    assert result["loop_owner"] == "libwebsockets"
    assert result["service_threads"] == 1
    assert result["worker_threads"] == 0
    assert result["queue_capacity"] == 4
    assert result["alternate_event_loop"] is False
    for field in REQUIRED_TRUE:
        assert result[field] is True, field
    for field in ("roundtrip", "pipeline_abort", "pipeline_sync", "recovered"):
        assert result["postgres"][field] is True, f"postgres.{field}"
    for field in ("roundtrip", "disconnect", "recovered"):
        assert result["redis"][field] is True, f"redis.{field}"
    assert result["curl_multi"] == {"roundtrip": True, "status": 200}

    command = ["otool", "-L", str(binary_path)] if platform.system() == "Darwin" else [
        "ldd", str(binary_path)
    ]
    linked = subprocess.run(command, check=True, capture_output=True, text=True).stdout.lower()
    assert "websockets" in linked
    assert "libuv" not in linked
    assert "libevent" not in linked


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: verify_result.py RESULT_JSON SPIKE_BINARY")
    verify(Path(sys.argv[1]), Path(sys.argv[2]))
    print("event-loop spike evidence: PASS")
