//============================================================================
// sim.cpp -- Phase 3: simulation engine (see sim.h for the design note).
//----------------------------------------------------------------------------
// Implements the assign-expression evaluator, delta-cycle loop, stimulus
// application, and top-level simulate() driver.  Deliberately does NOT
// modify any sig.cpp arrays: signal *values* live in a separate
// SignalValues table (defined in gate_wire.h, indexed the same as sg_*),
// keeping this file a pure consumer of Task 5.1 + 5.2's output.
//============================================================================
#include "sim.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace vsim {

// ===========================================================================
//  helpers
// ===========================================================================

// Convert a Bit to its display character.
static char bit_char(Bit b) {
    switch (b) {
        case Bit::Zero: return '0';
        case Bit::One:  return '1';
        case Bit::X:    return 'x';
        case Bit::Z:    return 'z';
    }
    return 'x';
}

// Parse a single bit character (0/1/x/X/z/Z) -> Bit.  Returns Bit::X for
// anything unrecognised -- consistent with Verilog's "unknown unless proven
// otherwise" default.
static Bit parse_bit_char(char c) {
    switch (c) {
        case '0':           return Bit::Zero;
        case '1':           return Bit::One;
        case 'x': case 'X': return Bit::X;
        case 'z': case 'Z': return Bit::Z;
        default:            return Bit::X;
    }
}

// ===========================================================================
//  collect_assign_drivers
// ===========================================================================

namespace {

// Walk one instance's own module-body item chain; collect ND_CONT_ASSIGN
// nodes and resolve their LHS idents.  Mirrors walk_items() in gate_wire.cpp.
void walk_assigns(Vsim& v, int scope_inst, int items,
                  std::vector<AssignDriver>& out) {
    int it = items;
    while (it != 0) {
        Kind k = v.nd_kind[static_cast<std::size_t>(it)];
        if (k == ND_GENERATE) {
            // Descend into generate blocks (same convention as elab.cpp and
            // gate_wire.cpp).
            walk_assigns(v, scope_inst, v.nd_a[static_cast<std::size_t>(it)], out);
        } else if (k == ND_CONT_ASSIGN) {
            // ND_CONT_ASSIGN layout (from parser.cpp parse_cont_assign):
            //   nd_a = lvalue expression (ND_IDENT or more complex)
            //   nd_b = RHS expression
            int lval_node = v.nd_a[static_cast<std::size_t>(it)];
            int rhs_node  = v.nd_b[static_cast<std::size_t>(it)];

            AssignDriver d;
            d.inst     = scope_inst;
            d.rhs_node = rhs_node;
            d.line     = v.nd_line[static_cast<std::size_t>(it)];
            d.col      = v.nd_col[static_cast<std::size_t>(it)];

            // LHS must be a bare identifier (same limitation as
            // resolve_conn_expr / gate terminal resolution).
            if (lval_node != 0 &&
                v.nd_kind[static_cast<std::size_t>(lval_node)] == ND_IDENT) {
                const std::string& nm = v.nd_name[static_cast<std::size_t>(lval_node)];
                d.lhs_sig = v.find_sig_local(scope_inst, nm);
                if (d.lhs_sig < 0) {
                    v.add_diag(SEV_ERROR,
                               v.nd_line[static_cast<std::size_t>(lval_node)],
                               v.nd_col[static_cast<std::size_t>(lval_node)],
                               "assign LHS refers to undefined signal '" + nm + "'");
                }
            } else if (lval_node != 0) {
                v.add_diag(SEV_WARNING, d.line, d.col,
                           "assign LHS is too complex for this subset (only bare "
                           "identifiers are supported); assignment skipped");
                d.lhs_sig = -1;
            }

            out.push_back(d);
        }
        it = v.nd_next[static_cast<std::size_t>(it)];
    }
}

} // namespace

std::vector<AssignDriver> collect_assign_drivers(Vsim& v) {
    std::vector<AssignDriver> out;
    for (int i = 0; i < v.n_inst; ++i) {
        int mod_node = v.mt_node[static_cast<std::size_t>(v.in_mod[static_cast<std::size_t>(i)])];
        // Module body is nd_b of the ND_MODULE node (same as gate_wire.cpp).
        walk_assigns(v, i, v.nd_b[static_cast<std::size_t>(mod_node)], out);
    }
    return out;
}

