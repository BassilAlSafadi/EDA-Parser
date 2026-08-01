#!/usr/bin/env bash
#=============================================================================
# run_xsim.sh -- build and test the Phase-1 tool with Vivado XSim
#-----------------------------------------------------------------------------
# XSim is the course toolchain (decision 0.2).  Requires xvlog/xelab/xsim on
# PATH.  Mirrors the spec §8 invocation:
#   xvlog -i rtl rtl/vsim_top.v
#   xelab -debug typical vsim_top -s vsim_sim
#   xsim vsim_sim -R -testplusarg src=... -testplusarg dump_ast ...
# Note: XSim passes run-time plusargs via -testplusarg (no leading '+').
#=============================================================================
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rtl="$here/rtl"; tb="$here/tb"; gold="$here/golden"; build="$here/build"
rm -rf "$build"; mkdir -p "$build"; cd "$build"

fail=0
echo "== compiling =="
xvlog -i "$rtl" "$rtl/vsim_top.v" "$tb/tb_fourvalue.v" "$tb/tb_lexer.v" "$tb/tb_parser.v" \
    || { echo "COMPILE FAILED"; exit 1; }

run_tb() {  # $1 = top module
    xelab -debug typical "$1" -s "${1}_sim" >/dev/null 2>&1
    xsim "${1}_sim" -R 2>&1 | grep -E "PASS|FAIL|ok |mismatch"
}

echo "== testbenches =="
for t in tb_fourvalue tb_lexer tb_parser; do
    out="$(run_tb "$t")"; echo "$out"; echo "$out" | grep -q FAIL && fail=1
done

echo "== golden circuits =="
xelab -debug typical vsim_top -s vsim_sim >/dev/null 2>&1
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm; do
    xsim vsim_sim -R \
        -testplusarg "src=$gold/$c.v" \
        -testplusarg dump_tokens -testplusarg "tokout=$build/$c.tokens" \
        -testplusarg dump_ast    -testplusarg "astout=$build/$c.ast" >/dev/null 2>&1
    for ext in tokens ast; do
        if diff <(tr -d '\r' < "$gold/$c.$ext") <(tr -d '\r' < "$build/$c.$ext") >/dev/null 2>&1
        then echo "ok   $c.$ext"; else echo "FAIL $c.$ext differs"; fail=1; fi
    done
done

echo "== overflow (§9.10) =="
xsim vsim_sim -R -testplusarg "src=$gold/counter.v" -testplusarg nodecap=8 2>&1 \
    | grep -qE "arena exhausted" && echo "ok   overflow reported" \
    || { echo "FAIL overflow"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
