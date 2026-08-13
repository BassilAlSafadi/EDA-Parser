//============================================================================
// gate_wire.cpp -- see gate_wire.h for the design note.
//============================================================================
#include "gate_wire.h"

namespace vsim {

namespace {

bool is_not_or_buf(Kind k) { return k == K_NOT || k == K_BUF; }

// One ND_GATE_INST node -> a resolved GateInst, or nothing (with a
// diagnostic) if its arity or a terminal is bad. scope_inst is the
// instance the gate is textually written inside -- its OWN scope, not a
// parent's (see gate_wire.h).
void resolve_one_gate(Vsim& v, int scope_inst, int node, std::vector<GateInst>& out) {
    Kind kind = v.nd_op[static_cast<std::size_t>(node)];

    // collect every terminal expression in source order
    std::vector<int> terms;
    int e = v.nd_a[static_cast<std::size_t>(node)];
    while (e != 0) {
        terms.push_back(e);
        e = v.nd_next[static_cast<std::size_t>(e)];
    }

    if (terms.size() < 2) {
        v.add_diag(SEV_ERROR, v.nd_line[static_cast<std::size_t>(node)], v.nd_col[static_cast<std::size_t>(node)],
                    "gate instance needs at least one output and one input terminal");
        return;
    }

    GateInst g;
    g.inst = scope_inst;
    g.kind = kind;
    g.line = v.nd_line[static_cast<std::size_t>(node)];
    g.col  = v.nd_col[static_cast<std::size_t>(node)];

    // LRM 7.2 terminal order: and/or/xor family = output first, inputs
    // after; not/buf = input LAST, one-or-more outputs before it.
    std::vector<int> out_terms, in_terms;
    if (is_not_or_buf(kind)) {
        for (std::size_t i = 0; i + 1 < terms.size(); ++i) out_terms.push_back(terms[i]);
        in_terms.push_back(terms.back());
    } else {
        out_terms.push_back(terms.front());
        for (std::size_t i = 1; i < terms.size(); ++i) in_terms.push_back(terms[i]);
    }

    bool ok = true;
    for (int t : out_terms) {
        int sg = v.resolve_conn_expr(scope_inst, t);   // same-scope, not parent's (gate_wire.h)
        if (sg < 0) ok = false;
        g.out_sigs.push_back(sg);
    }
    for (int t : in_terms) {
        int sg = v.resolve_conn_expr(scope_inst, t);
        if (sg < 0) ok = false;
        g.in_sigs.push_back(sg);
    }

    // resolve_conn_expr() already raised the specific diagnostic (undefined
    // signal, or "too complex to flatten in this subset") for whichever
    // terminal failed -- nothing more to report here, just don't fabricate
    // a half-resolved gate.
    if (!ok) return;

    out.push_back(std::move(g));
}

// Walk one instance's own item chain (NOT descending into child module
// instances -- those are separate instances, walked on their own turn of
// the outer loop in collect_gate_instances). Mirrors elab.cpp's
// resolve_items()/elab_body() convention of descending into ND_GENERATE.
void walk_items(Vsim& v, int scope_inst, int items, std::vector<GateInst>& out) {
    int it = items;
    while (it != 0) {
        Kind k = v.nd_kind[static_cast<std::size_t>(it)];
        if (k == ND_GENERATE) {
            walk_items(v, scope_inst, v.nd_a[static_cast<std::size_t>(it)], out);
        } else if (k == ND_GATE_INST) {
            resolve_one_gate(v, scope_inst, it, out);
        }
        it = v.nd_next[static_cast<std::size_t>(it)];
    }
}

} // namespace

std::vector<GateInst> collect_gate_instances(Vsim& v) {
    std::vector<GateInst> out;
    for (int i = 0; i < v.n_inst; ++i) {
        int mod_node = v.mt_node[static_cast<std::size_t>(v.in_mod[static_cast<std::size_t>(i)])];
        walk_items(v, i, v.nd_b[static_cast<std::size_t>(mod_node)], out);
    }
    return out;
}

int settle_gates(const std::vector<GateInst>& gates, SignalValues& vals, int max_passes) {
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
        if (!changed) return pass;   // settled
    }
    return -1;   // did not settle -- a combinational loop, most likely
}

} // namespace vsim
