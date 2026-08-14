//============================================================================
// verify.h -- Phase 4 + Phase 5 (Specification.md's numbering: "vector
// application" and "verification"), aka Phase 7 + 8 in the class's own
// planning document: apply a sequence of test vectors to a design and check
// its outputs against expected values, producing a PASS/FAIL report.
//----------------------------------------------------------------------------
// Builds entirely on top of sim.h's existing pipeline (collect_assign_drivers,
// collect_gate_instances, delta_cycle, StimulusEntry, parse_vector_file) --
// this file adds exactly the two things that weren't there yet: (1) knowing
// which vector-file columns are inputs to DRIVE vs outputs to CHECK, using
// the design's own declared port directions (sg_dir, resolved by sig.cpp),
// and (2) comparing settled output values against the vector file's own
// values for those columns and scoring PASS/FAIL.
//
// Vector file format (one file does double duty, per Specification.md
// decision 0.6 -- vectors are a plain external text file, not a Verilog
// testbench): each line is a sequence of whitespace-separated `name=bit`
// tokens (same syntax sim.h's parse_vector_file already reads). A name that
// resolves to an INPUT (or inout) port of the top instance is stimulus to
// apply; a name that resolves to an OUTPUT port is the expected value to
// check the settled result against. This matches the exact style shown in
// the class's own planning notes (input columns across the top, one row per
// test cycle) while also supplying the "expected" side Phase 5/8 needs, all
// in the one file format the project already committed to.
//
// A vector-file token naming something that isn't a port of the top
// instance is reported (v.add_diag, SEV_WARNING) and ignored for that
// line -- never silently matched to the wrong signal.
//
// An expected value of 'x' means "don't care" -- a very standard convention
// in test-vector verification (you often can't predict every internal
// signal's value, especially before reset) -- and is skipped rather than
// counted as a pass or a fail.
//============================================================================
#pragma once
#include <iosfwd>
#include <string>
#include <vector>

#include "sim.h"   // AssignDriver, StimulusEntry, parse_vector_file, delta_cycle, ...

namespace vsim {

// One expected-vs-actual comparison for a single output port on a single
// cycle.
struct VectorCheck {
    std::string name;
    Bit expected = Bit::X;
    Bit actual   = Bit::X;
    bool dont_care = false;   // expected was 'x' -- not counted as pass/fail
    bool match     = true;    // dont_care counts as a match
};

// The result of applying one vector-file line (one test cycle).
struct CycleResult {
    int cycle = 0;
    bool pass = true;           // false if any non-dont-care check mismatched,
                                 // or the delta cycle didn't converge
    int delta_passes = 0;       // -1 if settle_gates/delta_cycle didn't converge
    std::vector<VectorCheck> checks;   // output-port comparisons this cycle
};

// Summary across every vector-file line.
struct VerifyReport {
    int total_cycles  = 0;
    int passed        = 0;
    int failed         = 0;
    int nonconvergent = 0;
    std::vector<CycleResult> cycles;
};

// Core entry point: `vectors` is already-parsed (as from parse_vector_file()
// or built directly in code, e.g. by a test). For each vector line: split its
// tokens into stimulus (input/inout ports) vs expected (output ports) using
// the top instance's own sg_dir; apply the stimulus; run delta_cycle(); then
// compare every expected output against what actually settled. Writes one
// human-readable line per cycle to `out`, then a summary line, and returns
// the same information structured for a caller (e.g. a test) to assert on.
//
// Must run after v.elaborate() and v.resolve_signals() have both succeeded
// and produced a valid v.top_inst.
VerifyReport run_verification_vectors(Vsim& v,
                                       const std::vector<std::vector<StimulusEntry>>& vectors,
                                       std::ostream& out);

// Convenience wrapper: reads the vector file from `vector_path` via
// parse_vector_file() and delegates to run_verification_vectors().
VerifyReport run_verification(Vsim& v, const std::string& vector_path, std::ostream& out);

} // namespace vsim
