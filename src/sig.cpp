//============================================================================
// sig.cpp -- Phase 2, Task 5.2: Signal & Port Resolution
//----------------------------------------------------------------------------
// Ported 1:1 from rtl/vsim_sig.v. Consumes the validated instance tree that
// elab.cpp (Task 5.1) already built -- mt_*/in_*/n_inst -- and turns names
// into indices:
//   - signal/net table construction               -> build_scope / scan_decls
//   - connection flattening onto nets              -> flatten_connections
//   - width and direction resolution                -> resolve_widths
//   - parameter override evaluation                  -> build_params / apply_overrides
//
// check_conns() (Task 5.1) already confirmed every instance's connection
// list is well-formed (named XOR positional, arity in range). This file
// does not re-validate that shape -- it consumes it.
//
// ---------------------------------------------------------------------------
// SAME KNOWN GAP AS THE ORIGINAL: parse_net_decl()/parse_reg_decl() in the
// parser must set nd_line/nd_col on the ND_NET/ND_REG nodes they create, or
// "duplicate declaration of signal" diagnostics below will report line 0.
// ---------------------------------------------------------------------------
//
// SCOPE OF THIS SUBSET (state this in the report, same spirit as
// Specification.md 0.A): connection expressions are resolved as bare
// identifiers only. A bit-select, part-select, or concatenation on the
// connection side is recognised and flagged (SEV_WARNING, "too complex to
// flatten in this subset") rather than mis-resolved or silently dropped.
//

#include "vsim.hpp"

#include <cstring>
#include <iostream>
#include <ostream>
#include <string>

namespace vsim {

// ===========================================================================
//  lookups within one instance's own scope
// ===========================================================================

int Vsim::find_sig_local(int inst, const std::string& nm) const {
    for (int i = in_sig0[static_cast<std::size_t>(inst)]; i < n_sig; ++i)
        if (sg_inst[static_cast<std::size_t>(i)] == inst && sg_name[static_cast<std::size_t>(i)] == nm)
            return i;
    return -1;
}

// port at header position `pos` (0-based) within inst's own scope. Ports are
// always the first mt_nports[in_mod[inst]] rows of the scope (build_scope
// adds them before any local net/reg), so this is a direct offset -- same
// ordering find_port_named (Task 5.1) reads the header in.
int Vsim::port_local_at(int inst, int pos) const {
    return in_sig0[static_cast<std::size_t>(inst)] + pos;
}

int Vsim::find_param_local(int inst, const std::string& nm) const {
    for (int i = in_par0[static_cast<std::size_t>(inst)]; i < n_param; ++i)
        if (pm_inst[static_cast<std::size_t>(i)] == inst && pm_name[static_cast<std::size_t>(i)] == nm)
            return i;
    return -1;
}

// ===========================================================================
//  constant-expression evaluator (parameters only -- ranges must fold to a
//  number without touching any signal, exactly as real Verilog requires)
// ===========================================================================
int Vsim::eval_const(int inst, int e) {
    if (e == 0) return 0;

    switch (nd_kind[static_cast<std::size_t>(e)]) {
        case ND_LITERAL: {
            // A parameter/range expression must fold to a fully-known value
            // -- real Verilog requires this too. An x/z bit here means the
            // literal itself can't be a constant expression (not a lexer or
            // width bug), so this is diagnosed the same way an unresolvable
            // identifier is, just below.
            int w = nd_width[static_cast<std::size_t>(e)];
            if (w <= 0) w = 32;   // unsized literal defaults to 32 bits, same as elsewhere
            if (nd_value[static_cast<std::size_t>(e)].hasUnknown(w)) {
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(e)], nd_col[static_cast<std::size_t>(e)],
                         "not a constant expression (x/z bit in literal)");
                return 0;
            }
            return static_cast<int>(nd_value[static_cast<std::size_t>(e)].toUint(w));
        }

