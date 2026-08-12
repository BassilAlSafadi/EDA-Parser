//============================================================================
// elab.cpp -- Phase 2, Task 5.1: Module Hierarchy Resolution
//----------------------------------------------------------------------------
// Ported 1:1 from rtl/vsim_elab.v. The AST that the parser produces is a
// *syntactic* object: a chain of ND_MODULE nodes, each holding a chain of
// items, with module instantiations recorded as ND_MOD_INST nodes that name
// a module by text only. Nothing in it says which module is the top,
// whether an instantiated name exists, or what the design tree looks like.
// Elaboration answers exactly those questions -- it is the step every real
// EDA flow performs between reading HDL and simulating it (06 ASIC Design
// Flow, "Design" step; Elaboration.md §1).
//
// Five responsibilities (Elaboration.md §1), same split as the original:
//   1. identify the top module                 -> pick_top
//   2. resolve all module instantiations        -> resolve_instances / resolve_items
//   3. verify that instantiated modules exist   -> resolve_items
//   4. create module instance objects           -> new_inst
//   5. build the parent-child hierarchy         -> elab_body / in_child
//
// Signal/net tables and connectivity flattening are Task 5.2 (sig.cpp) and
// are NOT done here; the only connection work below is the arity/name check
// that "resolving an instantiation" requires (check_conns).
//
// Porting notes vs the Verilog original:
//   - mt_name/in_name/nd_name are std::string now, not packed IDW identifiers,
//     so string equality replaces `==` on packed reg vectors directly.
//   - add_diag_id (Verilog) is gone; every call site that used to pass an
//     identifier to quote now builds the quoted name straight into the
//     message string, e.g. "... module '" + name + "'".
//   - -1 is still the null handle for module-table / instance-table indices
//     (Elaboration.md §2: unlike the AST, where handle 0 is NULL, module 0
//     and instance 0 are real entries here).
//   - want_top == "" means "decide the top automatically" (was want_top == 0,
//     the packed-identifier NULL, in the Verilog).
//============================================================================
#include "vsim.hpp"

#include <iostream>
#include <ostream>
#include <string>

