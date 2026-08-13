#!/usr/bin/env bash
#=============================================================================
# run_cpp.sh -- build and test the tool (C++ host): Phase 1 + Task 5.1
#-----------------------------------------------------------------------------
#   1. configure + build with CMake (Visual Studio 17 2022 / MSVC generator)
#   2. run the unit tests (lexer, parser, elab)
#   3. for every golden circuit, regenerate .tokens/.ast and diff vs golden/
#   4. for every golden circuit, regenerate .hier and diff vs golden/ (Task 5.1)
#   5. exercise the arena-overflow path (§9.10) and the instance-table
#      overflow path (Elaboration.md §5 #12)
#   6. run the three Task-5.1 negative fixtures (bad_undef_inst / bad_recursive
#      / bad_conn) and check each produces its documented error, positioned
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
for t in test_lexer test_parser test_elab; do
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

echo "== hierarchy (Task 5.1, Elaboration.md §4) =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm hier_ripple4; do
    "$bindir/vsim.exe" +src="$gold/$c.v" +dump_hier +hierout="$build/$c.hier" >/dev/null 2>&1
    if diff <(tr -d '\r' < "$gold/$c.hier") <(tr -d '\r' < "$build/$c.hier") >/dev/null 2>&1; then
        echo "ok   $c.hier"
    else
        echo "FAIL $c.hier differs from golden"; fail=1
    fi
done

echo "== +top= override (Elaboration.md §3.1) =="
"$bindir/vsim.exe" +src="$gold/hier_ripple4.v" +top=full_adder 2>&1 \
    | grep -qE "ELABORATE OK: top='full_adder', 3 module\(s\), 3 instance\(s\)" \
    && echo "ok   +top=full_adder prunes hier_ripple4 to a 3-instance tree" \
    || { echo "FAIL +top= override did not produce the expected tree"; fail=1; }

echo "== overflow (§9.10) =="
"$bindir/vsim.exe" +src="$gold/counter.v" +nodecap=8 2>&1 | grep -qE "arena exhausted" \
    && echo "ok   arena overflow reported" || { echo "FAIL overflow not reported"; fail=1; }

echo "== instance-table overflow (Elaboration.md §5 #12) =="
"$bindir/vsim.exe" +src="$gold/hier_ripple4.v" +instcap=4 2>&1 \
    | grep -qE "instance table exhausted" \
    && echo "ok   instance-table overflow reported" \
    || { echo "FAIL instance-table overflow not reported"; fail=1; }
"$bindir/vsim.exe" +src="$gold/hier_ripple4.v" +instcap=4 2>&1 \
    | grep -qE "ELABORATE FAILED" \
    && echo "ok   ELABORATE FAILED on instance-table exhaustion" \
    || { echo "FAIL elaboration did not report failure on exhaustion"; fail=1; }

echo "== Task-5.1 negative fixtures =="
check_bad() {
    local file="$1" expect="$2" label="$3"
    "$bindir/vsim.exe" +src="$gold/$file" 2>&1 | grep -qF "$expect" \
        && echo "ok   $label" || { echo "FAIL $label"; fail=1; }
}
check_bad bad_undef_inst.v "bad_undef_inst.v:6:3: error: instantiation of undefined module 'missing_mod'" \
    "bad_undef_inst.v: undefined module, positioned"
check_bad bad_recursive.v  "error: recursive instantiation of module 'ping'" \
    "bad_recursive.v: recursive instantiation error"
check_bad bad_conn.v       "bad_conn.v:10:3: error: instantiated module has no port named 'zzz'" \
    "bad_conn.v: bad port name, positioned"

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
