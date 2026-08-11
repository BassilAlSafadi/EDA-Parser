#!/usr/bin/env bash
#=============================================================================
# run_cpp.sh -- build and test the Phase-1 tool (C++ host)
#-----------------------------------------------------------------------------
#   1. configure + build with CMake (Visual Studio 17 2022 / MSVC generator)
#   2. run the two unit tests (lexer, parser)
#   3. for every golden circuit, regenerate .tokens/.ast and diff vs golden/
#   4. exercise the arena-overflow path (§9.10)
#
# Exit status is non-zero if any test or golden diff fails.
#=============================================================================
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$here/build"
gold="$here/golden"

CMAKE="${CMAKE:-cmake}"
command -v "$CMAKE" >/dev/null 2>&1 || CMAKE="$HOME/AppData/Roaming/Python/Python313/Scripts/cmake.exe"

mkdir -p "$build"
echo "== configuring =="
"$CMAKE" -S "$here" -B "$build" -G "Visual Studio 17 2022" >"$build/configure.log" 2>&1
if [ $? -ne 0 ]; then cat "$build/configure.log"; echo "CONFIGURE FAILED"; exit 1; fi

echo "== building =="
"$CMAKE" --build "$build" --config Release >"$build/build.log" 2>&1
if [ $? -ne 0 ]; then cat "$build/build.log"; echo "BUILD FAILED"; exit 1; fi
echo "build clean"

bindir="$build/Release"
[ -f "$bindir/vsim.exe" ] || bindir="$build"   # single-config generators

fail=0
echo "== unit tests =="
for t in test_lexer test_parser; do
    out="$("$bindir/$t.exe")"
    echo "$out"
    echo "$out" | grep -q "FAIL" && fail=1
done

echo "== golden circuits =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm; do
    "$bindir/vsim.exe" \
        +src="$gold/$c.v" \
        +dump_tokens +tokout="$build/$c.tokens" \
        +dump_ast    +astout="$build/$c.ast" >/dev/null 2>&1
    for ext in tokens ast; do
        if diff <(tr -d '\r' < "$gold/$c.$ext") <(tr -d '\r' < "$build/$c.$ext") >/dev/null 2>&1; then
            echo "ok   $c.$ext"
        else
            echo "FAIL $c.$ext differs from golden"; fail=1
        fi
    done
done

echo "== overflow (§9.10) =="
"$bindir/vsim.exe" +src="$gold/counter.v" +nodecap=8 2>&1 | grep -qE "arena exhausted" \
    && echo "ok   arena overflow reported" || { echo "FAIL overflow not reported"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
