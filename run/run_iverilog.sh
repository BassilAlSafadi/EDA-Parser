#!/usr/bin/env bash
#=============================================================================
# run_iverilog.sh -- build and test the Phase-1 tool with Icarus Verilog
#-----------------------------------------------------------------------------
# Kept for portability (risk table §12: Icarus/XSim $value$plusargs differences).
# Requires iverilog + vvp on PATH.  Each testbench and the tool is a separate
# top module, so each is elaborated to its own .vvp.
#=============================================================================
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rtl="$here/rtl"; tb="$here/tb"; gold="$here/golden"; build="$here/build"
rm -rf "$build"; mkdir -p "$build"

IVL="${IVERILOG:-iverilog}"; VVP="${VVP:-vvp}"
fail=0

echo "== compiling =="
"$IVL" -g2005 -I "$rtl" -s vsim_top     -o "$build/tool.vvp"  "$rtl/vsim_top.v"     || exit 1
"$IVL" -g2005 -I "$rtl" -s tb_fourvalue -o "$build/tb4.vvp"   "$tb/tb_fourvalue.v"  || exit 1
"$IVL" -g2005 -I "$rtl" -s tb_lexer     -o "$build/tblex.vvp" "$tb/tb_lexer.v"      || exit 1
"$IVL" -g2005 -I "$rtl" -s tb_parser    -o "$build/tbpar.vvp" "$tb/tb_parser.v"     || exit 1

echo "== testbenches =="
for v in "$build/tb4.vvp" "$build/tblex.vvp" "$build/tbpar.vvp"; do
    out="$("$VVP" "$v" 2>&1 | grep -E 'PASS|FAIL|ok |mismatch')"
    echo "$out"; echo "$out" | grep -q FAIL && fail=1
done

echo "== golden circuits =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm; do
    "$VVP" "$build/tool.vvp" +src="$gold/$c.v" \
        +dump_tokens +tokout="$build/$c.tokens" \
        +dump_ast    +astout="$build/$c.ast" >/dev/null 2>&1
    for ext in tokens ast; do
        if diff <(tr -d '\r' < "$gold/$c.$ext") <(tr -d '\r' < "$build/$c.$ext") >/dev/null 2>&1
        then echo "ok   $c.$ext"; else echo "FAIL $c.$ext differs"; fail=1; fi
    done
done

echo "== overflow (§9.10) =="
"$VVP" "$build/tool.vvp" +src="$gold/counter.v" +nodecap=8 2>&1 \
    | grep -qE "arena exhausted" && echo "ok   overflow reported" \
    || { echo "FAIL overflow"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
