//============================================================================
// test_gate_wire.cpp -- gate-instance collection + combinational settling,
// run against real elaborated designs (including an actual golden fixture,
// not just synthetic snippets).
//============================================================================
#include "gate_wire.h"

#include <cstdio>

using namespace vsim;

static int fails = 0;

static char c(Bit b) {
    switch (b) {
        case Bit::Zero: return '0';
        case Bit::One:  return '1';
        case Bit::X:    return 'x';
        case Bit::Z:    return 'z';
    }
    return '?';
}

static void ck(bool cond, const char* msg) {
    if (!cond) { fails += 1; std::printf("FAIL: %s\n", msg); }
    else       { std::printf("ok   %s\n", msg); }
}

static void ck_bit(Bit got, Bit want, const char* msg) {
    if (got != want) {
        fails += 1;
        std::printf("FAIL: %s (got %c, want %c)\n", msg, c(got), c(want));
    } else {
        std::printf("ok   %s\n", msg);
    }
}

int main() {
    //======================================================================
    // 1. golden/half_adder_struct.v, the real fixture, not a snippet.
    //    Two gates, non-ANSI ports, named gate instances (u1/u2) -- exactly
    //    what a real target design looks like, going through the actual
    //    Task 5.1 -> Task 5.2 -> gate-wiring pipeline end to end.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_struct.v";
        v.load_src("golden/half_adder_struct.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        int nconn = v.resolve_signals();
        ck(v.n_diag == 0, "half_adder_struct.v resolves with zero diagnostics");
        ck(nconn == 4, "  4 connections flattened (2 ports each on 2 gates... "
                        "well, formally: A,B each used twice + Sum + Carry)");

        auto gates = collect_gate_instances(v);
        ck(gates.size() == 2, "collect_gate_instances finds both gates (xor u1, and u2)");

        SignalValues vals;
        vals.init(v.n_sig);
        int A = v.find_sig_local(0, "A"), B = v.find_sig_local(0, "B");
        int Sum = v.find_sig_local(0, "Sum"), Carry = v.find_sig_local(0, "Carry");
        ck(A >= 0 && B >= 0 && Sum >= 0 && Carry >= 0, "all four ports resolve in the top instance's scope");

        auto run = [&](Bit a, Bit b) {
            vals.set(A, a);
            vals.set(B, b);
            settle_gates(gates, vals);
        };

        run(Bit::Zero, Bit::Zero);
        ck_bit(vals.get(Sum), Bit::Zero, "0 xor 0 = 0 (Sum)");
        ck_bit(vals.get(Carry), Bit::Zero, "0 and 0 = 0 (Carry)");

        run(Bit::One, Bit::Zero);
        ck_bit(vals.get(Sum), Bit::One, "1 xor 0 = 1 (Sum)");
        ck_bit(vals.get(Carry), Bit::Zero, "1 and 0 = 0 (Carry)");

        run(Bit::One, Bit::One);
        ck_bit(vals.get(Sum), Bit::Zero, "1 xor 1 = 0 (Sum) -- this is the actual half-adder carry case");
        ck_bit(vals.get(Carry), Bit::One, "1 and 1 = 1 (Carry)");

        run(Bit::X, Bit::Zero);
        ck_bit(vals.get(Sum), Bit::X, "x xor 0 = x (Sum) -- unknown input propagates");
        ck_bit(vals.get(Carry), Bit::Zero, "x and 0 = 0 (Carry) -- 0 still dominates AND even with x present");
    }

    //======================================================================
    // 2. not/buf with multiple outputs: `not(y1, y2, a);` drives BOTH y1
    //    and y2 from the single input a (LRM 7.2 terminal order).
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, output y1, output y2); not(y1, y2, a); endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto gates = collect_gate_instances(v);
        ck(gates.size() == 1, "one gate collected");
        ck(gates[0].out_sigs.size() == 2 && gates[0].in_sigs.size() == 1,
           "multi-output not: 2 outputs, 1 input (last terminal is the input, LRM 7.2)");

        SignalValues vals;
        vals.init(v.n_sig);
        int a = v.find_sig_local(0, "a"), y1 = v.find_sig_local(0, "y1"), y2 = v.find_sig_local(0, "y2");
        vals.set(a, Bit::One);
        settle_gates(gates, vals);
        ck_bit(vals.get(y1), Bit::Zero, "not(y1,y2,1): y1 = 0");
        ck_bit(vals.get(y2), Bit::Zero, "not(y1,y2,1): y2 = 0 (same value, both outputs)");
    }

    //======================================================================
    // 3. a two-gate chain (one gate's output feeds another's input) --
    //    proves settling actually propagates across gates, not just
    //    evaluates each in isolation.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output y); wire nb; not(nb, b); and(y, a, nb); endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto gates = collect_gate_instances(v);
        ck(gates.size() == 2, "chain: 2 gates collected");

        SignalValues vals;
        vals.init(v.n_sig);
        int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b"), y = v.find_sig_local(0, "y");
        vals.set(a, Bit::One);
        vals.set(b, Bit::Zero);
        int passes = settle_gates(gates, vals);
        ck_bit(vals.get(y), Bit::One, "a=1,b=0: nb=not(0)=1, y=and(1,1)=1 -- propagated through the chain");
        ck(passes >= 0, "chain settles within max_passes");

        vals.set(a, Bit::One);
        vals.set(b, Bit::One);
        settle_gates(gates, vals);
        ck_bit(vals.get(y), Bit::Zero, "a=1,b=1: nb=not(1)=0, y=and(1,0)=0");
    }

    //======================================================================
    // 4. a feedback ring (3 NOT gates, no primary input) -- demonstrates
    //    the documented property in gate_wire.h: starting from all-X, this
    //    settles immediately to X rather than oscillating, because every
    //    gate truth table is monotonic in "how much is known." This is the
    //    textbook-correct answer for an unclocked feedback loop with no
    //    set/reset stimulus, not a limitation.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(output y1, output y2, output y3); "
                    "not(y1, y3); not(y2, y1); not(y3, y2); endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto gates = collect_gate_instances(v);
        SignalValues vals;
        vals.init(v.n_sig);
        int passes = settle_gates(gates, vals, 16);
        ck(passes >= 0, "3-inverter feedback ring settles (does not hit max_passes)");
        int y1 = v.find_sig_local(0, "y1"), y2 = v.find_sig_local(0, "y2"), y3 = v.find_sig_local(0, "y3");
        ck_bit(vals.get(y1), Bit::X, "ring settles to x, not an oscillation or a crash");
        ck_bit(vals.get(y2), Bit::X, "  same for y2");
        ck_bit(vals.get(y3), Bit::X, "  same for y3");
    }

    //======================================================================
    // 5. malformed gate: too few terminals -> diagnostic, not a crash or a
    //    fabricated half-resolved gate.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(output y); not(y); endmodule");   // not needs an output AND an input
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto gates = collect_gate_instances(v);
        ck(gates.empty(), "a gate with only 1 terminal is not collected");
        ck(v.had_error, "  and it's reported as an error, not silently dropped");
    }

    if (fails == 0) std::printf("test_gate_wire: PASS\n");
    else            std::printf("test_gate_wire: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
