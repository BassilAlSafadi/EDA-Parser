#!/usr/bin/env bash
#=============================================================================
# run_tests.sh -- run every demo test case in test/ against vsim.exe and
# diff the simulator's output against the expected values in each .vec file.
#
# Usage:  test/run_tests.sh            (uses build/Release/vsim.exe)
#         RUN_TESTS_BIN=path/to/vsim.exe test/run_tests.sh
#=============================================================================
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$here"

bin="${RUN_TESTS_BIN:-$here/build/Release/vsim.exe}"
[ -f "$bin" ] || bin="$here/build/vsim.exe"
if [ ! -f "$bin" ]; then
    echo "vsim.exe not found -- build it first with run/run_cpp.sh"
    exit 1
fi

fail=0
for d in "$here"/test/testcase*/; do
    name="$(basename "$d")"
    v="$(ls "$d"*.v 2>/dev/null | head -n1)"
    vec="$(ls "$d"*.vec 2>/dev/null | head -n1)"
    if [ -z "$v" ] || [ -z "$vec" ]; then
        echo "SKIP $name (missing .v or .vec)"
        continue
    fi

    echo "== $name =="
    out="$("$bin" +src="$v" +sim +vec="$vec" 2>&1)"
    echo "$out"

    if echo "$out" | grep -q "SIMULATE OK"; then
        echo "ok   $name simulated cleanly"
    else
        echo "FAIL $name did not report SIMULATE OK"
        fail=1
    fi
    echo
done

[ "$fail" = 0 ] && echo "ALL DEMO TESTS PASSED" || echo "SOME DEMO TESTS FAILED"
exit $fail
