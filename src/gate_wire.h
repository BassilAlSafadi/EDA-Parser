//============================================================================
// gate_wire.h -- Phase 3: wire elaborated gate primitives to real signals,
// and evaluate a purely combinational (gate-only) design to steady state.
//----------------------------------------------------------------------------
// gate_eval.cpp answers "given these input bits, what does one gate output?"
// This file answers the next question: "which signals is a given gate
// instance actually connected to, in a given elaborated design?" -- and
// then runs every gate until the whole network settles.
//
// Depends on Task 5.1 (elab.cpp: the instance table) and Task 5.2 (sig.cpp:
// resolve_signals() / the per-instance signal scope) having already run.
// Deliberately does NOT modify sig.cpp or add anything to its sg_* arrays --
// signal *values* are kept in a separate SignalValues table below, indexed
// the same way sg_* is, so this file stays a pure consumer of Task 5.2's
// output rather than a second author of it.
//
// Scope, stated plainly (same spirit as Specification.md 0.A and sig.cpp's
// own "SCOPE OF THIS SUBSET" note):
//   - only plain identifier connections are wired (`and g(y, a, b);`), the
//     same limitation resolve_conn_expr() already has for module port
//     connections -- a gate connected to something more complex than a bare
//     signal name is reported, not silently dropped, exactly like sig.cpp
//     already does for module instances.
//   - evaluation is a repeated-pass relaxation to a fixed point, starting
//     every signal at Bit::X. This is NOT a general event-driven scheduler
//     and does not touch `assign` expressions, `always` blocks, or clocks
//     -- that machinery is separate, later Phase-3 work.
//
// A note on why max_passes in settle_gates() below is a safety net rather
// than a feature you should expect to see trigger: every truth table in
// gate_eval.cpp is monotonic with respect to "how much is known" (X is the
// least-known value; feeding a gate MORE determined inputs can only make
// its output MORE determined or leave it unchanged -- it can never cause a
// previously-settled 0 or 1 to flip). Starting every signal at X and
// iterating a monotonic function over a lattice of finite height is
// guaranteed to reach a fixed point (a standard Kleene-logic result) -- it
// cannot oscillate. Tried this deliberately on a 3-gate NOT ring (the
// textbook "ring oscillator" that a two-state, zero-delay simulator would
// spin on forever): it settles to X/X/X in a single pass here, which is
// actually the textbook-correct answer for an unclocked feedback loop with
// no set/reset stimulus, not a limitation of this code. max_passes exists
// as a defensive bound in case that invariant is ever broken by a future
// change to gate_eval.cpp, not because real designs are expected to hit it.
//============================================================================
#pragma once
#include <vector>

#include "gate_eval.h"
#include "vsim.hpp"

namespace vsim {

// One elaborated gate instance, fully resolved: which module instance it
// lives in, which primitive it is, and which sg_* signal-table indices its
// terminals are wired to.
//
// Verilog's terminal-order convention (LRM 7.2) differs by gate family:
//   and/or/nand/nor/xor/xnor : terminal 0 is the OUTPUT, the rest are inputs
//                              (N-ary: `and g(y,a,b,c);` has 3 inputs)
//   not/buf                  : the LAST terminal is the single INPUT, every
//                              terminal before it is an output
//                              (`not g(y1,y2,a);` drives y1 and y2 from a)
// out_sigs/in_sigs below are already split according to that rule -- callers
// don't need to know it.
struct GateInst {
    int inst = -1;              // owning instance (elab.cpp's in_* index)
    Kind kind = 0;               // K_AND / K_OR / K_NOT / K_NAND / K_NOR / K_XOR / K_XNOR / K_BUF
    int line = 0, col = 0;       // for diagnostics
    std::vector<int> out_sigs;   // sg_* indices, one or more
    std::vector<int> in_sigs;    // sg_* indices, one or more
};

// Walk every elaborated instance's own module body (elab.cpp's n_inst /
// in_mod), find every ND_GATE_INST written directly in it (descending into
// ND_GENERATE, same convention as elab.cpp's resolve_items/elab_body), and
// resolve its terminals against THAT INSTANCE'S OWN signal scope --
// deliberately not the parent's: a gate is written inside a module body and
// refers to that module's own ports/wires, unlike a module instantiation's
// connections, which sig.cpp's flatten_connections() resolves in the
// PARENT's scope because the child's ports and the parent's wires are two
// different scopes.
//
// Must run after v.elaborate() and v.resolve_signals() have both succeeded.
// A gate with too few terminals, or a terminal that doesn't resolve to a
// signal in scope, is reported via v.add_diag() and skipped (not fabricated
// as a partial/garbage GateInst) -- same failure policy as check_conns() in
// elab.cpp and resolve_conn_expr() in sig.cpp.
std::vector<GateInst> collect_gate_instances(Vsim& v);

// Signal-value table: one Bit per sg_* row (same indexing sig.cpp uses),
// kept separate from sig.cpp's own arrays on purpose (see file header).
// Every entry starts as Bit::X ("unknown"), matching real hardware
// power-up state and Verilog's own default net/reg initial value.
struct SignalValues {
    std::vector<Bit> value;
    void init(int n_sig) { value.assign(static_cast<std::size_t>(n_sig), Bit::X); }
    Bit get(int sg) const { return (sg >= 0 && sg < static_cast<int>(value.size())) ? value[static_cast<std::size_t>(sg)] : Bit::X; }
    void set(int sg, Bit b) { if (sg >= 0 && sg < static_cast<int>(value.size())) value[static_cast<std::size_t>(sg)] = b; }
};

// Evaluate every gate in `gates` repeatedly -- each pass recomputes every
// gate's output from the current SignalValues and writes it back -- until a
// full pass changes nothing (the network has settled). Provably always
// terminates well before max_passes given SignalValues::init()'s all-X
// start (see the design note above) -- max_passes/-1 is a defensive bound,
// not a mechanism you should expect real designs to exercise.
// Returns the number of passes it took to settle (>= 0), or -1 if it
// somehow didn't within max_passes (would indicate a bug in gate_eval.cpp's
// monotonicity, not a property of the design being evaluated).
int settle_gates(const std::vector<GateInst>& gates, SignalValues& vals, int max_passes = 64);

} // namespace vsim