// ===========================================================================
//  eval_assign_expr -- recursive four-state single-bit evaluator
// ===========================================================================

// Forward declaration (mutual recursion for ND_TERNARY).
Bit eval_assign_expr(const Vsim& v, int inst, int node,
                     const SignalValues& vals);

namespace {

// Logical reduction: treat any nonzero known bit as 1, 0 as 0, X as X.
// Used for unary ! and for the ternary condition test.
static Bit logic_reduce(Bit b) {
    switch (b) {
        case Bit::Zero: return Bit::Zero;
        case Bit::One:  return Bit::One;
        default:        return Bit::X;    // X or Z
    }
}

// Four-state AND (single-bit): reuses the same rule as gate_and2.
// 0 dominates: 0 & anything = 0. 1 & 1 = 1. Otherwise X.
static Bit bit_and(Bit a, Bit b) {
    // Treat Z as X (LRM 5.1.1 for gates; same convention for expressions).
    if (a == Bit::Z) a = Bit::X;
    if (b == Bit::Z) b = Bit::X;
    if (a == Bit::Zero || b == Bit::Zero) return Bit::Zero;
    if (a == Bit::One  && b == Bit::One)  return Bit::One;
    return Bit::X;
}

// Four-state OR: 1 dominates.
static Bit bit_or(Bit a, Bit b) {
    if (a == Bit::Z) a = Bit::X;
    if (b == Bit::Z) b = Bit::X;
    if (a == Bit::One  || b == Bit::One)  return Bit::One;
    if (a == Bit::Zero && b == Bit::Zero) return Bit::Zero;
    return Bit::X;
}

// Four-state XOR: any unknown input -> X.
static Bit bit_xor(Bit a, Bit b) {
    if (a == Bit::Z) a = Bit::X;
    if (b == Bit::Z) b = Bit::X;
    if (a == Bit::X || b == Bit::X) return Bit::X;
    return (a == b) ? Bit::Zero : Bit::One;
}

// Four-state NOT: ~0=1, ~1=0, ~X=X, ~Z=X.
static Bit bit_not(Bit a) {
    if (a == Bit::Zero) return Bit::One;
    if (a == Bit::One)  return Bit::Zero;
    return Bit::X;
}

// Logical equality (==): only defined for 0/1 operands; X/Z -> X.
static Bit bit_eq(Bit a, Bit b) {
    if (a == Bit::Z) a = Bit::X;
    if (b == Bit::Z) b = Bit::X;
    if (a == Bit::X || b == Bit::X) return Bit::X;
    return (a == b) ? Bit::One : Bit::Zero;
}

// Logical inequality (!=)
static Bit bit_neq(Bit a, Bit b) {
    Bit eq = bit_eq(a, b);
    if (eq == Bit::X) return Bit::X;
    return (eq == Bit::One) ? Bit::Zero : Bit::One;
}

// Logical AND (&&): scalar truth value. 0 dominates.
static Bit bit_land(Bit a, Bit b) {
    a = logic_reduce(a);
    b = logic_reduce(b);
    if (a == Bit::Zero || b == Bit::Zero) return Bit::Zero;
    if (a == Bit::One  && b == Bit::One)  return Bit::One;
    return Bit::X;
}

// Logical OR (||): 1 dominates.
static Bit bit_lor(Bit a, Bit b) {
    a = logic_reduce(a);
    b = logic_reduce(b);
    if (a == Bit::One  || b == Bit::One)  return Bit::One;
    if (a == Bit::Zero && b == Bit::Zero) return Bit::Zero;
    return Bit::X;
}

// Reduction operators on a single Bit: for 1-bit signals, AND/OR/XOR/NAND/NOR/XNOR
// of a single bit are the identity (or complement), not a meaningful fold.
// We still return the correct result so wider signals work when extended.
static Bit apply_unary(Kind op, Bit a) {
    switch (op) {
        case T_BNOT:  return bit_not(a);   // ~
        case T_NOT:   return (logic_reduce(a) == Bit::Zero) ? Bit::One  // !
                                                             : (logic_reduce(a) == Bit::One ? Bit::Zero : Bit::X);
        // Reduction operators on a 1-bit operand are identity / complement.
        case T_AMP:   return (a == Bit::Z ? Bit::X : a);       // & (reduction AND of one bit)
        case T_PIPE:  return (a == Bit::Z ? Bit::X : a);       // | (reduction OR of one bit)
        case T_CARET: return (a == Bit::Z ? Bit::X : a);       // ^ (reduction XOR of one bit)
        case T_NANDT: return bit_not(a == Bit::Z ? Bit::X : a); // ~&
        case T_NORT:  return bit_not(a == Bit::Z ? Bit::X : a); // ~|
        case T_XNORT: return bit_not(a == Bit::Z ? Bit::X : a); // ~^
        default:      return Bit::X;
    }
}

static Bit apply_binary(Kind op, Bit a, Bit b) {
    switch (op) {
        case T_AMP:      return bit_and(a, b);   // &
        case T_PIPE:     return bit_or(a, b);    // |
        case T_CARET:    return bit_xor(a, b);   // ^
        case T_XNORT:    return bit_not(bit_xor(a, b));  // ~^ / ^~
        case T_LAND:     return bit_land(a, b);  // &&
        case T_LOR:      return bit_lor(a, b);   // ||
        case T_EQ:       return bit_eq(a, b);    // ==
        case T_CASE_EQ:  return bit_eq(a, b);    // === (treat same as == in 4-state)
        case T_NEQ:      return bit_neq(a, b);   // !=
        case T_CASE_NEQ: return bit_neq(a, b);   // !==
        // Arithmetic / comparison on single bits: treat 0/1 as integers.
        case T_PLUS:
            if (a == Bit::One && b == Bit::One) return Bit::Zero;  // 1+1 = 0 (bit 0 of 2)
            if (a == Bit::Zero && b == Bit::Zero) return Bit::Zero;
            if ((a == Bit::Zero || a == Bit::One) && (b == Bit::Zero || b == Bit::One))
                return Bit::One;
            return Bit::X;
        case T_MINUS:
            if (a == Bit::Zero && b == Bit::Zero) return Bit::Zero;
            if (a == Bit::One  && b == Bit::Zero) return Bit::One;
            if (a == Bit::One  && b == Bit::One)  return Bit::Zero;
            if (a == Bit::Zero && b == Bit::One)  return Bit::One;  // 0-1 wraps to 1 (unsigned)
            return Bit::X;
        case T_LT:
            if (a == Bit::Zero && b == Bit::Zero) return Bit::Zero;
            if (a == Bit::One  && b == Bit::One)  return Bit::Zero;
            if (a == Bit::Zero && b == Bit::One)  return Bit::One;
            if (a == Bit::One  && b == Bit::Zero) return Bit::Zero;
            return Bit::X;
        case T_GT:   return apply_binary(T_LT, b, a);
        case T_LE:   return bit_not(apply_binary(T_LT, b, a));
        case T_GE:   return bit_not(apply_binary(T_LT, a, b));
        default:     return Bit::X;
    }
}

} // namespace

