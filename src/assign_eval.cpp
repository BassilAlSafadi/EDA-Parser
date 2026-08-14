//============================================================================
// assign_eval.cpp -- see assign_eval.h for the design note and the exact
// subset of expressions supported.
//============================================================================
#include "assign_eval.h"

namespace vsim {

namespace {

// ---- scalar (1-bit) operator semantics, built on gate_eval.cpp's already-
// monotonic 2-input primitives wherever possible. ----------------------

// Logical equality: x/z on either side makes the answer unknown (LRM `==`
// semantics), otherwise a plain 0/1 compare. `!=` is its complement.
Bit b_eq2(Bit a, Bit b) {
    a = gate_normalize(a);
    b = gate_normalize(b);
    if (a == Bit::X || b == Bit::X) return Bit::X;
    return (a == b) ? Bit::One : Bit::Zero;
}
Bit b_neq2(Bit a, Bit b) {
    Bit e = b_eq2(a, b);
    if (e == Bit::X) return Bit::X;
    return (e == Bit::One) ? Bit::Zero : Bit::One;
}
Bit b_xnor2(Bit a, Bit b) { return gate_not1(gate_xor2(a, b)); }

// The standard four-valued conditional: settles as soon as the condition
// is known; while the condition is unknown, it can still resolve if both
// branches already agree (matching real Verilog `?:` and gate_wire.h's
// monotonicity argument -- this is the textbook Kleene-logic conditional).
Bit b_mux(Bit cond, Bit t, Bit e) {
    cond = gate_normalize(cond);
    if (cond == Bit::One)  return t;
    if (cond == Bit::Zero) return e;
    Bit tn = gate_normalize(t), en = gate_normalize(e);
    return (tn == en) ? tn : Bit::X;
}

// Unary operators this subset recognises. `~ ! ~^ ~& ~|` all complement a
// scalar bit (reduction-NAND/NOR/XNOR of a single bit is the complement of
// that bit, by the same fold-the-2-input-table-over-one-element reasoning
// AND/OR/XOR-reduction of a single bit is that bit itself, used below).
// `& | ^` in PREFIX position are reduction operators; reducing a single bit
// with any of them is that bit, so they're pass-throughs here.
Bit eval_unary(Kind op, Bit a) {
    switch (op) {
        case T_BNOT: case T_NOT: case T_XNORT: case T_NANDT: case T_NORT:
            return gate_not1(a);
        case T_AMP: case T_PIPE: case T_CARET:
            return gate_normalize(a);
        default:
            return Bit::X;   // unreachable: validate_expr() already filtered
    }
}

Bit eval_binary(Kind op, Bit a, Bit b) {
    switch (op) {
        case T_AMP:   return gate_and2(a, b);
        case T_PIPE:  return gate_or2(a, b);
        case T_CARET: return gate_xor2(a, b);
        case T_XNORT: return b_xnor2(a, b);
        case T_EQ:    return b_eq2(a, b);
        case T_NEQ:   return b_neq2(a, b);
        // Verilog reduces each operand of && / || to a single boolean first;
        // for an already-scalar operand that reduction is the identity, so
        // && / || collapse to exactly gate_and2 / gate_or2's 0-dominates /
        // x-otherwise truth table.
        case T_LAND:  return gate_and2(a, b);
        case T_LOR:   return gate_or2(a, b);
        default:
            return Bit::X;   // unreachable: validate_expr() already filtered
    }
}

// ---- validation: walk the RHS once at collection time, report anything
// outside the documented subset, and resolve every identifier so eval_expr()
// never needs to raise a diagnostic mid-simulation. ----------------------

bool validate_expr(Vsim& v, int scope_inst, int node) {
    if (node == 0) return false;
    Kind k = v.nd_kind[static_cast<std::size_t>(node)];
    switch (k) {
        case ND_IDENT:
            return v.resolve_conn_expr(scope_inst, node) >= 0;

        case ND_LITERAL:
            if (v.nd_width[static_cast<std::size_t>(node)] != 1) {
                v.add_diag(SEV_WARNING, v.nd_line[static_cast<std::size_t>(node)], v.nd_col[static_cast<std::size_t>(node)],
                            "multi-bit literal not supported by this subset's assign evaluator");
                return false;
            }
            return true;

        case ND_UNARY: {
            Kind op = v.nd_op[static_cast<std::size_t>(node)];
            if (op == T_PLUS || op == T_MINUS) {
                v.add_diag(SEV_WARNING, v.nd_line[static_cast<std::size_t>(node)], v.nd_col[static_cast<std::size_t>(node)],
                            "arithmetic operators are not supported by this subset's assign evaluator");
                return false;
            }
            return validate_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)]);
        }