namespace vsim {

// ===========================================================================
//  small helpers over the AST
// ===========================================================================

// length of an nd_next sibling chain
int Vsim::chain_len(int h) const {
    int c = 0, x = h;
    while (x != 0) {
        c += 1;
        x = nd_next[static_cast<std::size_t>(x)];
    }
    return c;
}

// count items of one kind in a module-item chain. Generate blocks hold
// their items in nd_a, so they are descended into rather than counted.
int Vsim::count_items(int h, Kind k) const {
    int c = 0, it = h;
    while (it != 0) {
        if (nd_kind[static_cast<std::size_t>(it)] == ND_GENERATE)
            c += count_items(nd_a[static_cast<std::size_t>(it)], k);
        else if (nd_kind[static_cast<std::size_t>(it)] == k)
            c += 1;
        it = nd_next[static_cast<std::size_t>(it)];
    }
    return c;
}

// module-table lookup by name; -1 when the module is not defined. Linear:
// n_mod is tiny (MAX_MODS = 64) and a linear scan keeps the table in
// declaration order, which the dump depends on for determinism.
int Vsim::find_module(const std::string& nm) const {
    for (int i = 0; i < n_mod; ++i)
        if (mt_name[static_cast<std::size_t>(i)] == nm) return i;
    return -1;
}

// port handle of the named port of module mi, or 0 if it has no such port
int Vsim::find_port_named(int mi, const std::string& nm) const {
    int p = nd_a[static_cast<std::size_t>(mt_node[static_cast<std::size_t>(mi)])];
    while (p != 0) {
        if (nd_name[static_cast<std::size_t>(p)] == nm) return p;
        p = nd_next[static_cast<std::size_t>(p)];
    }
    return 0;
}

// ===========================================================================
//  1. module table
// ===========================================================================
// Flatten the ND_MODULE chain into the module table and record the
// per-module statistics the later passes and the dump need. A repeated
// module name is an error (the second definition is unreachable, and
// silently keeping the first would make elaboration depend on declaration
// order).
int Vsim::build_module_table(int root) {
    n_mod = 0;
    mt_name.assign(static_cast<std::size_t>(MAX_MODS), std::string());
    mt_node.assign(static_cast<std::size_t>(MAX_MODS), 0);
    mt_nports.assign(static_cast<std::size_t>(MAX_MODS), 0);
    mt_nparam.assign(static_cast<std::size_t>(MAX_MODS), 0);
    mt_ninst.assign(static_cast<std::size_t>(MAX_MODS), 0);
    mt_ngate.assign(static_cast<std::size_t>(MAX_MODS), 0);
    mt_refs.assign(static_cast<std::size_t>(MAX_MODS), 0);

    int m = root;
    while (m != 0) {
        if (n_mod >= MAX_MODS) {
            if (!elab_err)
                std::cerr << "FATAL: module table exhausted (capacity=" << MAX_MODS << ")\n";
            elab_err = true;
            m = 0;                                  // stop, do not corrupt
        } else {
            if (find_module(nd_name[static_cast<std::size_t>(m)]) >= 0)
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(m)], nd_col[static_cast<std::size_t>(m)],
                         "duplicate definition of module '" + nd_name[static_cast<std::size_t>(m)] + "'");
            mt_name[static_cast<std::size_t>(n_mod)]   = nd_name[static_cast<std::size_t>(m)];
            mt_node[static_cast<std::size_t>(n_mod)]   = m;
            mt_nports[static_cast<std::size_t>(n_mod)] = chain_len(nd_a[static_cast<std::size_t>(m)]);
            mt_nparam[static_cast<std::size_t>(n_mod)] = chain_len(nd_c[static_cast<std::size_t>(m)]);
            mt_ninst[static_cast<std::size_t>(n_mod)]  = count_items(nd_b[static_cast<std::size_t>(m)], ND_MOD_INST);
            mt_ngate[static_cast<std::size_t>(n_mod)]  = count_items(nd_b[static_cast<std::size_t>(m)], ND_GATE_INST);
            mt_refs[static_cast<std::size_t>(n_mod)]   = 0;
            n_mod += 1;
            m = nd_next[static_cast<std::size_t>(m)];
        }
    }
    return n_mod;
}

// ===========================================================================
//  2-3. resolve instantiations / verify the instantiated modules exist
// ===========================================================================
// Port association check. Verilog allows either style but not both in one
// instantiation:
//   named       .p(expr)   -> every name must be a port of the target module
//   positional  expr       -> at most one expression per port, in order
// A short positional list is legal Verilog (the trailing ports are left
// unconnected) so it is a warning; a long one cannot be mapped at all, so it
// is an error.
int Vsim::check_conns(int inst, int tgt) {
    int nnamed = 0, npos = 0;
    int c = nd_a[static_cast<std::size_t>(inst)];
    while (c != 0) {
        if (nd_kind[static_cast<std::size_t>(c)] == ND_CONN) {
            nnamed += 1;
            if (find_port_named(tgt, nd_name[static_cast<std::size_t>(c)]) == 0)
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(inst)], nd_col[static_cast<std::size_t>(inst)],
                         "instantiated module has no port named '" + nd_name[static_cast<std::size_t>(c)] + "'");
        } else {
            npos += 1;
        }
        c = nd_next[static_cast<std::size_t>(c)];
    }
    int ntot = nnamed + npos;
    if (nnamed > 0 && npos > 0) {
        add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(inst)], nd_col[static_cast<std::size_t>(inst)],
                 "mixed named and positional connections to module '" + nd_name[static_cast<std::size_t>(inst)] + "'");
    } else if (npos > 0 && npos > mt_nports[static_cast<std::size_t>(tgt)]) {
        add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(inst)], nd_col[static_cast<std::size_t>(inst)],
                 "more port connections than ports on module '" + nd_name[static_cast<std::size_t>(inst)] + "'");
    } else if (npos > 0 && npos < mt_nports[static_cast<std::size_t>(tgt)]) {
        add_diag(SEV_WARNING, nd_line[static_cast<std::size_t>(inst)], nd_col[static_cast<std::size_t>(inst)],
                 "fewer port connections than ports on module '" + nd_name[static_cast<std::size_t>(inst)] + "'");
    }
    return ntot;
}