Bit eval_assign_expr(const Vsim& v, int inst, int node,
                     const SignalValues& vals) {
    if (node == 0) return Bit::X;

    Kind k = v.nd_kind[static_cast<std::size_t>(node)];

    switch (k) {
        case ND_LITERAL: {
            // Use bit 0 of the stored four-state value (single-bit subset).
            // Literals with x/z at bit 0 propagate correctly.
            return v.nd_value[static_cast<std::size_t>(node)].get(0);
        }

        case ND_IDENT: {
            // Look up the signal in the owning instance's scope.
            int sg = v.find_sig_local(inst, v.nd_name[static_cast<std::size_t>(node)]);
            if (sg < 0) return Bit::X;   // undefined (already diagnosed at resolve time)
            return vals.get(sg);
        }

        case ND_UNARY: {
            Bit a = eval_assign_expr(v, inst,
                                     v.nd_a[static_cast<std::size_t>(node)],
                                     vals);
            return apply_unary(v.nd_op[static_cast<std::size_t>(node)], a);
        }

        case ND_BINARY: {
            Bit a = eval_assign_expr(v, inst,
                                     v.nd_a[static_cast<std::size_t>(node)],
                                     vals);
            Bit b = eval_assign_expr(v, inst,
                                     v.nd_b[static_cast<std::size_t>(node)],
                                     vals);
            return apply_binary(v.nd_op[static_cast<std::size_t>(node)], a, b);
        }

        case ND_TERNARY: {
            // nd_a = condition, nd_b = true branch, nd_c = false branch.
            Bit cond = logic_reduce(eval_assign_expr(v, inst,
                                                     v.nd_a[static_cast<std::size_t>(node)],
                                                     vals));
            if (cond == Bit::One)  return eval_assign_expr(v, inst, v.nd_b[static_cast<std::size_t>(node)], vals);
            if (cond == Bit::Zero) return eval_assign_expr(v, inst, v.nd_c[static_cast<std::size_t>(node)], vals);
            // Unknown condition: both branches evaluated, result is X if they differ.
            Bit tb = eval_assign_expr(v, inst, v.nd_b[static_cast<std::size_t>(node)], vals);
            Bit fb = eval_assign_expr(v, inst, v.nd_c[static_cast<std::size_t>(node)], vals);
            return (tb == fb) ? tb : Bit::X;
        }

        // Concatenation: for single-bit subset, take the first element's bit 0.
        case ND_CONCAT: {
            int child = v.nd_a[static_cast<std::size_t>(node)];
            if (child == 0) return Bit::X;
            return eval_assign_expr(v, inst, child, vals);
        }

        // Bit-select: e.g., a[0] -- just evaluate the base for now.
        case ND_BIT_SELECT: {
            return eval_assign_expr(v, inst, v.nd_a[static_cast<std::size_t>(node)], vals);
        }

        default:
            return Bit::X;   // unsupported node kind; X propagates safely
    }
}

