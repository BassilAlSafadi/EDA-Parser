#!/usr/bin/env bash
#=============================================================================
# run_xsim.sh -- build and test the tool with Vivado XSim
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
xvlog -i "$rtl" "$rtl/vsim_top.v" "$tb/tb_fourvalue.v" "$tb/tb_lexer.v" \
      "$tb/tb_parser.v" "$tb/tb_elab.v" \
    || { echo "COMPILE FAILED"; exit 1; }

run_tb() {  # $1 = top module
    xelab -debug typical "$1" -s "${1}_sim" >/dev/null 2>&1
    xsim "${1}_sim" -R 2>&1 | grep -E "PASS|FAIL|ok |mismatch"
}

echo "== testbenches =="
for t in tb_fourvalue tb_lexer tb_parser tb_elab; do
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

echo "== elaboration: golden hierarchies (Task 5.1) =="
for c in half_adder_struct half_adder_rtl blocking_nonblocking counter op_battery fsm hier_ripple4; do
    xsim vsim_sim -R \
        -testplusarg "src=$gold/$c.v" \
        -testplusarg dump_hier -testplusarg "hierout=$build/$c.hier" >/dev/null 2>&1
    if diff <(tr -d '\r' < "$gold/$c.hier") <(tr -d '\r' < "$build/$c.hier") >/dev/null 2>&1
    then echo "ok   $c.hier"; else echo "FAIL $c.hier differs"; fail=1; fi
done

xsim vsim_sim -R -testplusarg "src=$gold/hier_ripple4.v" -testplusarg elab \
     -testplusarg top=full_adder 2>&1 \
    | grep -q "ELABORATE OK: top='full_adder', 3 module(s), 3 instance(s)" \
    && echo "ok   +top=full_adder elaborates that subtree only" \
    || { echo "FAIL +top= override"; fail=1; }

echo "== elaboration: negative cases (Task 5.1) =="
check_msg() {   # $1 = basename, $2 = pattern, $3 = label
    if xsim vsim_sim -R -testplusarg "src=$gold/$1.v" -testplusarg elab 2>&1 | grep -q "$2"
    then echo "ok   $3"; else echo "FAIL $3"; fail=1; fi
}
check_msg bad_undef_inst "instantiation of undefined module 'missing_mod'" \
          "undefined instantiated module is reported"
check_msg bad_recursive  "recursive instantiation of module 'ping'" \
          "instantiation cycle is reported (and terminates)"
check_msg bad_conn       "instantiated module has no port named 'zzz'" \
          "unknown named port is reported"

echo "== overflow (§9.10) =="
xsim vsim_sim -R -testplusarg "src=$gold/counter.v" -testplusarg nodecap=8 2>&1 \
    | grep -qE "arena exhausted" && echo "ok   AST arena overflow reported" \
    || { echo "FAIL overflow"; fail=1; }
xsim vsim_sim -R -testplusarg "src=$gold/hier_ripple4.v" -testplusarg elab \
     -testplusarg instcap=4 2>&1 \
    | grep -qE "instance table exhausted" && echo "ok   instance table overflow reported" \
    || { echo "FAIL instance overflow"; fail=1; }

echo "============================================"
[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
