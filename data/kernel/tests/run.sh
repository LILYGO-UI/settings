#!/bin/sh
set -eu

test_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/bq27220-protocol.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM

# Compile the actual added driver implementation, not a duplicated algorithm.
python3 - "$test_root/../0001-bq27220-capacity-calibration.patch" "$test_build" <<'PY'
import pathlib
import sys

patch = pathlib.Path(sys.argv[1]).read_text()
added = "\n".join(line[1:] for line in patch.splitlines()
                  if line.startswith(" ") or
                  (line.startswith("+") and not line.startswith("+++")))
start_marker = "/*\n * BQ27220 capacity calibration,"
end_marker = "static const struct attribute_group *bq27220_capacity_groups[] = {"
assert added.count(start_marker) == 1 and added.count(end_marker) == 1
start = added.index(start_marker)
end = added.index("\n};", added.index(end_marker)) + len("\n};")
pathlib.Path(sys.argv[2], "bq27220-calibration.c").write_text(added[start:end] + "\n")
PY

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-unused-parameter \
    -Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
    -I "$test_build" "$test_root/calibration_protocol_test.cpp" \
    -o "$test_build/calibration_protocol_test"
"$test_build/calibration_protocol_test"
