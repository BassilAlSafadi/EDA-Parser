//============================================================================
// vsim_sig.v  --  Phase 2, Task 5.2: Signal & Port Resolution
//----------------------------------------------------------------------------
// Include fragment.  Same rule as vsim_elab.v: Verilog functions may only
// touch variables in their own module scope, so include this AFTER
// vsim_dump.v, vsim_diag.v and vsim_elab.v in the same enclosing module --
// it uses dfd/sp/fput_ident from vsim_dump.v, add_diag[_id] from
// vsim_diag.v, and mt_*/in_*/n_inst from vsim_elab.v.
//
// GOAL (Elaboration.md, "6. Not in this task", listed as Task 5.2):
//   - signal/net table construction
//   - connection flattening (binding port connections onto nets)
//   - width and direction resolution
//   - parameter override evaluation
//
// Task 5.1 already gives us a validated instance tree: in_* / mt_*, and
// check_conns() already confirmed every instance's connection list is
// well-formed (named XOR positional, arity in range).  This file does not
// re-validate that shape -- it consumes it and turns names into indices.
//
// ---------------------------------------------------------------------------
// KNOWN GAP IN vsim_parser.v (fix before relying on signal diagnostics):
//   parse_net_decl() and parse_reg_decl() never set nd_line[n]/nd_col[n] on
//   the ND_NET / ND_REG nodes they create (parse_module(), parse_mod_inst()
//   and parse_gate_inst() do this for their own node kinds; these two don't).
//   Two 1-line additions fix it -- inside the `while (more)` loop of each,
//   right after `i = eat_tok(0);`:
//       nd_line[n] = tok_line[i]; nd_col[n] = tok_col[i];
//   Without this, "duplicate declaration of signal" diagnostics below will
//   report line 0.
// ---------------------------------------------------------------------------
//
// SCOPE OF THIS SUBSET (be explicit about this in the report, same spirit as
// Specification.md 0.A): connection expressions are resolved as bare
// identifiers only. A bit-select, part-select, or concatenation on the
// connection side is recognised and flagged (SEV_WARNING, "too complex to
// flatten in this subset") rather than mis-resolved or silently dropped --
// none of the ten golden .v designs use anything richer, and getting that
// resolved correctly is really a Task 5.2-and-a-half.  If your golden files
// grow one, extend resolve_conn_expr() rather than working around it.
//============================================================================

//---------------------------------------------------------------- capacities
// Guarded like the arenas in vsim_defs.vh, so a testbench can shrink one to
// exercise the overflow path, and move these into vsim_defs.vh proper
// whenever convenient -- they only live here so this file drops in without
// edits elsewhere.
`ifndef MAX_SIG
  `define MAX_SIG   4096            // total signals (ports+nets) over all instances
`endif
`ifndef MAX_PARAM
  `define MAX_PARAM 1024            // total parameter rows over all instances
`endif
`ifndef MAX_CONN
  `define MAX_CONN  4096            // total resolved port<->net bindings
`endif

// ----------------------------------------------------------- signal table
// One row per port or per locally declared wire/reg, scoped to one instance.
// Ports of instance i always occupy sg_* indices [in_sig0[i] ..
// in_sig0[i]+mt_nports[in_mod[i]]-1), in header order -- build_scope() adds
// them first, so positional connections can index straight into this range.
reg  [`IDW-1:0] sg_name   [0:`MAX_SIG-1];   // local name (not a path)
integer         sg_inst   [0:`MAX_SIG-1];   // owning instance (in_* index)
integer         sg_node   [0:`MAX_SIG-1];   // ND_PORT / ND_NET / ND_REG handle
reg             sg_isport [0:`MAX_SIG-1];
integer         sg_dir    [0:`MAX_SIG-1];   // DIR_* (DIR_NONE for plain nets)
integer         sg_width  [0:`MAX_SIG-1];   // resolved bit width, >= 1
integer         n_sig;

