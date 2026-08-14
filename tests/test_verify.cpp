//============================================================================
// test_verify.cpp -- vector-file-driven verification (Phase 4/5, aka the
// class's Phase 7/8), run against real golden fixtures plus deliberately
// wrong vectors to prove FAIL detection actually works, not just PASS.
//============================================================================
#include "verify.h"

#include <cstdio>
#include <sstream>

using namespace vsim;

static int fails = 0;

static void ck(bool cond, const char* msg) {
    if (!cond) { fails += 1; std::printf("FAIL: %s\n", msg); }
    else       { std::printf("ok   %s\n", msg); }
}

int main() {
    //======================================================================
    // 1. golden/half_adder_struct.vec against golden/half_adder_struct.v
    //    (structural/gate design) -- every cycle should PASS.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_struct.v";
        v.load_src("golden/half_adder_struct.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        ck(v.n_diag == 0, "half_adder_struct.v resolves cleanly");

        std::ostringstream out;
        VerifyReport rep = run_verification(v, "golden/half_adder_struct.vec", out);
        ck(rep.total_cycles == 5, "half_adder_struct.vec: 5 cycles read");
        ck(rep.passed == 5 && rep.failed == 0, "half_adder_struct.vec: all 5 cycles PASS");
        ck(rep.nonconvergent == 0, "  and all converged");
    }

    //======================================================================
    // 2. golden/half_adder_rtl.vec against golden/half_adder_rtl.v
    //    (assign-statement / RTL design, now bug-fixed) -- every cycle PASS.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_rtl.v";
        v.load_src("golden/half_adder_rtl.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        ck(v.n_diag == 0, "half_adder_rtl.v resolves cleanly");

        std::ostringstream out;
        VerifyReport rep = run_verification(v, "golden/half_adder_rtl.vec", out);
        ck(rep.total_cycles == 5, "half_adder_rtl.vec: 5 cycles read");
        ck(rep.passed == 5 && rep.failed == 0, "half_adder_rtl.vec: all 5 cycles PASS");
    }

    //======================================================================
    // 3. deliberately WRONG expected values -- proves the tool actually
    //    catches a mismatch instead of always reporting PASS. This is the
    //    single most important thing a verification tool must prove about
    //    itself: that FAIL is reachable, not just PASS.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_struct.v";
        v.load_src("golden/half_adder_struct.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();

        std::vector<std::vector<StimulusEntry>> bad_vectors = {
            {{"A", Bit::One}, {"B", Bit::One}, {"Sum", Bit::One}, {"Carry", Bit::One}},
            // real answer: 1 xor 1 = 0 (Sum), 1 and 1 = 1 (Carry) -- Sum is
            // deliberately wrong above (claims Sum=1 when it's really 0),
            // Carry is left correct.
        };
        std::ostringstream out;
        VerifyReport rep = run_verification_vectors(v, bad_vectors, out);
        ck(rep.total_cycles == 1, "bad-vector run: 1 cycle read");
        ck(rep.failed == 1 && rep.passed == 0, "bad-vector run: the wrong expectation is caught as FAIL");
        ck(!rep.cycles[0].checks.empty(), "  checks were actually performed");
        bool sum_flagged = false, carry_ok = false;
        for (const VectorCheck& c : rep.cycles[0].checks) {
            if (c.name == "Sum" && !c.match) sum_flagged = true;
            if (c.name == "Carry" && c.match) carry_ok = true;
        }
        ck(sum_flagged, "  Sum is specifically flagged as a mismatch (actual 0 != expected 1)");
        ck(carry_ok, "  Carry (which really is 1) is correctly reported as a match");
    }

    //======================================================================
    // 4. 'x' as an expected value means don't-care -- skipped, not a FAIL.
    //======================================================================
    {
        Vsim v;
        v.g_fname = "half_adder_struct.v";
        v.load_src("golden/half_adder_struct.v");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();

        std::vector<std::vector<StimulusEntry>> vectors = {
            {{"A", Bit::One}, {"B", Bit::One}, {"Sum", Bit::X}, {"Carry", Bit::One}},
            // Sum's expected value is 'x' -- don't care, regardless of the
            // real answer (0). Only Carry is actually checked.
        };
        std::ostringstream out;
        VerifyReport rep = run_verification_vectors(v, vectors, out);
        ck(rep.passed == 1, "don't-care expected value doesn't block a PASS");
        ck(rep.cycles[0].checks[0].dont_care, "  Sum's check is marked don't-care");
    }

    //======================================================================
    // 5. the three sim.cpp bugfixes, exercised through the real
    //    verification path (not just the evaluator in isolation) --
    //    guards against regressing them back to silently-wrong answers.
    //======================================================================
    {
        // === must be an exact match (1 for x===x), not aliased to ==.
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output ceq); assign ceq = (a === b); endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        std::vector<std::vector<StimulusEntry>> vectors = {
            {{"a", Bit::X}, {"b", Bit::X}, {"ceq", Bit::One}},
        };
        std::ostringstream out;
        VerifyReport rep = run_verification_vectors(v, vectors, out);
        ck(rep.passed == 1, "=== (case equality) of x,x correctly verifies as 1, not x");
    }
    {
        // bit-select / concat in an assign RHS must be refused, not
        // silently mis-evaluated -- collect_assign_drivers() should not
        // produce a live driver for these, so the target signal simply
        // never gets driven (stays X) instead of getting a wrong value.
        Vsim v;
        v.g_fname = "t";
        v.load_str("module m(input a, input b, output y); assign y = {a, b}; endmodule");
        v.lex();
        int root = v.parse_source();
        v.elaborate(root, "");
        v.resolve_signals();
        auto drivers = collect_assign_drivers(v);
        bool has_live_driver = false;
        for (const AssignDriver& d : drivers) if (d.lhs_sig >= 0) has_live_driver = true;
        ck(!has_live_driver, "concatenation in assign RHS produces no live driver (refused, not mis-evaluated)");
    }

    if (fails == 0) std::printf("test_verify: PASS\n");
    else            std::printf("test_verify: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