        case ND_BINARY: {
            Kind op = v.nd_op[static_cast<std::size_t>(node)];
            switch (op) {
                case T_AMP: case T_PIPE: case T_CARET: case T_XNORT:
                case T_EQ:  case T_NEQ:  case T_LAND:  case T_LOR:
                    return validate_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)]) &&
                           validate_expr(v, scope_inst, v.nd_b[static_cast<std::size_t>(node)]);
                default:
                    v.add_diag(SEV_WARNING, v.nd_line[static_cast<std::size_t>(node)], v.nd_col[static_cast<std::size_t>(node)],
                                "operator not supported by this subset's assign evaluator "
                                "(arithmetic/relational/shift/case-equality are out of scope)");
                    return false;
            }
        }

        case ND_TERNARY:
            return validate_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)]) &&
                   validate_expr(v, scope_inst, v.nd_b[static_cast<std::size_t>(node)]) &&
                   validate_expr(v, scope_inst, v.nd_c[static_cast<std::size_t>(node)]);

        default:
            v.add_diag(SEV_WARNING, v.nd_line[static_cast<std::size_t>(node)], v.nd_col[static_cast<std::size_t>(node)],
                        "expression too complex for this subset's assign evaluator "
                        "(bit-select/part-select/concat/replication not supported)");
            return false;
    }
}

// Mirrors gate_wire.cpp's walk_items(): walk one instance's own item chain,
// descending into ND_GENERATE, collecting every ND_CONT_ASSIGN found.
void walk_items(Vsim& v, int scope_inst, int items, std::vector<ContAssign>& out) {
    int it = items;
    while (it != 0) {
        Kind k = v.nd_kind[static_cast<std::size_t>(it)];
        if (k == ND_GENERATE) {
            walk_items(v, scope_inst, v.nd_a[static_cast<std::size_t>(it)], out);
        } else if (k == ND_CONT_ASSIGN) {
            int lv  = v.nd_a[static_cast<std::size_t>(it)];
            int rhs = v.nd_b[static_cast<std::size_t>(it)];
            if (v.nd_kind[static_cast<std::size_t>(lv)] != ND_IDENT) {
                v.add_diag(SEV_WARNING, v.nd_line[static_cast<std::size_t>(it)], v.nd_col[static_cast<std::size_t>(it)],
                            "assign target too complex for this subset (only a plain signal name is supported)");
            } else if (validate_expr(v, scope_inst, rhs)) {
                int lhs_sig = v.resolve_conn_expr(scope_inst, lv);
                if (lhs_sig >= 0) {
                    ContAssign a;
                    a.inst     = scope_inst;
                    a.lhs_sig  = lhs_sig;
                    a.rhs_node = rhs;
                    a.line     = v.nd_line[static_cast<std::size_t>(it)];
                    a.col      = v.nd_col[static_cast<std::size_t>(it)];
                    out.push_back(a);
                }
            }
            // else: validate_expr() already reported the specific diagnostic.
        }
        it = v.nd_next[static_cast<std::size_t>(it)];
    }
}

} // namespace

std::vector<ContAssign> collect_cont_assigns(Vsim& v) {
    std::vector<ContAssign> out;
    for (int i = 0; i < v.n_inst; ++i) {
        int mod_node = v.mt_node[static_cast<std::size_t>(v.in_mod[static_cast<std::size_t>(i)])];
        walk_items(v, i, v.nd_b[static_cast<std::size_t>(mod_node)], out);
    }
    return out;
}

Bit eval_expr(Vsim& v, int scope_inst, int node, const SignalValues& vals) {
    if (node == 0) return Bit::X;
    Kind k = v.nd_kind[static_cast<std::size_t>(node)];
    switch (k) {
        case ND_LITERAL:
            return v.nd_value[static_cast<std::size_t>(node)].get(0);

        case ND_IDENT: {
            int sg = v.find_sig_local(scope_inst, v.nd_name[static_cast<std::size_t>(node)]);
            return vals.get(sg);
        }

        case ND_UNARY: {
            Bit a = eval_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)], vals);
            return eval_unary(v.nd_op[static_cast<std::size_t>(node)], a);
        }

        case ND_BINARY: {
            Bit a = eval_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)], vals);
            Bit b = eval_expr(v, scope_inst, v.nd_b[static_cast<std::size_t>(node)], vals);
            return eval_binary(v.nd_op[static_cast<std::size_t>(node)], a, b);
        }

        case ND_TERNARY: {
            Bit c = eval_expr(v, scope_inst, v.nd_a[static_cast<std::size_t>(node)], vals);
            Bit t = eval_expr(v, scope_inst, v.nd_b[static_cast<std::size_t>(node)], vals);
            Bit e = eval_expr(v, scope_inst, v.nd_c[static_cast<std::size_t>(node)], vals);
            return b_mux(c, t, e);
        }

        default:
            return Bit::X;   // unreachable: validate_expr() already filtered
    }
}

int settle_network(Vsim& v, const std::vector<GateInst>& gates,
                    const std::vector<ContAssign>& assigns,
                    SignalValues& vals, int max_passes) {
    for (int pass = 0; pass < max_passes; ++pass) {
        bool changed = false;

        for (const GateInst& g : gates) {
            std::vector<Bit> ins;
            ins.reserve(g.in_sigs.size());
            for (int sg : g.in_sigs) ins.push_back(vals.get(sg));

            Bit result = eval_gate(g.kind, ins);

            for (int sg : g.out_sigs) {
                if (vals.get(sg) != result) {
                    vals.set(sg, result);
                    changed = true;
                }
            }
        }

        for (const ContAssign& a : assigns) {
            Bit result = eval_expr(v, a.inst, a.rhs_node, vals);
            if (vals.get(a.lhs_sig) != result) {
                vals.set(a.lhs_sig, result);
                changed = true;
            }
        }

        if (!changed) return pass;   // settled
    }
    return -1;   // did not settle within max_passes
}

} // namespace vsim