// Walk one item chain, binding every ND_MOD_INST to its definition. This is
// the pass that makes "verify that instantiated modules exist" true of the
// WHOLE source, not only the part reachable from the top: an undefined
// module inside an unused module is still a design error, and the reference
// counts it produces are what pick_top consumes.
int Vsim::resolve_items(int items) {
    int n = 0, it = items;
    while (it != 0) {
        if (nd_kind[static_cast<std::size_t>(it)] == ND_GENERATE) {
            n += resolve_items(nd_a[static_cast<std::size_t>(it)]);
        } else if (nd_kind[static_cast<std::size_t>(it)] == ND_MOD_INST) {
            int tgt = find_module(nd_name[static_cast<std::size_t>(it)]);
            if (tgt < 0) {
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(it)], nd_col[static_cast<std::size_t>(it)],
                         "instantiation of undefined module '" + nd_name[static_cast<std::size_t>(it)] + "'");
            } else {
                mt_refs[static_cast<std::size_t>(tgt)] += 1;
                check_conns(it, tgt);
                n += 1;
            }
        }
        it = nd_next[static_cast<std::size_t>(it)];
    }
    return n;
}

int Vsim::resolve_instances() {
    int n = 0;
    for (int i = 0; i < n_mod; ++i)
        n += resolve_items(nd_b[static_cast<std::size_t>(mt_node[static_cast<std::size_t>(i)])]);
    return n;
}

// ===========================================================================
//  1. identify the top module
// ===========================================================================
// The top module is the one nothing instantiates. After resolve_instances
// that is simply mt_refs == 0. Three outcomes:
//   exactly one  -> that is the top
//   none         -> every module is instantiated, so the reference graph has
//                   a cycle and the design has no root at all
//   several      -> the source holds independent designs; take the first
//                   declared and say so, or let want_top choose explicitly
int Vsim::pick_top(const std::string& want_top) {
    int r = -1;
    if (n_mod == 0) {
        add_diag(SEV_ERROR, 0, 0, "no modules to elaborate");
    } else if (!want_top.empty()) {
        r = find_module(want_top);
        if (r < 0)
            add_diag(SEV_ERROR, 0, 0, "requested top module is not defined '" + want_top + "'");
    } else {
        int nc = 0, cand = -1;
        for (int i = 0; i < n_mod; ++i) {
            if (mt_refs[static_cast<std::size_t>(i)] == 0) {
                if (nc == 0) cand = i;
                nc += 1;
            }
        }
        if (nc == 0) {
            add_diag(SEV_ERROR, 0, 0,
                     "no top module: every module is instantiated (cyclic hierarchy)");
        } else {
            if (nc > 1)
                add_diag(SEV_WARNING, 0, 0,
                         "several candidate top modules; elaborating '" +
                         mt_name[static_cast<std::size_t>(cand)] + "'");
            r = cand;
        }
    }
    return r;
}

