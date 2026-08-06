#!/usr/bin/env bash
#=============================================================================
# run_iverilog.sh -- build and test the tool with Icarus Verilog
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
"$IVL" -g2005 -I "$rtl" -s tb_elab      -o "$build/tbelab.vvp" "$tb/tb_elab.v"      || exit 1

echo "== testbenches =="
for v in "$build/tb4.vvp" "$build/tblex.vvp" "$build/tbpar.vvp" "$build/tbelab.vvp"; do
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

echo "== elaboration: golden hierarchies (Task 5.1) =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm hier_ripple4; do
    "$VVP" "$build/tool.vvp" +src="$gold/$c.v" \
        +dump_hier +hierout="$build/$c.hier" >/dev/null 2>&1
    if diff <(tr -d '\r' < "$gold/$c.hier") <(tr -d '\r' < "$build/$c.hier") >/dev/null 2>&1
    then echo "ok   $c.hier"; else echo "FAIL $c.hier differs"; fail=1; fi
done

"$VVP" "$build/tool.vvp" +src="$gold/hier_ripple4.v" +elab +top=full_adder 2>&1 \
    | grep -q "ELABORATE OK: top='full_adder', 3 module(s), 3 instance(s)" \
    && echo "ok   +top=full_adder elaborates that subtree only" \
    || { echo "FAIL +top= override"; fail=1; }

echo "== elaboration: negative cases (Task 5.1) =="
check_msg() {   # $1 = basename, $2 = pattern, $3 = label
    if "$VVP" "$build/tool.vvp" +src="$gold/$1.v" +elab 2>&1 | grep -q "$2"
    then echo "ok   $3"; else echo "FAIL $3"; fail=1; fi
}
check_msg bad_undef_inst "instantiation of undefined module 'missing_mod'" \
          "undefined instantiated module is reported"
check_msg bad_recursive  "recursive instantiation of module 'ping'" \
          "instantiation cycle is reported (and terminates)"
check_msg bad_conn       "instantiated module has no port named 'zzz'" \
          "unknown named port is reported"

echo "== overflow (§9.10) =="
"$VVP" "$build/tool.vvp" +src="$gold/counter.v" +nodecap=8 2>&1 \
    | grep -qE "arena exhausted" && echo "ok   AST arena overflow reported" \
    || { echo "FAIL overflow"; fail=1; }
"$VVP" "$build/tool.vvp" +src="$gold/hier_ripple4.v" +elab +instcap=4 2>&1 \
    | grep -qE "instance table exhausted" \
    && echo "ok   instance table overflow reported" \
    || { echo "FAIL instance overflow"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
