//============================================================================
// test_assign_eval.cpp -- continuous-assignment collection + evaluation,
// run against a real golden fixture, plus mixed gate/assign networks and
// deliberate out-of-scope rejections.
//============================================================================
#include "assign_eval.h"

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
    // 1. golden/half_adder_rtl.v -- the real fixture: "assign sum = a^b;
    //    assign carry = a&b;", ANSI ports. Same truth table as the
    //    structural half-adder in test_gate_wire.cpp, proving the RTL and
    //    structural paths agree, end to end through the real pipeline.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_rtl.v";
        v.load_src("golden/half_adder_rtl.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        ck(v.n_diag == 0, "half_adder_rtl.v resolves with zero diagnostics");

        auto assigns = collect_cont_assigns(v);
        ck(assigns.size() == 2, "collect_cont_assigns finds both assign statements");

        SignalValues vals;
        vals.init(v.n_sig);
        int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
        int sum = v.find_sig_local(0, "sum"), carry = v.find_sig_local(0, "carry");
        ck(a >= 0 && b >= 0 && sum >= 0 && carry >= 0, "all four ports resolve in the top instance's scope");

        std::vector<GateInst> no_gates;
        auto run = [&](Bit av, Bit bv) {
            vals.set(a, av);
            vals.set(b, bv);
            settle_network(v, no_gates, assigns, vals);
        };

        run(Bit::Zero, Bit::Zero);
        ck_bit(vals.get(sum), Bit::Zero, "0 ^ 0 = 0 (sum)");
        ck_bit(vals.get(carry), Bit::Zero, "0 & 0 = 0 (carry)");

        run(Bit::One, Bit::Zero);
        ck_bit(vals.get(sum), Bit::One, "1 ^ 0 = 1 (sum)");
        ck_bit(vals.get(carry), Bit::Zero, "1 & 0 = 0 (carry)");

        run(Bit::One, Bit::One);
        ck_bit(vals.get(sum), Bit::Zero, "1 ^ 1 = 0 (sum) -- matches the structural half-adder's carry case");
        ck_bit(vals.get(carry), Bit::One, "1 & 1 = 1 (carry)");

        run(Bit::X, Bit::Zero);
        ck_bit(vals.get(sum), Bit::X, "x ^ 0 = x (sum) -- unknown propagates");
        ck_bit(vals.get(carry), Bit::Zero, "x & 0 = 0 (carry) -- 0 still dominates AND even with x present");
    }

    //======================================================================
    // 2. ternary / conditional operator, including the ambiguous-condition
    //    rule: an unknown condition still resolves if both branches agree.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input sel, input a, input b, output y); "
                    "assign y = sel ? a : b; endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto assigns = collect_cont_assigns(v);
        ck(assigns.size() == 1, "ternary assign collected");

        SignalValues vals;
        vals.init(v.n_sig);
        int sel = v.find_sig_local(0, "sel"), a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
        int y = v.find_sig_local(0, "y");
        std::vector<GateInst> no_gates;

        vals.set(sel, Bit::One); vals.set(a, Bit::Zero); vals.set(b, Bit::One);
        settle_network(v, no_gates, assigns, vals);
        ck_bit(vals.get(y), Bit::Zero, "sel=1: y = a = 0");

        vals.set(sel, Bit::Zero);
        settle_network(v, no_gates, assigns, vals);
        ck_bit(vals.get(y), Bit::One, "sel=0: y = b = 1");

        vals.set(sel, Bit::X); vals.set(a, Bit::One); vals.set(b, Bit::One);
        settle_network(v, no_gates, assigns, vals);
        ck_bit(vals.get(y), Bit::One, "sel=x but a==b==1: y = 1 (branches agree, ambiguity doesn't matter)");

        vals.set(sel, Bit::X); vals.set(a, Bit::Zero); vals.set(b, Bit::One);
        settle_network(v, no_gates, assigns, vals);
        ck_bit(vals.get(y), Bit::X, "sel=x and a!=b: y = x (genuinely ambiguous)");
    }

    //======================================================================
    // 3. mixed network: an `assign` feeding a gate's input -- proves
    //    settle_network() actually unifies both evaluation kinds in one
    //    fixed point, not just runs them side by side.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output y); "
                    "wire nb; assign nb = ~b; and(y, a, nb); endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto gates = collect_gate_instances(v);
        auto assigns = collect_cont_assigns(v);
        ck(gates.size() == 1 && assigns.size() == 1, "mixed network: 1 gate + 1 assign collected");

        SignalValues vals;
        vals.init(v.n_sig);
        int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b"), y = v.find_sig_local(0, "y");
        vals.set(a, Bit::One);
        vals.set(b, Bit::Zero);
        int passes = settle_network(v, gates, assigns, vals);
        ck_bit(vals.get(y), Bit::One, "a=1,b=0: nb=assign(~0)=1, y=and(1,1)=1 -- assign feeds a gate correctly");
        ck(passes >= 0, "mixed network settles within max_passes");

        vals.set(a, Bit::One);
        vals.set(b, Bit::One);
        settle_network(v, gates, assigns, vals);
        ck_bit(vals.get(y), Bit::Zero, "a=1,b=1: nb=assign(~1)=0, y=and(1,0)=0");
    }

    //======================================================================
    // 4. out-of-scope constructs are reported and skipped, not silently
    //    mis-evaluated or crashed on.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output y); assign y = a + b; endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        int diags_before = v.n_diag;
        auto assigns = collect_cont_assigns(v);
        ck(assigns.empty(), "arithmetic assign ('+' ) is not collected");
        ck(v.n_diag > diags_before, "  and it's reported as a diagnostic, not silently dropped");
    }
    {
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output y); assign y = a << b; endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto assigns = collect_cont_assigns(v);
        ck(assigns.empty(), "shift assign ('<<') is not collected");
    }

    if (fails == 0) std::printf("test_assign_eval: PASS\n");
    else            std::printf("test_assign_eval: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