        case ND_IDENT: {
            int pi = (inst >= 0) ? find_param_local(inst, nd_name[static_cast<std::size_t>(e)]) : -1;
            if (pi < 0) {
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(e)], nd_col[static_cast<std::size_t>(e)],
                         "not a constant expression (unknown parameter) '" +
                         nd_name[static_cast<std::size_t>(e)] + "'");
                return 0;
            }
            return pm_value[static_cast<std::size_t>(pi)];
        }

        case ND_UNARY: {
            int a = eval_const(inst, nd_a[static_cast<std::size_t>(e)]);
            switch (nd_op[static_cast<std::size_t>(e)]) {
                case T_MINUS: return -a;
                case T_PLUS:  return a;
                case T_NOT:   return (a == 0) ? 1 : 0;
                case T_BNOT:  return ~a;
                default:      return (a != 0) ? 1 : 0;   // reduction op on a scalar const
            }
        }

        case ND_BINARY: {
            int a = eval_const(inst, nd_a[static_cast<std::size_t>(e)]);
            int b = eval_const(inst, nd_b[static_cast<std::size_t>(e)]);
            switch (nd_op[static_cast<std::size_t>(e)]) {
                case T_PLUS:              return a + b;
                case T_MINUS:             return a - b;
                case T_STAR:              return a * b;
                case T_SLASH:             return (b != 0) ? a / b : 0;
                case T_PCT:               return (b != 0) ? a % b : 0;
                case T_SHL:               return a << b;
                case T_SHR:               return a >> b;
                case T_LT:                return (a <  b) ? 1 : 0;
                case T_LE:                return (a <= b) ? 1 : 0;
                case T_GT:                return (a >  b) ? 1 : 0;
                case T_GE:                return (a >= b) ? 1 : 0;
                case T_EQ:      case T_CASE_EQ:  return (a == b) ? 1 : 0;
                case T_NEQ:     case T_CASE_NEQ: return (a != b) ? 1 : 0;
                case T_AMP:               return a & b;
                case T_PIPE:              return a | b;
                case T_CARET:             return a ^ b;
                case T_XNORT:             return ~(a ^ b);
                case T_LAND:              return (a != 0 && b != 0) ? 1 : 0;
                case T_LOR:               return (a != 0 || b != 0) ? 1 : 0;
                default:                  return 0;
            }
        }

        case ND_TERNARY: {
            int c = eval_const(inst, nd_a[static_cast<std::size_t>(e)]);
            return (c != 0) ? eval_const(inst, nd_b[static_cast<std::size_t>(e)])
                             : eval_const(inst, nd_c[static_cast<std::size_t>(e)]);
        }

        default:
            add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(e)], nd_col[static_cast<std::size_t>(e)],
                     "not a constant expression");
            return 0;
    }
}

