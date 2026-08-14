//============================================================================
// assign_eval.h -- Phase 3: evaluate `assign` (continuous assignment)
// statements, the RTL-style sibling of gate_wire.cpp's structural gates.
//----------------------------------------------------------------------------
// gate_wire.cpp answers "which signals is this gate instance wired to, and
// what does it drive?" for `and g(y,a,b);`-style structural code. This file
// answers the same two questions for `assign y = a & b;`-style RTL code:
// which signal does the left-hand side name, and what does the right-hand
// side expression currently evaluate to?
//
// Deliberately scoped to SCALAR (1-bit) signals and a well-defined subset of
// expression forms -- the same "recognise a frozen subset, report anything
// outside it, never fabricate a wrong answer" policy Specification.md and
// gate_wire.h already use throughout this project:
//
//   SUPPORTED on the right-hand side:
//     - a plain signal name, a 1-bit literal (0/1/x/z)
//     - bitwise/reduction  ~ & | ^ ~^ ^~ ~& ~|   (reduction of a single bit
//       is that bit itself, or its complement for the negated forms -- see
//       the .cpp for the derivation)
//     - logical              ! && ||
//     - equality              ==  !=
//     - the conditional (ternary) operator   cond ? a : b
//   SUPPORTED on the left-hand side:
//     - a plain signal name only (same restriction resolve_conn_expr()
//       already applies to module port connections and gate_wire.cpp
//       applies to gate terminals)
//
//   NOT supported (reported via v.add_diag() as SEV_WARNING and the whole
//   `assign` statement is skipped -- never partially evaluated):
//     - arithmetic  + - * / %           (four_state.hpp itself documents
//       that FourState was deliberately never given arithmetic operators;
//       this file keeps that same boundary rather than quietly re-opening
//       it with ad hoc 1-bit addition)
//     - relational  < <= > >=  and shifts  << >>   (not meaningful without
//       multi-bit, width-aware values)
//     - case equality  === !==          (different x/z semantics than ==/!=
//       -- always yields 0/1, never x -- a separate feature, not a subset
///      of this one)
//     - concatenation, replication, bit-select, part-select, multi-bit
//       literals                        (all require width-aware values;
//       every signal here is 1 bit, matching what gate_wire.cpp already
//       assumes)
//     - `always` blocks, procedural statements, delays  (sequential /
//       event-driven simulation is separate, later work -- see README/
//       Specification.md Phase 3's own "event queue" half, still open)
//
// Depends on Task 5.1 (elab.cpp) and Task 5.2 (sig.cpp) having already run,
// exactly like gate_wire.cpp. Does not modify sig.cpp or gate_wire.cpp.
//
// Monotonicity (why settle_network() below is safe to iterate to a fixed
// point, same argument gate_wire.h makes for gates): every operator
// implemented here is built out of gate_eval.cpp's already-monotonic
// gate_and2/gate_or2/gate_xor2/gate_not1, or defined so that an X input
// forces an X output (equality, "does this input matter" style), or is the
// standard four-valued Kleene conditional (cond ? t : e, resolving to t/e
// only once cond is 0/1, and to t itself when t and e already agree even
// under an unknown cond) -- the same construction real Verilog simulators
// use for exactly this convergence property. None of it can turn a
// previously-settled 0 or 1 back into x as an input becomes MORE defined.
//============================================================================
#pragma once
#include <vector>

#include "gate_eval.h"
#include "gate_wire.h"
#include "vsim.hpp"

namespace vsim {

// One elaborated `assign` statement, fully resolved except for the
// right-hand side's actual value (which depends on the current
// SignalValues and is (re)computed by eval_expr()/settle_network() below).
struct ContAssign {
    int inst = -1;       // owning instance (elab.cpp's in_* index)
    int lhs_sig = -1;    // sg_* index the left-hand side names
    int rhs_node = 0;    // pre-validated RHS expression AST node
    int line = 0, col = 0;
};

// Walk every elaborated instance's own module body (descending into
// ND_GENERATE, same convention as elab.cpp and gate_wire.cpp), find every
// ND_CONT_ASSIGN written directly in it, and resolve it against THAT
// INSTANCE'S OWN scope (an `assign` refers to its own module's ports/wires,
// same reasoning gate_wire.h gives for gates).
//
// An assign whose left-hand side isn't a plain signal name, or whose
// right-hand side uses anything outside the subset documented above, is
// reported via v.add_diag() and skipped entirely -- not partially wired.
std::vector<ContAssign> collect_cont_assigns(Vsim& v);

// Evaluate an already-validated expression node (as produced by a
// ContAssign collected above) against the current signal values. scope_inst
// is the instance the expression is textually written inside.
Bit eval_expr(Vsim& v, int scope_inst, int node, const SignalValues& vals);

// Evaluate a combinational network made of BOTH gate instances and
// `assign` statements together, repeatedly, until a full pass changes
// nothing. Strict superset of gate_wire.cpp's settle_gates(): passing an
// empty `assigns` vector reproduces settle_gates()'s behaviour exactly
// (same per-gate loop body), so this is the function to reach for once a
// design mixes structural and RTL style, which real target designs do.
// Returns the number of passes it took to settle (>= 0), or -1 if it
// somehow didn't within max_passes (see the monotonicity note above for why
// that would indicate a bug here, not a property of the design).
int settle_network(Vsim& v, const std::vector<GateInst>& gates,
                    const std::vector<ContAssign>& assigns,
                    SignalValues& vals, int max_passes = 64);

} // namespace vsim