// -------------------------------------------------------- parameter table
// One row per parameter, scoped to one instance: defaults copied from the
// module (header #( ) list, then body `parameter` decls, declaration order),
// then overridden by that instance's own #( ) list.
reg  [`IDW-1:0] pm_name   [0:`MAX_PARAM-1];
integer         pm_inst   [0:`MAX_PARAM-1];
integer         pm_value  [0:`MAX_PARAM-1];
integer         n_param;

// -------------------------------------------------------- connection table
// One row per resolved port<->net binding: cn_port is a sg_* index in the
// CHILD's own scope, cn_net is a sg_* index in the PARENT's scope, or -1 for
// an unconnected port (legal: Task 5.1 §3.3) or an unresolved expression.
integer         cn_inst   [0:`MAX_CONN-1];  // child instance (in_* index)
integer         cn_port   [0:`MAX_CONN-1];
integer         cn_net    [0:`MAX_CONN-1];
integer         n_conn;

integer         in_sig0   [0:`MAX_INST-1];  // first sg_* index for this instance
integer         in_nsig   [0:`MAX_INST-1];  // count of signals in its scope
integer         in_par0   [0:`MAX_INST-1];  // first pm_* index for this instance

reg             sig_err;                    // table overflow: loud, not silent

// ===========================================================================
//  lookups within one instance's own scope
// ===========================================================================

function automatic integer find_sig_local;
    input integer      inst;
    input [`IDW-1:0]   nm;
    integer i, r;
    begin
        r = -1;
        for (i = in_sig0[inst]; i < n_sig; i = i + 1)
            if (r < 0 && sg_inst[i] == inst && sg_name[i] == nm) r = i;
        find_sig_local = r;
    end
endfunction

// port at header position `pos` (0-based) within inst's own scope.  Ports are
// always the first mt_nports[in_mod[inst]] rows of the scope (build_scope
// adds them before any local net/reg), so this is a direct offset -- same
// ordering find_port_named (Task 5.1) reads the header in.
function automatic integer port_local_at;
    input integer inst;
    input integer pos;
    port_local_at = in_sig0[inst] + pos;
endfunction

function automatic integer find_param_local;
    input integer      inst;
    input [`IDW-1:0]   nm;
    integer i, r;
    begin
        r = -1;
        for (i = in_par0[inst]; i < n_param; i = i + 1)
            if (r < 0 && pm_inst[i] == inst && pm_name[i] == nm) r = i;
        find_param_local = r;
    end
endfunction