// ===========================================================================
//  1. signal table (ports, then locally declared wires/regs)
// ===========================================================================
int Vsim::scan_decls(int inst, int items) {
    int n = 0, it = items;
    while (it != 0) {
        if (nd_kind[static_cast<std::size_t>(it)] == ND_GENERATE) {
            n += scan_decls(inst, nd_a[static_cast<std::size_t>(it)]);
        } else if (nd_kind[static_cast<std::size_t>(it)] == ND_NET ||
                   nd_kind[static_cast<std::size_t>(it)] == ND_REG) {
            if (n_sig >= MAX_SIG) {
                if (!sig_err)
                    std::cerr << "FATAL: signal table exhausted (capacity=" << MAX_SIG << ")\n";
                sig_err = true;
            } else {
                int existing = find_sig_local(inst, nd_name[static_cast<std::size_t>(it)]);

                // BUGFIX (verified 2026-08-14, found by test_sim.cpp's own
                // Suite 1): a port declared in the header with no net type
                // yet (e.g. `output y;`) is legitimately completed by a
                // later `wire y;` / `reg y;` of the SAME name -- that is
                // standard, LRM-legal non-ANSI Verilog style (IEEE
                // 1364-2001 12.3.3), not a duplicate declaration.
                // Reproduced directly: `module m(output y); wire y; assign
                // y = 1'b1; endmodule` was being rejected with "duplicate
                // declaration of signal 'y'" even though it's valid
                // Verilog. Fix: only treat this as a real duplicate if the
                // existing entry is NOT an as-yet-untyped port -- i.e. it's
                // a plain net/reg, or a port whose type was already
                // completed by an earlier net/reg decl (sg_node no longer
                // points at the raw ND_PORT node in that case).
                bool completes_untyped_port =
                    existing >= 0 && sg_isport[static_cast<std::size_t>(existing)] &&
                    nd_kind[static_cast<std::size_t>(sg_node[static_cast<std::size_t>(existing)])] == ND_PORT;

                if (completes_untyped_port) {
                    // Attach this net/reg decl to the port's existing row
                    // instead of creating a second row for the same name --
                    // this also lets resolve_widths() below pick up an
                    // explicit width from the wire/reg decl (e.g. `output
                    // y; wire [3:0] y;`) instead of defaulting to 1 bit.
                    sg_node[static_cast<std::size_t>(existing)] = it;
                } else {
                    if (existing >= 0)
                        add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(it)], nd_col[static_cast<std::size_t>(it)],
                                 "duplicate declaration of signal '" + nd_name[static_cast<std::size_t>(it)] + "'");
                    int s = n_sig;
                    sg_name[static_cast<std::size_t>(s)]   = nd_name[static_cast<std::size_t>(it)];
                    sg_inst[static_cast<std::size_t>(s)]   = inst;
                    sg_node[static_cast<std::size_t>(s)]   = it;
                    sg_isport[static_cast<std::size_t>(s)] = false;
                    sg_dir[static_cast<std::size_t>(s)]    = DIR_NONE;
                    sg_width[static_cast<std::size_t>(s)]  = 1;   // filled in by resolve_widths
                    n_sig += 1;
                }
                n += 1;
            }
        }
        it = nd_next[static_cast<std::size_t>(it)];
    }
    return n;
}

int Vsim::build_scope(int inst) {
    int mi      = in_mod[static_cast<std::size_t>(inst)];
    int modnode = mt_node[static_cast<std::size_t>(mi)];
    int first   = n_sig;
    in_sig0[static_cast<std::size_t>(inst)] = first;

    int p = nd_a[static_cast<std::size_t>(modnode)];   // ports, header order
    while (p != 0) {
        if (n_sig >= MAX_SIG) {
            if (!sig_err)
                std::cerr << "FATAL: signal table exhausted (capacity=" << MAX_SIG << ")\n";
            sig_err = true;
            p = 0;
        } else {
            int s = n_sig;
            sg_name[static_cast<std::size_t>(s)]   = nd_name[static_cast<std::size_t>(p)];
            sg_inst[static_cast<std::size_t>(s)]   = inst;
            sg_node[static_cast<std::size_t>(s)]   = p;
            sg_isport[static_cast<std::size_t>(s)] = true;
            sg_dir[static_cast<std::size_t>(s)]    = nd_op[static_cast<std::size_t>(p)];
            sg_width[static_cast<std::size_t>(s)]  = 1;
            n_sig += 1;
            p = nd_next[static_cast<std::size_t>(p)];
        }
    }

    scan_decls(inst, nd_b[static_cast<std::size_t>(modnode)]);   // locally declared wires/regs
    in_nsig[static_cast<std::size_t>(inst)] = n_sig - first;
    return n_sig - first;
}

// ===========================================================================
//  2. parameter table + override application
// ===========================================================================
int Vsim::new_param(int inst, const std::string& nm, int val) {
    if (n_param >= MAX_PARAM) {
        if (!sig_err)
            std::cerr << "FATAL: parameter table exhausted (capacity=" << MAX_PARAM << ")\n";
        sig_err = true;
        return -1;
    }
    int s = n_param;
    pm_name[static_cast<std::size_t>(s)]  = nm;
    pm_inst[static_cast<std::size_t>(s)]  = inst;
    pm_value[static_cast<std::size_t>(s)] = val;
    n_param += 1;
    return s;
}

