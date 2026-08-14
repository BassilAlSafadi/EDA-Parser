//============================================================================
// sim.h -- Phase 3: simulation engine
//----------------------------------------------------------------------------
// Provides the `assign`-expression evaluator, continuous-assignment driver
// collection, delta-cycle loop, stimulus application, and result dump.
//
// Depends on Task 5.1 (elab.cpp) and Task 5.2 (sig.cpp) having already run
// (elaborate() + resolve_signals() must both succeed before anything here is
// called), and on gate_wire.h for GateInst/SignalValues/collect_gate_instances
// and settle_gates(), which this file drives but does not reimplement.
//
// Scope (same spirit as gate_wire.h and sig.cpp's own notes):
//   - assign LHS must be a bare identifier (same limit as resolve_conn_expr).
//   - assign RHS expression evaluation uses only bit 0 of every signal
//     (single-bit subset); multi-bit operators are left for a later phase.
//   - Stimulus arrives as a plain name=value text file (Specification.md §0.6:
//     vectors are NOT a Verilog testbench, they are an external plain-text
//     file). Format: one vector per line, space-separated name=bit tokens,
//     '#' comments, blank lines ignored. Legal bit characters: 0 1 x X z Z.
//   - Output format: one line per vector:
//       time=<N>  <sig>=<bit> <sig>=<bit> ...
//============================================================================
#pragma once
#include <iosfwd>
#include <string>
#include <vector>

#include "gate_wire.h"   // GateInst, SignalValues, settle_gates
#include "vsim.hpp"

namespace vsim {

//============================================================================
// AssignDriver -- one resolved `assign lhs = rhs;` statement
//============================================================================
// lhs_sig  : sg_* index of the driven net (left-hand side, already resolved
//            by collect_assign_drivers against the owning instance's scope).
// rhs_node : AST handle of the right-hand expression; evaluated at runtime by
//            eval_assign_expr() against the current SignalValues.
// inst     : owning elaborated instance (in_* index); needed for ND_IDENT
//            name lookups inside eval_assign_expr().
// line/col : source position of the ND_CONT_ASSIGN node, for diagnostics.
struct AssignDriver {
    int inst     = -1;
    int lhs_sig  = -1;   // sg_* index; -1 = failed to resolve (already diagnosed)
    int rhs_node = 0;    // AST handle of the RHS expression
    int line     = 0;
    int col      = 0;
};

//============================================================================
// collect_assign_drivers -- gather all `assign` statements in the design
//============================================================================
// Walk every elaborated instance's own module body (elab.cpp n_inst/in_mod),
// find every ND_CONT_ASSIGN, resolve its LHS ident against THAT instance's
// own signal scope (same-scope rule as gate_wire.cpp), and record the
// (lhs_sig, rhs_node, inst) triple.
//
// A non-ident LHS is warned and skipped (same policy as resolve_conn_expr
// for complex connection expressions -- flagged, not fabricated partially).
// Must run after v.elaborate() and v.resolve_signals() have both succeeded.
std::vector<AssignDriver> collect_assign_drivers(Vsim& v);

//============================================================================
// eval_assign_expr -- four-state single-bit AST expression evaluator
//============================================================================
// Recursively evaluate the AST node `node` in the context of instance `inst`,
// reading current signal values from `vals`.  Returns a single Bit.
//
// Supported node kinds:
//   ND_LITERAL  : read bit 0 of nd_value[node]
//   ND_IDENT    : find_sig_local(inst, name) -> vals.get(sg)
//   ND_UNARY    : apply nd_op (T_BNOT ~, T_NOT !, T_AMP &-reduct, T_PIPE
//                 |-reduct, T_CARET ^-reduct, T_NANDT ~&, T_NORT ~|, T_XNORT ~^)
//   ND_BINARY   : apply nd_op (&, |, ^, ~^, &&, ||, ==, !=)
//   ND_TERNARY  : condition nd_a -> select nd_b or nd_c
//   anything else: Bit::X  (unknown, not a crash)
//
// Four-state rules follow IEEE 1364-2001 (same as gate_eval.cpp):
//   0 & X = 0,  1 & X = X
//   0 | X = X,  1 | X = 1
//   ~X = X,     !X = X
//   X == 1 = X, X != 0 = X
Bit eval_assign_expr(const Vsim& v, int inst, int node,
                     const SignalValues& vals);

//============================================================================
// eval_all_assigns -- one pass over every AssignDriver
//============================================================================
// Re-evaluate every driver and write lhs_sig in `vals`.
// Returns true if at least one signal value changed.
bool eval_all_assigns(const Vsim& v,
                      const std::vector<AssignDriver>& drivers,
                      SignalValues& vals);

//============================================================================
// delta_cycle -- converge assigns + gates to a fixed point
//============================================================================
// Alternates eval_all_assigns() and settle_gates() until a complete round
// (one assign pass + one gate pass) produces no change.  Starting all signals
// at X and applying monotone operations guarantees termination (same argument
// as gate_wire.h); max_deltas is a defensive upper bound.
//
// Returns the number of delta cycles consumed (>= 0), or -1 if the network
// did not converge within max_deltas (would indicate a non-monotone operation
// being introduced, not a property of normal designs).
int delta_cycle(const Vsim& v,
                const std::vector<AssignDriver>& drivers,
                const std::vector<GateInst>&     gates,
                SignalValues& vals,
                int max_deltas = 128);

//============================================================================
// StimulusEntry -- one input assignment from a vector file
//============================================================================
struct StimulusEntry {
    std::string name;
    Bit         value = Bit::X;
};

//============================================================================
// parse_vector_file -- read a plain-text stimulus file
//============================================================================
// Format: one vector per line.  Each line is a sequence of name=bit tokens
// separated by whitespace. '#' begins a comment to end-of-line. Blank lines
// and comment-only lines are skipped. Legal bit chars: 0 1 x X z Z.
//
// Returns the parsed vectors; emits warnings to std::cerr for unrecognised
// tokens (does not use v.add_diag -- the vector file is not target source).
std::vector<std::vector<StimulusEntry>> parse_vector_file(const std::string& path);

//============================================================================
// apply_stimulus -- drive primary inputs into SignalValues
//============================================================================
// Set the sg_* value for each StimulusEntry::name found in inst 0 (the top).
// Entries that do not resolve in the top instance's scope are diagnosed via
// v.add_diag (SEV_WARNING) and skipped -- not a fatal error, consistent with
// the project's collect-all-diagnose-later policy.
void apply_stimulus(Vsim& v, const std::vector<StimulusEntry>& stim,
                    SignalValues& vals);

//============================================================================
// dump_vector_result -- print one line of simulation output
//============================================================================
// Format:  time=<time_step>  <name>=<bit> ...
// `port_names` controls which signals (and in what order) are printed.
// Signals not found in inst 0's scope are printed as '?'.
void dump_vector_result(const Vsim& v, const SignalValues& vals,
                        const std::vector<std::string>& port_names,
                        int time_step, std::ostream& out);

//============================================================================
// simulate -- top-level Phase 3 driver
//============================================================================
// 1. collect_assign_drivers(v)
// 2. collect_gate_instances(v)
// 3. For each vector:
//    a. apply_stimulus
//    b. delta_cycle
//    c. dump_vector_result
// port_names: signals to print (in order); empty => all signals of inst 0.
// Returns 0 on success, -1 if any delta-cycle failed to converge.
int simulate(Vsim& v,
             const std::vector<std::vector<StimulusEntry>>& vectors,
             const std::vector<std::string>&                port_names,
             std::ostream& out);

} // namespace vsim