// ===========================================================================
//  constant-expression evaluator (parameters only -- ranges must fold to a
//  number without touching any signal, exactly as real Verilog requires)
// ===========================================================================
function automatic integer eval_const;
    input integer inst;                       // parameter scope, -1 = none
    input integer e;                          // expression node, 0 = absent
    integer a, b, c, pi;
    begin
        if (e == 0) eval_const = 0;
        else case (nd_kind[e])
            `ND_LITERAL: eval_const = nd_value[e];
            `ND_IDENT: begin
                pi = (inst >= 0) ? find_param_local(inst, nd_name[e]) : -1;
                if (pi < 0) begin
                    ignore = add_diag_id(`SEV_ERROR, nd_line[e], nd_col[e],
                                         "not a constant expression (unknown parameter)",
                                         nd_name[e]);
                    eval_const = 0;
                end else
                    eval_const = pm_value[pi];
            end
            `ND_UNARY: begin
                a = eval_const(inst, nd_a[e]);
                case (nd_op[e])
                    `T_MINUS: eval_const = -a;
                    `T_PLUS:  eval_const = a;
                    `T_NOT:   eval_const = (a == 0) ? 1 : 0;
                    `T_BNOT:  eval_const = ~a;
                    default:  eval_const = (a != 0) ? 1 : 0;  // reduction op on a scalar const
                endcase
            end
            `ND_BINARY: begin
                a = eval_const(inst, nd_a[e]);
                b = eval_const(inst, nd_b[e]);
                case (nd_op[e])
                    `T_PLUS:             eval_const = a + b;
                    `T_MINUS:            eval_const = a - b;
                    `T_STAR:             eval_const = a * b;
                    `T_SLASH:            eval_const = (b != 0) ? a / b : 0;
                    `T_PCT:              eval_const = (b != 0) ? a % b : 0;
                    `T_SHL:              eval_const = a <<< b;
                    `T_SHR:              eval_const = a >>> b;
                    `T_LT:               eval_const = (a <  b) ? 1 : 0;
                    `T_LE:               eval_const = (a <= b) ? 1 : 0;
                    `T_GT:               eval_const = (a >  b) ? 1 : 0;
                    `T_GE:               eval_const = (a >= b) ? 1 : 0;
                    `T_EQ, `T_CASE_EQ:   eval_const = (a == b) ? 1 : 0;
                    `T_NEQ, `T_CASE_NEQ: eval_const = (a != b) ? 1 : 0;
                    `T_AMP:              eval_const = a & b;
                    `T_PIPE:             eval_const = a | b;
                    `T_CARET:            eval_const = a ^ b;
                    `T_XNORT:            eval_const = ~(a ^ b);
                    `T_LAND:             eval_const = (a != 0 && b != 0) ? 1 : 0;
                    `T_LOR:              eval_const = (a != 0 || b != 0) ? 1 : 0;
                    default:             eval_const = 0;
                endcase
            end
            `ND_TERNARY: begin
                c = eval_const(inst, nd_a[e]);
                eval_const = (c != 0) ? eval_const(inst, nd_b[e])
                                      : eval_const(inst, nd_c[e]);
            end
            default: begin
                ignore = add_diag(`SEV_ERROR, nd_line[e], nd_col[e],
                                  "not a constant expression");
                eval_const = 0;
            end
        endcase
    end
endfunction

// ===========================================================================
//  1. net table  (ports, then locally declared wires/regs)
// ===========================================================================
function automatic integer scan_decls;
    input integer inst;
    input integer items;
    integer it, s, n;
    begin
        n = 0; it = items;
        while (it != 0) begin
            if (nd_kind[it] == `ND_GENERATE)
                n = n + scan_decls(inst, nd_a[it]);
            else if (nd_kind[it] == `ND_NET || nd_kind[it] == `ND_REG) begin
                if (n_sig >= `MAX_SIG) begin
                    if (!sig_err) $display("FATAL: signal table exhausted (capacity=%0d)", `MAX_SIG);
                    sig_err = 1'b1;
                end else begin
                    if (find_sig_local(inst, nd_name[it]) >= 0)
                        ignore = add_diag_id(`SEV_ERROR, nd_line[it], nd_col[it],
                                             "duplicate declaration of signal", nd_name[it]);
                    s = n_sig;
                    sg_name  [s] = nd_name[it];
                    sg_inst  [s] = inst;
                    sg_node  [s] = it;
                    sg_isport[s] = 1'b0;
                    sg_dir   [s] = `DIR_NONE;
                    sg_width [s] = 1;             // filled in by resolve_widths
                    n_sig = n_sig + 1;
                    n = n + 1;
                end
            end
            it = nd_next[it];
        end
        scan_decls = n;
    end
endfunction

function automatic integer build_scope;
    input integer inst;
    integer mi, modnode, p, s, first;
    begin
        mi      = in_mod[inst];
        modnode = mt_node[mi];
        first   = n_sig;
        in_sig0[inst] = first;

        p = nd_a[modnode];                        // ports, header order
        while (p != 0) begin
            if (n_sig >= `MAX_SIG) begin
                if (!sig_err) $display("FATAL: signal table exhausted (capacity=%0d)", `MAX_SIG);
                sig_err = 1'b1;
                p = 0;
            end else begin
                s = n_sig;
                sg_name  [s] = nd_name[p];
                sg_inst  [s] = inst;
                sg_node  [s] = p;
                sg_isport[s] = 1'b1;
                sg_dir   [s] = nd_op[p];
                sg_width [s] = 1;
                n_sig = n_sig + 1;
                p = nd_next[p];
            end
        end

        ignore = scan_decls(inst, nd_b[modnode]);  // locally declared wires/regs
        in_nsig[inst] = n_sig - first;
        build_scope = n_sig - first;
    end
endfunction

// ===========================================================================
//  2. parameter table + override application
// ===========================================================================
function automatic integer new_param;
    input integer inst;
    input [`IDW-1:0] nm;
    input integer val;
    integer s;
    begin
        if (n_param >= `MAX_PARAM) begin
            if (!sig_err) $display("FATAL: parameter table exhausted (capacity=%0d)", `MAX_PARAM);
            sig_err = 1'b1;
            new_param = -1;
        end else begin
            s = n_param;
            pm_name [s] = nm;
            pm_inst [s] = inst;
            pm_value[s] = val;
            n_param = n_param + 1;
            new_param = s;
        end
    end