int Vsim::scan_params(int inst, int items) {
    int n = 0, it = items;
    while (it != 0) {
        if (nd_kind[static_cast<std::size_t>(it)] == ND_GENERATE) {
            n += scan_params(inst, nd_a[static_cast<std::size_t>(it)]);
        } else if (nd_kind[static_cast<std::size_t>(it)] == ND_PARAM) {
            new_param(inst, nd_name[static_cast<std::size_t>(it)],
                      eval_const(inst, nd_a[static_cast<std::size_t>(it)]));
            n += 1;
        }
        it = nd_next[static_cast<std::size_t>(it)];
    }
    return n;
}

// Apply inst's #( ) override list (nd_b[in_node[inst]]) onto the parameter
// table just built for it. Named overrides look up by name; positional
// overrides consume the table in the same order it was just built (header
// params, then body params) -- the simplified-subset version of the LRM's
// ordered-list rule.
int Vsim::apply_overrides(int inst) {
    int idx = 0;
    int nnode = in_node[static_cast<std::size_t>(inst)];
    int ov = (nnode != 0) ? nd_b[static_cast<std::size_t>(nnode)] : 0;
    while (ov != 0) {
        if (!nd_name[static_cast<std::size_t>(ov)].empty()) {
            int pi = find_param_local(inst, nd_name[static_cast<std::size_t>(ov)]);
            if (pi < 0)
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(nnode)], nd_col[static_cast<std::size_t>(nnode)],
                         "parameter override names a parameter that does not exist '" +
                         nd_name[static_cast<std::size_t>(ov)] + "'");
            else
                pm_value[static_cast<std::size_t>(pi)] =
                    eval_const(in_parent[static_cast<std::size_t>(inst)], nd_a[static_cast<std::size_t>(ov)]);
        } else {
            if (idx >= n_param - in_par0[static_cast<std::size_t>(inst)]) {
                add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(nnode)], nd_col[static_cast<std::size_t>(nnode)],
                         "more parameter overrides than parameters");
            } else {
                pm_value[static_cast<std::size_t>(in_par0[static_cast<std::size_t>(inst)] + idx)] =
                    eval_const(in_parent[static_cast<std::size_t>(inst)], nd_a[static_cast<std::size_t>(ov)]);
            }
            idx += 1;
        }
        ov = nd_next[static_cast<std::size_t>(ov)];
    }
    return 0;
}

int Vsim::build_params(int inst) {
    int mi      = in_mod[static_cast<std::size_t>(inst)];
    int modnode = mt_node[static_cast<std::size_t>(mi)];
    int first   = n_param;
    in_par0[static_cast<std::size_t>(inst)] = first;

    int p = nd_c[static_cast<std::size_t>(modnode)];   // header #( ) parameter list
    while (p != 0) {
        new_param(inst, nd_name[static_cast<std::size_t>(p)], eval_const(inst, nd_a[static_cast<std::size_t>(p)]));
        p = nd_next[static_cast<std::size_t>(p)];
    }
    scan_params(inst, nd_b[static_cast<std::size_t>(modnode)]);   // body `parameter` decls

    apply_overrides(inst);
    return n_param - first;
}

// ===========================================================================
//  3. width resolution -- now that every instance's parameter table is final
// ===========================================================================
int Vsim::resolve_widths(int inst) {
    int lo = in_sig0[static_cast<std::size_t>(inst)];
    int hi = lo + in_nsig[static_cast<std::size_t>(inst)];
    for (int i = lo; i < hi; ++i) {
        int rng = nd_a[static_cast<std::size_t>(sg_node[static_cast<std::size_t>(i)])];   // ND_RANGE, or 0 => 1 bit
        if (rng == 0) {
            sg_width[static_cast<std::size_t>(i)] = 1;
        } else {
            int msb = eval_const(inst, nd_a[static_cast<std::size_t>(rng)]);
            int lsb = eval_const(inst, nd_b[static_cast<std::size_t>(rng)]);
            sg_width[static_cast<std::size_t>(i)] = (msb >= lsb) ? (msb - lsb + 1) : (lsb - msb + 1);
        }
    }
    return 0;
}