// ===========================================================================
//  eval_all_assigns
// ===========================================================================

bool eval_all_assigns(const Vsim& v,
                      const std::vector<AssignDriver>& drivers,
                      SignalValues& vals) {
    bool changed = false;
    for (const AssignDriver& d : drivers) {
        if (d.lhs_sig < 0) continue;   // failed to resolve -- skip silently
        Bit result = eval_assign_expr(v, d.inst, d.rhs_node, vals);
        if (vals.get(d.lhs_sig) != result) {
            vals.set(d.lhs_sig, result);
            changed = true;
        }
    }
    return changed;
}

// ===========================================================================
//  delta_cycle
// ===========================================================================
// One delta cycle = one assign pass + one gate settle pass. We keep looping
// until both produce no change in the same iteration. This guarantees
// correctness for designs where assign outputs feed gate inputs and vice versa
// (e.g., an assign feeding a gate which feeds another assign).
//
// Why is this necessary? In Verilog all concurrent assignments are triggered
// simultaneously; a sequential evaluator may not propagate changes through
// chains of assignments in a single pass if it evaluates drivers in a
// different order from their dependency graph. Repeating until stable is the
// standard "fixed-point iteration" / "relaxation" technique used by all
// event-driven simulators for zero-delay combinational logic.

int delta_cycle(const Vsim& v,
                const std::vector<AssignDriver>& drivers,
                const std::vector<GateInst>&     gates,
                SignalValues& vals,
                int max_deltas) {
    for (int delta = 0; delta < max_deltas; ++delta) {
        bool changed = false;

        // 1. Re-evaluate all continuous assignments.
        if (eval_all_assigns(v, drivers, vals)) changed = true;

        // 2. Settle all gate primitives (settle_gates does its own internal
        //    iteration; here we call it for one full pass and detect if it
        //    moved any values).
        //    We can't just use the return value of settle_gates because it
        //    counts internal passes, not signal changes since the last delta.
        //    So we snapshot before/after.
        SignalValues before = vals;   // cheap copy: std::vector<Bit>
        settle_gates(gates, vals);
        // Check if settle_gates changed anything.
        for (int sg = 0; sg < static_cast<int>(vals.value.size()); ++sg) {
            if (vals.get(sg) != before.get(sg)) { changed = true; break; }
        }

        if (!changed) return delta;   // converged
    }
    return -1;   // did not converge
}