// ===========================================================================
//  4-5. instance objects and the parent-child hierarchy
// ===========================================================================
// Allocate one instance object. -1 once the table is exhausted, and the
// exhaustion is announced once (same policy as the AST arena).
int Vsim::new_inst(const std::string& nm, int mi, int parent, int node) {
    if (n_inst >= inst_cap) {
        if (!elab_err)
            std::cerr << "FATAL: instance table exhausted (capacity=" << inst_cap << ")\n";
        elab_err = true;
        return -1;
    }
    int i = n_inst;
    in_name[static_cast<std::size_t>(i)]   = nm;
    in_mod[static_cast<std::size_t>(i)]    = mi;
    in_parent[static_cast<std::size_t>(i)] = parent;
    in_node[static_cast<std::size_t>(i)]   = node;
    in_child[static_cast<std::size_t>(i)]  = -1;
    in_sib[static_cast<std::size_t>(i)]    = -1;
    in_depth[static_cast<std::size_t>(i)]  = (parent < 0) ? 0 : in_depth[static_cast<std::size_t>(parent)] + 1;
    in_nconn[static_cast<std::size_t>(i)]  = (node == 0) ? 0 : chain_len(nd_a[static_cast<std::size_t>(node)]);
    n_inst += 1;
    return i;
}

// append kid to parent's child list, keeping source order
int Vsim::add_child(int parent, int kid) {
    if (in_child[static_cast<std::size_t>(parent)] < 0) {
        in_child[static_cast<std::size_t>(parent)] = kid;
    } else {
        int t = in_child[static_cast<std::size_t>(parent)];
        while (in_sib[static_cast<std::size_t>(t)] >= 0) t = in_sib[static_cast<std::size_t>(t)];
        in_sib[static_cast<std::size_t>(t)] = kid;
    }
    return kid;
}

// Cycle guard: is module mi already somewhere on the path from instance i up
// to the root? Verilog modules may not be recursively instantiated, and
// without this check elaboration would not terminate.
bool Vsim::mod_on_path(int i, int mi) const {
    int p = i;
    while (p >= 0) {
        if (in_mod[static_cast<std::size_t>(p)] == mi) return true;
        p = in_parent[static_cast<std::size_t>(p)];
    }
    return false;
}

// Elaborate the body of instance `self`: every module instantiation in its
// item chain becomes a child instance, and each child is elaborated in
// turn. Depth-first and in source order, so the instance table comes out in
// preorder and the dump is deterministic. Undefined modules were already
// diagnosed by resolve_items; here they are simply not expanded.
int Vsim::elab_body(int self, int items) {
    int n = 0, it = items;
    while (it != 0 && !elab_err) {
        if (nd_kind[static_cast<std::size_t>(it)] == ND_GENERATE) {
            n += elab_body(self, nd_a[static_cast<std::size_t>(it)]);
        } else if (nd_kind[static_cast<std::size_t>(it)] == ND_MOD_INST) {
            int tgt = find_module(nd_name[static_cast<std::size_t>(it)]);
            if (tgt >= 0) {
                if (mod_on_path(self, tgt)) {
                    add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(it)], nd_col[static_cast<std::size_t>(it)],
                             "recursive instantiation of module '" + nd_name[static_cast<std::size_t>(it)] + "'");
                } else {
                    int cnode = nd_c[static_cast<std::size_t>(it)];
                    std::string iname = (cnode != 0) ? nd_name[static_cast<std::size_t>(cnode)] : std::string();
                    int kid = new_inst(iname, tgt, self, it);
                    if (kid >= 0) {
                        add_child(self, kid);
                        n += 1 + elab_body(kid, nd_b[static_cast<std::size_t>(mt_node[static_cast<std::size_t>(tgt)])]);
                    }
                }
            }
        }
        it = nd_next[static_cast<std::size_t>(it)];
    }
    return n;
}