// ===========================================================================
//  4. connection flattening
// ===========================================================================
// Resolve one connected expression against a scope. Bare identifiers only
// (see file header) -- anything else is flagged, not mis-resolved.
int Vsim::resolve_conn_expr(int scope_inst, int e) {
    if (e == 0)
        return -1;   // unconnected port (Task 5.1 §3.3)

    if (nd_kind[static_cast<std::size_t>(e)] == ND_IDENT) {
        int r = find_sig_local(scope_inst, nd_name[static_cast<std::size_t>(e)]);
        if (r < 0)
            add_diag(SEV_ERROR, nd_line[static_cast<std::size_t>(e)], nd_col[static_cast<std::size_t>(e)],
                     "reference to undefined signal '" + nd_name[static_cast<std::size_t>(e)] + "'");
        return r;
    }

    add_diag(SEV_WARNING, nd_line[static_cast<std::size_t>(e)], nd_col[static_cast<std::size_t>(e)],
             "connection expression too complex to flatten in this subset");
    return -1;
}

int Vsim::new_conn(int inst, int portsig, int netsig) {
    if (n_conn >= MAX_CONN) {
        if (!sig_err)
            std::cerr << "FATAL: connection table exhausted (capacity=" << MAX_CONN << ")\n";
        sig_err = true;
        return -1;
    }
    int c = n_conn;
    cn_inst[static_cast<std::size_t>(c)] = inst;
    cn_port[static_cast<std::size_t>(c)] = portsig;
    cn_net[static_cast<std::size_t>(c)]  = netsig;
    n_conn += 1;
    return c;
}

// Bind every connection written on inst's ND_MOD_INST to a port-net in its
// own scope and a net in its PARENT's scope. Named/positional shape and
// arity were already validated by Task 5.1's check_conns; this only maps
// what is already known to be valid.
//
// A port never mentioned (a short positional list, or a name left out of a
// named list) still gets exactly one row, with cn_net == -1, so the
// connection table always covers every port of every instance. `seen` is
// capped at 64 ports/instance, matching every module in golden/ with room
// to spare; widen it if a design ever needs more.
int Vsim::flatten_connections(int inst) {
    int nnode  = in_node[static_cast<std::size_t>(inst)];
    int nports = mt_nports[static_cast<std::size_t>(in_mod[static_cast<std::size_t>(inst)])];
    bool seen[64];
    std::memset(seen, 0, sizeof(seen));

    if (nnode != 0) {
        int pos = 0;
        int c = nd_a[static_cast<std::size_t>(nnode)];
        while (c != 0) {
            int portsig, netsig, pidx;
            if (nd_kind[static_cast<std::size_t>(c)] == ND_CONN) {
                portsig = find_sig_local(inst, nd_name[static_cast<std::size_t>(c)]);   // must exist: Task 5.1 checked it
                netsig  = resolve_conn_expr(in_parent[static_cast<std::size_t>(inst)], nd_a[static_cast<std::size_t>(c)]);
                pidx    = portsig - in_sig0[static_cast<std::size_t>(inst)];
            } else {
                portsig = (pos < nports) ? port_local_at(inst, pos) : -1;
                netsig  = resolve_conn_expr(in_parent[static_cast<std::size_t>(inst)], c);
                pidx    = pos;
                pos += 1;
            }
            if (portsig >= 0) {
                new_conn(inst, portsig, netsig);
                if (pidx >= 0 && pidx < 64) seen[pidx] = true;
            }
            c = nd_next[static_cast<std::size_t>(c)];
        }
    }

    for (int pos = 0; pos < nports && pos < 64; ++pos)
        if (!seen[pos])
            new_conn(inst, port_local_at(inst, pos), -1);

    return 0;
}