endfunction

function automatic integer scan_params;
    input integer inst;
    input integer items;
    integer it, n;
    begin
        n = 0; it = items;
        while (it != 0) begin
            if (nd_kind[it] == `ND_GENERATE)
                n = n + scan_params(inst, nd_a[it]);
            else if (nd_kind[it] == `ND_PARAM) begin
                ignore = new_param(inst, nd_name[it], eval_const(inst, nd_a[it]));
                n = n + 1;
            end
            it = nd_next[it];
        end
        scan_params = n;
    end
endfunction

// Apply inst's #( ) override list (nd_b[in_node[inst]], parsed but unused
// since Phase 1) onto the parameter table just built for it.  Named
// overrides look up by name; positional overrides consume the table in the
// same order it was just built (header params, then body params) -- the
// simplified-subset version of the LRM's ordered-list rule.
function automatic integer apply_overrides;
    input integer inst;
    integer ov, pi, idx, nnode;
    begin
        idx = 0;
        nnode = in_node[inst];
        ov = (nnode != 0) ? nd_b[nnode] : 0;
        while (ov != 0) begin
            if (nd_name[ov] != `IDW'd0) begin
                pi = find_param_local(inst, nd_name[ov]);
                if (pi < 0)
                    ignore = add_diag_id(`SEV_ERROR, nd_line[nnode], nd_col[nnode],
                                         "parameter override names a parameter that does not exist",
                                         nd_name[ov]);
                else
                    pm_value[pi] = eval_const(in_parent[inst], nd_a[ov]);
            end else begin
                if (idx >= n_param - in_par0[inst])
                    ignore = add_diag(`SEV_ERROR, nd_line[nnode], nd_col[nnode],
                                      "more parameter overrides than parameters");
                else
                    pm_value[in_par0[inst] + idx] = eval_const(in_parent[inst], nd_a[ov]);
                idx = idx + 1;
            end
            ov = nd_next[ov];
        end
        apply_overrides = 0;
    end
endfunction

function automatic integer build_params;
    input integer inst;
    integer mi, modnode, p, first;
    begin
        mi      = in_mod[inst];
        modnode = mt_node[mi];
        first   = n_param;
        in_par0[inst] = first;

        p = nd_c[modnode];                         // header #( ) parameter list
        while (p != 0) begin
            ignore = new_param(inst, nd_name[p], eval_const(inst, nd_a[p]));
            p = nd_next[p];
        end
        ignore = scan_params(inst, nd_b[modnode]);  // body `parameter` decls

        ignore = apply_overrides(inst);
        build_params = n_param - first;
    end
endfunction

// ===========================================================================
//  3. width resolution -- now that every instance's parameter table is final
// ===========================================================================
function automatic integer resolve_widths;
    input integer inst;
    integer i, rng, msb, lsb;
    begin
        for (i = in_sig0[inst]; i < in_sig0[inst] + in_nsig[inst]; i = i + 1) begin
            rng = nd_a[sg_node[i]];                 // ND_RANGE, or 0 => 1 bit
            if (rng == 0)
                sg_width[i] = 1;
            else begin
                msb = eval_const(inst, nd_a[rng]);
                lsb = eval_const(inst, nd_b[rng]);
                sg_width[i] = (msb >= lsb) ? (msb - lsb + 1) : (lsb - msb + 1);
            end
        end
        resolve_widths = 0;
    end
endfunction

// ===========================================================================
//  4. connection flattening
// ===========================================================================
// Resolve one connected expression against a scope.  Bare identifiers only
// (see file header) -- anything else is flagged, not mis-resolved.
function automatic integer resolve_conn_expr;
    input integer scope_inst;
    input integer e;
    integer r;
    begin
        if (e == 0)
            resolve_conn_expr = -1;                 // unconnected port (Task 5.1 §3.3)
        else if (nd_kind[e] == `ND_IDENT) begin
            r = find_sig_local(scope_inst, nd_name[e]);
            if (r < 0)
                ignore = add_diag_id(`SEV_ERROR, nd_line[e], nd_col[e],
                                     "reference to undefined signal", nd_name[e]);
            resolve_conn_expr = r;
        end else begin
            ignore = add_diag(`SEV_WARNING, nd_line[e], nd_col[e],
                              "connection expression too complex to flatten in this subset");
            resolve_conn_expr = -1;
        end
    end