// ===========================================================================
//  parse_vector_file
// ===========================================================================
// Format per line (after stripping comments):
//   name=bit  [name=bit ...]
// Blank / comment-only lines are skipped. Each non-empty line becomes one
// vector.  Parsing is deliberately simple; a malformed token is warned to
// stderr but does not abort -- the vector is still included (with Bit::X for
// the bad entry), consistent with the tool's collect-all-diagnose-later policy.

std::vector<std::vector<StimulusEntry>> parse_vector_file(const std::string& path) {
    std::vector<std::vector<StimulusEntry>> result;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "sim: cannot open vector file '" << path << "'\n";
        return result;
    }

    std::string line;
    while (std::getline(f, line)) {
        // Strip comment.
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);

        // Tokenize on whitespace.
        std::istringstream ss(line);
        std::string tok;
        std::vector<StimulusEntry> vec;
        while (ss >> tok) {
            // Expected form: name=bit  (e.g. a=1, clk=0, sel=x)
            auto eq = tok.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= tok.size()) {
                std::cerr << "sim: unrecognised stimulus token '" << tok
                          << "' (expected name=bit)\n";
                continue;
            }
            StimulusEntry e;
            e.name  = tok.substr(0, eq);
            e.value = parse_bit_char(tok[eq + 1]);
            vec.push_back(e);
        }
        if (!vec.empty()) result.push_back(std::move(vec));
    }
    return result;
}

// ===========================================================================
//  apply_stimulus
// ===========================================================================

void apply_stimulus(Vsim& v, const std::vector<StimulusEntry>& stim,
                    SignalValues& vals) {
    for (const StimulusEntry& e : stim) {
        // Primary inputs are looked up in the top instance (always inst 0).
        int sg = (v.top_inst >= 0) ? v.find_sig_local(v.top_inst, e.name) : -1;
        if (sg < 0) {
            v.add_diag(SEV_WARNING, 0, 0,
                       "stimulus refers to unknown signal '" + e.name +
                       "' in top instance");
        } else {
            vals.set(sg, e.value);
        }
    }
}

// ===========================================================================
//  dump_vector_result
// ===========================================================================

void dump_vector_result(const Vsim& v, const SignalValues& vals,
                        const std::vector<std::string>& port_names,
                        int time_step, std::ostream& out) {
    out << "time=" << time_step;
    for (const std::string& nm : port_names) {
        int sg = (v.top_inst >= 0) ? v.find_sig_local(v.top_inst, nm) : -1;
        out << "  " << nm << "=";
        out << (sg >= 0 ? bit_char(vals.get(sg)) : '?');
    }
    out << "\n";
}

// ===========================================================================
//  simulate -- top-level Phase 3 driver
// ===========================================================================

int simulate(Vsim& v,
             const std::vector<std::vector<StimulusEntry>>& vectors,
             const std::vector<std::string>&                port_names,
             std::ostream& out) {
    // Collect drivers (both phases must already have run).
    auto drivers = collect_assign_drivers(v);
    auto gates   = collect_gate_instances(v);

    // Determine which signal names to print if the caller left port_names
    // empty: default to all signals of the top instance, in scope order.
    std::vector<std::string> names = port_names;
    if (names.empty() && v.top_inst >= 0) {
        int lo = v.in_sig0[static_cast<std::size_t>(v.top_inst)];
        int hi = lo + v.in_nsig[static_cast<std::size_t>(v.top_inst)];
        for (int s = lo; s < hi; ++s)
            names.push_back(v.sg_name[static_cast<std::size_t>(s)]);
    }

    int ret = 0;

    for (int t = 0; t < static_cast<int>(vectors.size()); ++t) {
        // Initialise signal values to X at the start of each vector, then
        // drive the stimulus.  This models an ideal "apply inputs and wait
        // for combinational outputs" test: every undriven internal wire
        // starts unknown.
        SignalValues vals;
        vals.init(v.n_sig);
        apply_stimulus(v, vectors[static_cast<std::size_t>(t)], vals);

        int d = delta_cycle(v, drivers, gates, vals);
        if (d < 0) {
            std::cerr << "sim: warning: delta-cycle did not converge at "
                         "time=" << t << "\n";
            ret = -1;
        }

        dump_vector_result(v, vals, names, t, out);
    }

    return ret;
}

} // namespace vsim