// ===========================================================================
//  driver -- the whole of Task 5.2. Call after elaborate() (Task 5.1).
// ===========================================================================
// Three passes over the instance table (already in preorder from Task 5.1):
//   1. every instance's own net + parameter table -- independent of
//      siblings, but all must exist before pass 3 can look sideways into a
//      parent
//   2. widths, now that every instance's parameter table is final
//   3. connection flattening, parent-scope lookups now safe
int Vsim::resolve_signals() {
    sig_err = false;
    n_sig = 0; n_param = 0; n_conn = 0;

    sg_name.assign(static_cast<std::size_t>(MAX_SIG), std::string());
    sg_inst.assign(static_cast<std::size_t>(MAX_SIG), 0);
    sg_node.assign(static_cast<std::size_t>(MAX_SIG), 0);
    sg_isport.assign(static_cast<std::size_t>(MAX_SIG), false);
    sg_dir.assign(static_cast<std::size_t>(MAX_SIG), 0);
    sg_width.assign(static_cast<std::size_t>(MAX_SIG), 0);

    pm_name.assign(static_cast<std::size_t>(MAX_PARAM), std::string());
    pm_inst.assign(static_cast<std::size_t>(MAX_PARAM), 0);
    pm_value.assign(static_cast<std::size_t>(MAX_PARAM), 0);

    cn_inst.assign(static_cast<std::size_t>(MAX_CONN), 0);
    cn_port.assign(static_cast<std::size_t>(MAX_CONN), 0);
    cn_net.assign(static_cast<std::size_t>(MAX_CONN), 0);

    in_sig0.assign(static_cast<std::size_t>(n_inst), 0);
    in_nsig.assign(static_cast<std::size_t>(n_inst), 0);
    in_par0.assign(static_cast<std::size_t>(n_inst), 0);

    for (int i = 0; i < n_inst; ++i) {
        build_scope(i);
        build_params(i);
    }
    for (int i = 0; i < n_inst; ++i)
        resolve_widths(i);
    for (int i = 0; i < n_inst; ++i)
        flatten_connections(i);

    return n_conn;
}

// ===========================================================================
//  dump (deterministic, no line/column -- same convention as dump_hier;
//  reuses fput_path from elab.cpp)
// ===========================================================================
static void fput_dir(std::ostream& o, int d) {
    switch (d) {
        case DIR_INPUT:  o << "input";  break;
        case DIR_OUTPUT: o << "output"; break;
        case DIR_INOUT:  o << "inout";  break;
        default:         o << "net";    break;
    }
}

void Vsim::dump_sig(std::ostream& o) const {
    o << "(signals";
    for (int i = 0; i < n_inst; ++i) {
        o << "\n  (inst \"";
        fput_path(o, i);
        o << "\"";
        int lo = in_sig0[static_cast<std::size_t>(i)];
        int hi = lo + in_nsig[static_cast<std::size_t>(i)];
        for (int c = lo; c < hi; ++c) {
            o << "\n    (sig \"" << sg_name[static_cast<std::size_t>(c)] << "\" ";
            fput_dir(o, sg_dir[static_cast<std::size_t>(c)]);
            o << " width=" << sg_width[static_cast<std::size_t>(c)] << ")";
        }
        o << ")";
    }
    o << ")\n";

    o << "(connections";
    for (int i = 0; i < n_conn; ++i) {
        o << "\n  (bind \"";
        fput_path(o, cn_inst[static_cast<std::size_t>(i)]);
        o << ".\"" << sg_name[static_cast<std::size_t>(cn_port[static_cast<std::size_t>(i)])] << "\" -> ";
        if (cn_net[static_cast<std::size_t>(i)] < 0) {
            o << "<unconnected>";
        } else {
            o << "\"";
            fput_path(o, in_parent[static_cast<std::size_t>(cn_inst[static_cast<std::size_t>(i)])]);
            o << ".\"" << sg_name[static_cast<std::size_t>(cn_net[static_cast<std::size_t>(i)])] << "\"";
        }
        o << ")";
    }
    o << ")\n";
}

} // namespace vsim