endfunction

function automatic integer new_conn;
    input integer inst;
    input integer portsig;
    input integer netsig;
    integer c;
    begin
        if (n_conn >= `MAX_CONN) begin
            if (!sig_err) $display("FATAL: connection table exhausted (capacity=%0d)", `MAX_CONN);
            sig_err = 1'b1;
            new_conn = -1;
        end else begin
            c = n_conn;
            cn_inst[c] = inst;
            cn_port[c] = portsig;
            cn_net [c] = netsig;
            n_conn = n_conn + 1;
            new_conn = c;
        end
    end
endfunction

// Bind every connection written on inst's ND_MOD_INST to a port-net in its
// own scope and a net in its PARENT's scope.  Named/positional shape and
// arity were already validated by Task 5.1's check_conns; this only maps
// what is already known to be valid.
//
// A port never mentioned -- a short positional list (legal Verilog: trailing
// ports are simply unconnected, Task 5.1 §3.3) or a name just left out of a
// named list -- still gets exactly one row, with cn_net == -1, so the
// connection table always covers every port of every instance: "Connect
// every signal ... report undefined signals or invalid connections" (5.2
// brief) reads as completeness, not best-effort.  `seen` is capped at 64
// ports/instance, matching every module in golden/ with room to spare;
// widen it if a design ever needs more.
function automatic integer flatten_connections;
    input integer inst;
    integer c, portsig, netsig, pos, nnode, nports, pidx;
    reg     seen [0:63];
    begin
        nnode  = in_node[inst];
        nports = mt_nports[in_mod[inst]];
        for (pos = 0; pos < 64; pos = pos + 1) seen[pos] = 1'b0;

        if (nnode != 0) begin
            pos = 0;
            c = nd_a[nnode];
            while (c != 0) begin
                if (nd_kind[c] == `ND_CONN) begin
                    portsig = find_sig_local(inst, nd_name[c]);   // must exist: Task 5.1 checked it
                    netsig  = resolve_conn_expr(in_parent[inst], nd_a[c]);
                    pidx    = portsig - in_sig0[inst];
                end else begin
                    portsig = (pos < nports) ? port_local_at(inst, pos) : -1;
                    netsig  = resolve_conn_expr(in_parent[inst], c);
                    pidx    = pos;
                    pos = pos + 1;
                end
                if (portsig >= 0) begin
                    ignore = new_conn(inst, portsig, netsig);
                    if (pidx >= 0 && pidx < 64) seen[pidx] = 1'b1;
                end
                c = nd_next[c];
            end
        end

        for (pos = 0; pos < nports && pos < 64; pos = pos + 1)
            if (!seen[pos])
                ignore = new_conn(inst, port_local_at(inst, pos), -1);

        flatten_connections = 0;
    end
endfunction

// ===========================================================================
//  driver -- the whole of Task 5.2. Call after elaborate() (Task 5.1).
// ===========================================================================
// Three passes over the instance table (already in preorder from Task 5.1):
//   1. every instance's own net + parameter table -- independent of siblings,
//      but all must exist before pass 3 can look sideways into a parent
//   2. widths, now that every instance's parameter table is final
//   3. connection flattening, parent-scope lookups now safe
function automatic integer resolve_signals;
    input integer u;                                // dummy (Verilog-2001 §10.3.1)
    integer i;
    begin
        sig_err = 1'b0;
        n_sig = 0; n_param = 0; n_conn = 0;
        for (i = 0; i < n_inst; i = i + 1) begin
            ignore = build_scope(i);
            ignore = build_params(i);
        end
        for (i = 0; i < n_inst; i = i + 1)
            ignore = resolve_widths(i);
        for (i = 0; i < n_inst; i = i + 1)
            ignore = flatten_connections(i);
        resolve_signals = n_conn;
    end
endfunction

// ===========================================================================
//  dump  (§6 style: deterministic, no line/column -- same convention as
//  dump_hier in vsim_elab.v; reuses dfd/sp/fput_ident/fput_path from there)
// ===========================================================================
task automatic fput_dir;
    input integer d;
    input integer fdo;
    begin
        case (d)
            `DIR_INPUT:  $fwrite(fdo, "input");
            `DIR_OUTPUT: $fwrite(fdo, "output");
            `DIR_INOUT:  $fwrite(fdo, "inout");
            default:     $fwrite(fdo, "net");
        endcase
    end
endtask

task dump_sig;
    input integer fd;
    integer i, c;
    begin
        dfd = fd;
        $fwrite(dfd, "(signals");
        for (i = 0; i < n_inst; i = i + 1) begin
            $fwrite(dfd, "\n"); sp(2);
            $fwrite(dfd, "(inst \""); fput_path(i, dfd); $fwrite(dfd, "\"");
            for (c = in_sig0[i]; c < in_sig0[i] + in_nsig[i]; c = c + 1) begin
                $fwrite(dfd, "\n"); sp(4);
                $fwrite(dfd, "(sig \""); fput_ident(sg_name[c], dfd);
                $fwrite(dfd, "\" "); fput_dir(sg_dir[c], dfd);
                $fwrite(dfd, " width=%0d)", sg_width[c]);
            end
            $fwrite(dfd, ")");
        end
        $fwrite(dfd, ")\n");

        $fwrite(dfd, "(connections");
        for (i = 0; i < n_conn; i = i + 1) begin
            $fwrite(dfd, "\n"); sp(2);
            $fwrite(dfd, "(bind \""); fput_path(cn_inst[i], dfd);
            $fwrite(dfd, ".\""); fput_ident(sg_name[cn_port[i]], dfd);
            $fwrite(dfd, "\" -> ");
            if (cn_net[i] < 0) $fwrite(dfd, "<unconnected>");
            else begin
                $fwrite(dfd, "\""); fput_path(in_parent[cn_inst[i]], dfd);
                $fwrite(dfd, ".\""); fput_ident(sg_name[cn_net[i]], dfd);
                $fwrite(dfd, "\"");
            end
            $fwrite(dfd, ")");
        end
        $fwrite(dfd, ")\n");
    end
endtask