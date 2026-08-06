#!/usr/bin/env bash
#=============================================================================
# run_modelsim.sh -- build and test the tool with ModelSim / Questa
#-----------------------------------------------------------------------------
# The course toolchain is Vivado XSim (decision 0.2); this script targets the
# ModelSim - Intel FPGA Starter Edition that is installed here.  See
# run_xsim.sh / run_iverilog.sh for the other simulators.
#
#   1. compile rtl/ + tb/ into a fresh work library
#   2. run the four testbenches (four-value, lexer, parser, elaboration)
#   3. for every golden circuit, regenerate .tokens/.ast and diff vs golden/
#   4. elaborate every golden circuit, regenerate .hier and diff vs golden/
#   5. the Task-5.1 negative cases, and the two overflow paths
#
# Exit status is non-zero if any test or golden diff fails.
#=============================================================================
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# ModelSim is a native-Windows tool: hand it Windows-style paths (F:/...), not
# the MSYS /f/... form, for +incdir, file arguments and +plusargs.
winp() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
rtl="$(winp "$here/rtl")"; tb="$(winp "$here/tb")"; gold="$(winp "$here/golden")"
build="$here/build"                 # MSYS path for shell file ops
buildw="$(winp "$build")"           # Windows path for tool arguments
work="$buildw/work"

# locate the ModelSim binaries (PATH first, then the known install dir)
MSIM_BIN="${MSIM_BIN:-/c/intelFPGA/20.1/modelsim_ase/win32aloem}"
VLIB="$(command -v vlib || echo "$MSIM_BIN/vlib")"
VLOG="$(command -v vlog || echo "$MSIM_BIN/vlog")"
VSIM="$(command -v vsim || echo "$MSIM_BIN/vsim")"

rm -rf "$build"; mkdir -p "$build"
"$VLIB" "$work" >/dev/null

echo "== compiling =="
"$VLOG" -work "$work" +incdir+"$rtl" \
    "$rtl/vsim_top.v" "$tb/tb_fourvalue.v" "$tb/tb_lexer.v" "$tb/tb_parser.v" \
    "$tb/tb_elab.v" \
    > "$build/compile.log" 2>&1
if grep -qE "^\*\* Error" "$build/compile.log"; then
    grep -E "^\*\* (Error|Warning)" "$build/compile.log"
    echo "COMPILE FAILED"; exit 1
fi
echo "compile clean"

run_tb() {   # $1 = top module name
    "$VSIM" -work "$work" -c -do "run -all; quit -f" "$1" 2>&1 \
        | grep -E "PASS|FAIL|ok |mismatch"
}

fail=0
echo "== testbenches =="
for t in tb_fourvalue tb_lexer tb_parser tb_elab; do
    out="$(run_tb "$t")"
    echo "$out"
    echo "$out" | grep -q "FAIL" && fail=1
done

echo "== golden circuits =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm; do
    "$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
        +src="$gold/$c.v" \
        +dump_tokens +tokout="$buildw/$c.tokens" \
        +dump_ast    +astout="$buildw/$c.ast" >/dev/null 2>&1
    for ext in tokens ast; do
        if diff <(tr -d '\r' < "$gold/$c.$ext") <(tr -d '\r' < "$build/$c.$ext") >/dev/null 2>&1; then
            echo "ok   $c.$ext"
        else
            echo "FAIL $c.$ext differs from golden"; fail=1
        fi
    done
done

echo "== elaboration: golden hierarchies (Task 5.1) =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm hier_ripple4; do
    "$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
        +src="$gold/$c.v" +dump_hier +hierout="$buildw/$c.hier" >/dev/null 2>&1
    if diff <(tr -d '\r' < "$gold/$c.hier") <(tr -d '\r' < "$build/$c.hier") >/dev/null 2>&1; then
        echo "ok   $c.hier"
    else
        echo "FAIL $c.hier differs from golden"; fail=1
    fi
done

# the deduced top must be overridable, and the override must prune the tree
out="$("$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
        +src="$gold/hier_ripple4.v" +elab +top=full_adder 2>&1)"
echo "$out" | grep -q "ELABORATE OK: top='full_adder', 3 module(s), 3 instance(s)" \
    && echo "ok   +top=full_adder elaborates that subtree only" \
    || { echo "FAIL +top= override"; fail=1; }

echo "== elaboration: negative cases (Task 5.1) =="
check_msg() {   # $1 = target basename, $2 = grep pattern, $3 = label
    out="$("$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
            +src="$gold/$1.v" +elab 2>&1)"
    if echo "$out" | grep -q "$2"; then echo "ok   $3"
    else echo "FAIL $3"; fail=1; fi
}
check_msg bad_undef_inst "instantiation of undefined module 'missing_mod'" \
          "undefined instantiated module is reported"
check_msg bad_recursive  "recursive instantiation of module 'ping'" \
          "instantiation cycle is reported (and terminates)"
check_msg bad_conn       "instantiated module has no port named 'zzz'" \
          "unknown named port is reported"

echo "== overflow (§9.10) =="
"$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
    +src="$gold/counter.v" +nodecap=8 2>&1 | grep -qE "arena exhausted" \
    && echo "ok   AST arena overflow reported" || { echo "FAIL overflow not reported"; fail=1; }
"$VSIM" -work "$work" -c -do "run -all; quit -f" vsim_top \
    +src="$gold/hier_ripple4.v" +elab +instcap=4 2>&1 | grep -qE "instance table exhausted" \
    && echo "ok   instance table overflow reported" \
    || { echo "FAIL instance overflow not reported"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
