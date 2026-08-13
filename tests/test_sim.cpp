//============================================================================
// test_sim.cpp -- unit tests for sim.cpp (Phase 3 simulation engine).
//----------------------------------------------------------------------------
// Tests are grouped into numbered suites that mirror sim.h's API surface.
// Every test runs against either an in-memory snippet (load_str) or one of
// the existing golden .v fixtures, going through the full elaboration +
// signal-resolution pipeline exactly as the tool would in production.
//============================================================================
#include "sim.h"

#include <cstdio>
#include <sstream>

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
    else        { std::printf("ok   %s\n", msg); }
}

static void ck_bit(Bit got, Bit want, const char* msg) {
    if (got != want) {
        fails += 1;
        std::printf("FAIL: %s (got %c, want %c)\n", msg, c(got), c(want));
    } else {
        std::printf("ok   %s\n", msg);
    }
}

// ---------------------------------------------------------------------------
// Fixture helper: lex + parse + elaborate + resolve_signals a snippet.
// Returns false if any of these phases produced an error.
// ---------------------------------------------------------------------------
static bool setup(Vsim& v, const char* src) {
    v.g_fname = "t";
    v.load_str(src);
    v.lex();
    int root = v.parse_source();
    if (v.had_error || v.arena_err) return false;
    v.elaborate(root, "");
    if (v.elab_err || v.had_error || v.top_mod < 0) return false;
    v.resolve_signals();
    return !v.sig_err && !v.had_error;
}