// ---------------------------------------------------------------------------
// elaborate -- the whole of Task 5.1. Returns the number of instances built;
// success is  (top_mod >= 0 && !elab_err && !had_error).
// ---------------------------------------------------------------------------
int Vsim::elaborate(int root, const std::string& want_top) {
    elab_err = false;
    n_mod    = 0;
    n_inst   = 0;
    top_mod  = -1;
    top_inst = -1;

    in_name.assign(static_cast<std::size_t>(inst_cap), std::string());
    in_mod.assign(static_cast<std::size_t>(inst_cap), 0);
    in_parent.assign(static_cast<std::size_t>(inst_cap), -1);
    in_node.assign(static_cast<std::size_t>(inst_cap), 0);
    in_child.assign(static_cast<std::size_t>(inst_cap), -1);
    in_sib.assign(static_cast<std::size_t>(inst_cap), -1);
    in_depth.assign(static_cast<std::size_t>(inst_cap), 0);
    in_nconn.assign(static_cast<std::size_t>(inst_cap), 0);

    build_module_table(root);           // definitions
    resolve_instances();                // bind + verify + count refs
    top_mod = pick_top(want_top);        // the root of the design

    if (top_mod >= 0 && !elab_err) {
        top_inst = new_inst(mt_name[static_cast<std::size_t>(top_mod)], top_mod, -1, 0);
        if (top_inst >= 0)
            elab_body(top_inst, nd_b[static_cast<std::size_t>(mt_node[static_cast<std::size_t>(top_mod)])]);
    }
    return n_inst;
}

// ===========================================================================
//  hierarchy dump (deterministic, no line/column, same convention as
//  dump_ast: takes an ostream, matches the original's §6 S-expression shape)
// ===========================================================================

// full dotted path of an instance, root first
void Vsim::fput_path(std::ostream& o, int i) const {
    if (in_parent[static_cast<std::size_t>(i)] >= 0) {
        fput_path(o, in_parent[static_cast<std::size_t>(i)]);
        o << ".";
    }
    o << in_name[static_cast<std::size_t>(i)];
}

void Vsim::dump_inst(std::ostream& o, int i, int ind) const {
    o << std::string(static_cast<std::size_t>(ind), ' ');
    o << "(inst \"" << in_name[static_cast<std::size_t>(i)] << "\" of \""
      << mt_name[static_cast<std::size_t>(in_mod[static_cast<std::size_t>(i)])]
      << "\" conns=" << in_nconn[static_cast<std::size_t>(i)];
    int c = in_child[static_cast<std::size_t>(i)];
    while (c >= 0) {
        o << "\n";
        dump_inst(o, c, ind + 2);
        c = in_sib[static_cast<std::size_t>(c)];
    }
    o << ")";
}

// (design "top" (modules ...) (hierarchy ...) (paths ...))
void Vsim::dump_hier(std::ostream& o) const {
    o << "(design ";
    if (top_mod >= 0)
        o << "\"" << mt_name[static_cast<std::size_t>(top_mod)] << "\"";
    else
        o << "<none>";

    o << "\n  (modules";
    if (n_mod == 0) {
        o << ")";
    } else {
        for (int i = 0; i < n_mod; ++i) {
            o << "\n    (ND_MODULE \"" << mt_name[static_cast<std::size_t>(i)]
              << "\" ports=" << mt_nports[static_cast<std::size_t>(i)]
              << " params=" << mt_nparam[static_cast<std::size_t>(i)]
              << " insts=" << mt_ninst[static_cast<std::size_t>(i)]
              << " gates=" << mt_ngate[static_cast<std::size_t>(i)]
              << " refs=" << mt_refs[static_cast<std::size_t>(i)] << ")";
        }
        o << ")";
    }

    o << "\n  (hierarchy";
    if (top_inst < 0) {
        o << ")";
    } else {
        o << "\n";
        dump_inst(o, top_inst, 4);
        o << ")";
    }

    o << "\n  (paths";
    if (n_inst == 0) {
        o << ")";
    } else {
        for (int i = 0; i < n_inst; ++i) {
            o << "\n    \"";
            fput_path(o, i);
            o << "\"";
        }
        o << ")";
    }
    o << ")\n";
}

} // namespace vsim