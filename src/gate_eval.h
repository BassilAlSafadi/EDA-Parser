//============================================================================
// gate_eval.h -- four-state truth tables for the 8 gate primitives.
//----------------------------------------------------------------------------
// Phase 3 ("Evaluation engine and event queue", Specification.md §1) is
// where the tool actually simulates a design. This file covers exactly one
// well-defined slice of it: given the input bit(s) of one gate instance,
// what is its output bit? That's a closed, table-driven question,
// independent of the event queue / scheduler / blocking-vs-nonblocking
// machinery that the rest of Phase 3 still needs -- and independent of
// sig.cpp (Task 5.2), since it operates on plain Bit values, not on the
// signal table. Wiring "which gate is connected to which signal" is the
// next layer, built on top of Task 5.2's connection table once that's
// stable; this file only answers "given these inputs, what comes out."
//
// Truth tables follow IEEE 1364-2001 Table 5-1 (and/or/xor + their negated
// forms). Two LRM rules baked in here:
//   - a `z` input is treated exactly like `x` for primitive evaluation
//     (LRM 5.1.1) -- gate primitives have no notion of high-impedance
//     inputs, only tri-state gates like bufif/notif do, and this subset's
//     grammar (defs.hpp is_gate_kw) doesn't recognise those.
//   - a gate's output is therefore always 0/1/x, never z.
//
// and/or/nand/nor/xor/xnor accept one or more inputs (Verilog gate
// primitives are N-ary: `and g(y,a,b,c);` is legal). and/or/xor are
// associative and commutative, so folding the inputs left-to-right through
// the 2-input table gives the same answer regardless of order. not/buf
// take exactly one input.
//============================================================================
#pragma once
#include <vector>

#include "defs.hpp"
#include "four_state.hpp"

namespace vsim {

// z -> x, 0/1/x pass through unchanged (LRM 5.1.1).
Bit gate_normalize(Bit b);

// 2-input primitives (inputs already normalized or not -- these normalize
// internally, so callers may pass raw Bit values including Z).
Bit gate_and2(Bit a, Bit b);
Bit gate_or2(Bit a, Bit b);
Bit gate_xor2(Bit a, Bit b);
Bit gate_not1(Bit a);   // also used as the base case for nand/nor/xnor negation

// Evaluate one gate instance's output from its input bit(s).
//   kind must be one of K_AND, K_OR, K_NAND, K_NOR, K_XOR, K_XNOR, K_NOT, K_BUF.
//   inputs.size() must be >= 1 for and/or/nand/nor/xor/xnor; exactly 1 for not/buf
//     (an empty vector or an unrecognised kind returns Bit::X rather than
//     asserting -- the caller, once gate instances are elaborated, is
//     expected to guarantee arity, matching how check_conns() in elab.cpp
//     validates connection counts before anything downstream relies on them).
Bit eval_gate(Kind kind, const std::vector<Bit>& inputs);

} // namespace vsim