int main() {
    std::printf("=== Suite 1: eval_assign_expr — literals ===\n");
    {
        // A module with one wire, one assign that drives a constant.
        // We check the evaluator directly, not through simulate().
        Vsim v;
        ck(setup(v, "module m(output y); wire y; assign y = 1'b1; endmodule"), "setup ok");
        auto drivers = collect_assign_drivers(v);
        ck(drivers.size() == 1, "one assign driver collected");
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            Bit r = eval_assign_expr(v, drivers[0].inst, drivers[0].rhs_node, vals);
            ck_bit(r, Bit::One, "literal 1'b1 evaluates to One");
        }
    }
    {
        Vsim v;
        ck(setup(v, "module m(output y); wire y; assign y = 1'b0; endmodule"), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            Bit r = eval_assign_expr(v, drivers[0].inst, drivers[0].rhs_node, vals);
            ck_bit(r, Bit::Zero, "literal 1'b0 evaluates to Zero");
        }
    }

    std::printf("\n=== Suite 2: eval_assign_expr — identifiers ===\n");
    {
        Vsim v;
        ck(setup(v, "module m(input a, output y); assign y = a; endmodule"), "setup ok");
        auto drivers = collect_assign_drivers(v);
        ck(drivers.size() == 1, "one assign driver");
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a");
            vals.set(a, Bit::One);
            Bit r = eval_assign_expr(v, drivers[0].inst, drivers[0].rhs_node, vals);
            ck_bit(r, Bit::One, "ident a=1 evaluates to One");
            vals.set(a, Bit::Zero);
            r = eval_assign_expr(v, drivers[0].inst, drivers[0].rhs_node, vals);
            ck_bit(r, Bit::Zero, "ident a=0 evaluates to Zero");
            vals.set(a, Bit::X);
            r = eval_assign_expr(v, drivers[0].inst, drivers[0].rhs_node, vals);
            ck_bit(r, Bit::X, "ident a=X evaluates to X");
        }
    }

    std::printf("\n=== Suite 3: eval_assign_expr — unary operators ===\n");
    {
        // ~a
        Vsim v;
        ck(setup(v, "module m(input a, output y); assign y = ~a; endmodule"), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a");
            vals.set(a, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "~0 = 1");
            vals.set(a, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "~1 = 0");
            vals.set(a, Bit::X);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "~X = X");
        }
    }
    {
        // !a
        Vsim v;
        ck(setup(v, "module m(input a, output y); assign y = !a; endmodule"), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a");
            vals.set(a, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "!0 = 1");
            vals.set(a, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "!1 = 0");
            vals.set(a, Bit::X);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "!X = X");
        }
    }

    std::printf("\n=== Suite 4: eval_assign_expr — binary operators ===\n");
    {
        const char* snippet = "module m(input a, input b, output y); assign y = a & b; endmodule";
        Vsim v;
        ck(setup(v, snippet), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");

            vals.set(a, Bit::Zero); vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "0 & 0 = 0");
            vals.set(a, Bit::One);  vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "1 & 0 = 0");
            vals.set(a, Bit::One);  vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "1 & 1 = 1");
            vals.set(a, Bit::X);   vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "X & 1 = X");
            vals.set(a, Bit::X);   vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "X & 0 = 0 (0 dominates AND)");
        }
    }
    {
        const char* snippet = "module m(input a, input b, output y); assign y = a | b; endmodule";
        Vsim v;
        ck(setup(v, snippet), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");

            vals.set(a, Bit::Zero); vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "0 | 0 = 0");
            vals.set(a, Bit::One);  vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "1 | 0 = 1");
            vals.set(a, Bit::X);   vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "X | 1 = 1 (1 dominates OR)");
            vals.set(a, Bit::X);   vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "X | 0 = X");
        }
    }
    {
        const char* snippet = "module m(input a, input b, output y); assign y = a ^ b; endmodule";
        Vsim v;
        ck(setup(v, snippet), "setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");

            vals.set(a, Bit::Zero); vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "0 ^ 0 = 0");
            vals.set(a, Bit::One);  vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "1 ^ 0 = 1");
            vals.set(a, Bit::One);  vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "1 ^ 1 = 0");
            vals.set(a, Bit::X);   vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "X ^ 1 = X");
        }
    }
    {
        // == and !=
        const char* snip_eq  = "module m(input a, input b, output y); assign y = (a == b); endmodule";
        const char* snip_neq = "module m(input a, input b, output y); assign y = (a != b); endmodule";
        {
            Vsim v;
            ck(setup(v, snip_eq), "setup ==");
            auto drivers = collect_assign_drivers(v);
            if (drivers.size() == 1) {
                SignalValues vals; vals.init(v.n_sig);
                int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
                vals.set(a, Bit::One); vals.set(b, Bit::One);
                ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "1 == 1 = 1");
                vals.set(a, Bit::One); vals.set(b, Bit::Zero);
                ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "1 == 0 = 0");
                vals.set(a, Bit::X);  vals.set(b, Bit::One);
                ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,    "X == 1 = X");
            }
        }
        {
            Vsim v;
            ck(setup(v, snip_neq), "setup !=");
            auto drivers = collect_assign_drivers(v);
            if (drivers.size() == 1) {
                SignalValues vals; vals.init(v.n_sig);
                int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
                vals.set(a, Bit::One); vals.set(b, Bit::Zero);
                ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,  "1 != 0 = 1");
                vals.set(a, Bit::One); vals.set(b, Bit::One);
                ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero, "1 != 1 = 0");
            }
        }
    }

    std::printf("\n=== Suite 5: collect_assign_drivers — golden half_adder_rtl.v ===\n");
    {
        Vsim v;
        v.g_fname = "half_adder_rtl.v";
        v.load_src("golden/half_adder_rtl.v");
        v.lex();
        int root = v.parse_source();
        ck(!v.had_error, "half_adder_rtl.v parses cleanly");
        v.elaborate(root, "");
        ck(!v.elab_err && !v.had_error, "elaborates cleanly");
        v.resolve_signals();
        ck(!v.sig_err && !v.had_error, "signals resolve cleanly");

        auto drivers = collect_assign_drivers(v);
        ck(drivers.size() == 2,
           "half_adder_rtl.v: 2 assign drivers collected (sum and carry)");
        for (const auto& d : drivers)
            ck(d.lhs_sig >= 0, "  each driver's lhs_sig resolved");
    }

    std::printf("\n=== Suite 6: delta_cycle — RTL half-adder (assign only, no gates) ===\n");
    {
        Vsim v;
        v.g_fname = "half_adder_rtl.v";
        v.load_src("golden/half_adder_rtl.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();

        auto drivers = collect_assign_drivers(v);
        auto gates   = collect_gate_instances(v);
        ck(gates.empty(), "RTL half-adder has no gate primitives");

        int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
        int sum = v.find_sig_local(0, "sum"), carry = v.find_sig_local(0, "carry");
        ck(a >= 0 && b >= 0 && sum >= 0 && carry >= 0, "all 4 ports found");

        auto run = [&](Bit av, Bit bv, Bit want_sum, Bit want_carry, const char* label) {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(a, av); vals.set(b, bv);
            int d = delta_cycle(v, drivers, gates, vals);
            ck(d >= 0, "  converges");
            ck_bit(vals.get(sum),   want_sum,   label);
            (void)want_carry;
        };

        auto run2 = [&](Bit av, Bit bv, Bit want_sum, Bit want_carry, const char* ls, const char* lc) {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(a, av); vals.set(b, bv);
            delta_cycle(v, drivers, gates, vals);
            ck_bit(vals.get(sum),   want_sum,   ls);
            ck_bit(vals.get(carry), want_carry, lc);
        };

        run2(Bit::Zero, Bit::Zero, Bit::Zero, Bit::Zero, "0^0=0 (sum)", "0&0=0 (carry)");
        run2(Bit::One,  Bit::Zero, Bit::One,  Bit::Zero, "1^0=1 (sum)", "1&0=0 (carry)");
        run2(Bit::Zero, Bit::One,  Bit::One,  Bit::Zero, "0^1=1 (sum)", "0&1=0 (carry)");
        run2(Bit::One,  Bit::One,  Bit::Zero, Bit::One,  "1^1=0 (sum)", "1&1=1 (carry)");

        // X propagation
        run2(Bit::X, Bit::Zero, Bit::X,    Bit::Zero, "X^0=X (sum)", "X&0=0 (carry, 0 dominates)");
        run2(Bit::X, Bit::One,  Bit::X,    Bit::X,    "X^1=X (sum)", "X&1=X (carry)");

        (void)run; // suppress unused warning
    }

    std::printf("\n=== Suite 7: delta_cycle — structural half-adder (gates only) ===\n");
    {
        Vsim v;
        v.g_fname = "half_adder_struct.v";
        v.load_src("golden/half_adder_struct.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();

        auto drivers = collect_assign_drivers(v);
        auto gates   = collect_gate_instances(v);
        ck(drivers.empty(), "structural half-adder has no assign drivers");
        ck(gates.size() == 2, "structural half-adder has 2 gate primitives");

        int A = v.find_sig_local(0, "A"), B = v.find_sig_local(0, "B");
        int Sum = v.find_sig_local(0, "Sum"), Carry = v.find_sig_local(0, "Carry");

        auto run = [&](Bit av, Bit bv, Bit ws, Bit wc, const char* ls, const char* lc) {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(A, av); vals.set(B, bv);
            int d = delta_cycle(v, drivers, gates, vals);
            ck(d >= 0, "  converges");
            ck_bit(vals.get(Sum),   ws, ls);
            ck_bit(vals.get(Carry), wc, lc);
        };

        run(Bit::Zero, Bit::Zero, Bit::Zero, Bit::Zero, "struct: 0 xor 0 = 0", "struct: 0 and 0 = 0");
        run(Bit::One,  Bit::Zero, Bit::One,  Bit::Zero, "struct: 1 xor 0 = 1", "struct: 1 and 0 = 0");
        run(Bit::One,  Bit::One,  Bit::Zero, Bit::One,  "struct: 1 xor 1 = 0", "struct: 1 and 1 = 1");
    }

    std::printf("\n=== Suite 8: delta_cycle — mixed assign + gate chain ===\n");
    {
        // assign inv = ~a;  then  or(y, inv, b);
        // Tests that delta_cycle correctly propagates assign output into gate input.
        Vsim v;
        ck(setup(v, "module m(input a, input b, output y);\n"
                    "  wire inv;\n"
                    "  assign inv = ~a;\n"
                    "  or(y, inv, b);\n"
                    "endmodule"), "mixed setup ok");
        auto drivers = collect_assign_drivers(v);
        auto gates   = collect_gate_instances(v);
        ck(drivers.size() == 1, "one assign driver (inv = ~a)");
        ck(gates.size() == 1,   "one gate (or)");

        int a = v.find_sig_local(0, "a"), b = v.find_sig_local(0, "b");
        int y = v.find_sig_local(0, "y");

        // a=1, b=0: inv = ~1 = 0; y = or(0, 0) = 0
        {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(a, Bit::One); vals.set(b, Bit::Zero);
            int d = delta_cycle(v, drivers, gates, vals);
            ck(d >= 0, "mixed: converges");
            ck_bit(vals.get(y), Bit::Zero, "a=1,b=0: inv=0, y=or(0,0)=0");
        }
        // a=0, b=0: inv = ~0 = 1; y = or(1, 0) = 1
        {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(a, Bit::Zero); vals.set(b, Bit::Zero);
            delta_cycle(v, drivers, gates, vals);
            ck_bit(vals.get(y), Bit::One, "a=0,b=0: inv=1, y=or(1,0)=1");
        }
        // a=1, b=1: inv = 0; y = or(0, 1) = 1
        {
            SignalValues vals; vals.init(v.n_sig);
            vals.set(a, Bit::One); vals.set(b, Bit::One);
            delta_cycle(v, drivers, gates, vals);
            ck_bit(vals.get(y), Bit::One, "a=1,b=1: inv=0, y=or(0,1)=1");
        }
    }

    std::printf("\n=== Suite 9: simulate() — RTL half-adder full run ===\n");
    {
        Vsim v;
        v.g_fname = "half_adder_rtl.v";
        v.load_src("golden/half_adder_rtl.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();

        std::vector<std::vector<StimulusEntry>> vecs = {
            {{"a", Bit::Zero}, {"b", Bit::Zero}},
            {{"a", Bit::One},  {"b", Bit::Zero}},
            {{"a", Bit::Zero}, {"b", Bit::One}},
            {{"a", Bit::One},  {"b", Bit::One}},
        };
        std::vector<std::string> ports = {"a", "b", "sum", "carry"};

        std::ostringstream out;
        int ret = simulate(v, vecs, ports, out);
        ck(ret == 0, "simulate() returns 0 (converged for all vectors)");

        std::string s = out.str();
        ck(s.find("time=0") != std::string::npos, "output has time=0");
        ck(s.find("time=3") != std::string::npos, "output has time=3");
        ck(s.find("sum=0") != std::string::npos, "output contains sum=0 (time 0 and 3)");
        ck(s.find("sum=1") != std::string::npos, "output contains sum=1 (time 1 and 2)");
        ck(s.find("carry=1") != std::string::npos, "output contains carry=1 (time 3)");

        std::printf("  Simulation output:\n");
        // Print each line of the output indented.
        std::istringstream ss(s);
        std::string line;
        while (std::getline(ss, line)) std::printf("    %s\n", line.c_str());
    }

    std::printf("\n=== Suite 10: ternary operator ===\n");
    {
        Vsim v;
        ck(setup(v, "module m(input sel, input a, input b, output y);\n"
                    "  assign y = sel ? a : b;\n"
                    "endmodule"), "ternary setup ok");
        auto drivers = collect_assign_drivers(v);
        if (drivers.size() == 1) {
            SignalValues vals; vals.init(v.n_sig);
            int sel = v.find_sig_local(0, "sel");
            int a   = v.find_sig_local(0, "a");
            int b   = v.find_sig_local(0, "b");
            // sel=1 -> y=a
            vals.set(sel, Bit::One); vals.set(a, Bit::One); vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,
                   "sel=1, a=1, b=0 -> y=1");
            // sel=0 -> y=b
            vals.set(sel, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::Zero,
                   "sel=0, a=1, b=0 -> y=0");
            // sel=X, a==b -> y=same
            vals.set(sel, Bit::X); vals.set(a, Bit::One); vals.set(b, Bit::One);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::One,
                   "sel=X, a=b=1 -> y=1 (both branches agree)");
            // sel=X, a!=b -> y=X
            vals.set(a, Bit::One); vals.set(b, Bit::Zero);
            ck_bit(eval_assign_expr(v, 0, drivers[0].rhs_node, vals), Bit::X,
                   "sel=X, a=1, b=0 -> y=X (branches disagree)");
        }
    }

    std::printf("\n");
    if (fails == 0) std::printf("test_sim: PASS\n");
    else            std::printf("test_sim: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
