//============================================================================
// vsim_elab.v  --  Phase 2, Task 5.1: Module Hierarchy Resolution
//----------------------------------------------------------------------------
// Include fragment (same rule as the other fragments: Verilog functions may
// only touch variables in their own module scope, so the tables and every
// subprogram that walks them live in one module).  Include AFTER vsim_dump.v,
// because the hierarchy dump reuses `dfd`, `sp` and `fput_ident`.
//
// GOAL (project brief, "construct an internal representation of the design"):
// build the complete design hierarchy from the parsed target modules.
//
// The AST that Phase 1 produces is a *syntactic* object: a chain of ND_MODULE
// nodes, each holding a chain of items, with module instantiations recorded as
// ND_MOD_INST nodes that name a module by text only.  Nothing in it says which
// module is the top, whether an instantiated name exists, or what the design
// tree looks like.  Elaboration answers exactly those questions, and it is the
// step every real EDA flow performs between reading HDL and simulating it
// (06 ASIC Design Flow, "Design" step).
//
// This file implements the five responsibilities of Task 5.1:
//   1. identify the top module                       -> pick_top
//   2. resolve all module instantiations             -> resolve_instances
//   3. verify that instantiated modules exist        -> resolve_items
//   4. create module instance objects                -> new_inst
//   5. build the parent-child hierarchy              -> elab_body / in_child
//
// Signal/net tables and connectivity flattening are Task 5.2 and are NOT done
// here; the only connection work below is the arity/name check that "resolving
// an instantiation" requires (an instance whose ports cannot be matched has
// not been resolved).
//
// Representation.  Two tables of parallel arrays, in the same arena style as
// the AST (§2.5), integer indices as handles.  Unlike the AST, handle 0 is a
// real entry here and -1 is the null handle: module 0 is a legitimate module,
// and instance 0 is the root of the design, so reserving 0 would waste the
// most-used slot.
//============================================================================

// ----------------------------------------------------------- module table
// One row per module DEFINITION found in the target source, in declaration
// order.  mt_refs counts static references (how many ND_MOD_INST nodes name
// this module anywhere in the source) -- it is the top-module test, not an
// instance count: a module instantiated once inside a module that is itself
// instantiated four times has mt_refs == 1 and four elaborated instances.
reg  [`IDW-1:0] mt_name  [0:`MAX_MODS-1];   // module name
integer         mt_node  [0:`MAX_MODS-1];   // its ND_MODULE handle in the AST
integer         mt_nports[0:`MAX_MODS-1];   // ports in the header
integer         mt_nparam[0:`MAX_MODS-1];   // parameters in #( )
integer         mt_ninst [0:`MAX_MODS-1];   // module instantiations inside it
integer         mt_ngate [0:`MAX_MODS-1];   // gate primitives inside it
integer         mt_refs  [0:`MAX_MODS-1];   // static references to it
integer         n_mod;

// --------------------------------------------------------- instance table
// One row per ELABORATED instance -- the "module instance objects" of Task
// 5.1.  The hierarchy is a first-child / next-sibling tree, which keeps the
// parent-child relation explicit in both directions and needs no per-node
// child array:  in_child[i] is i's first child, in_sib[c] the next child of
// the same parent, in_parent[c] the way back up.  Source order is preserved.
reg  [`IDW-1:0] in_name  [0:`MAX_INST-1];   // instance name (leaf, not a path)
integer         in_mod   [0:`MAX_INST-1];   // module-table index of its type
integer         in_parent[0:`MAX_INST-1];   // enclosing instance, -1 at root
integer         in_node  [0:`MAX_INST-1];   // its ND_MOD_INST handle, 0 at root
integer         in_child [0:`MAX_INST-1];   // first child, -1 if a leaf
integer         in_sib   [0:`MAX_INST-1];   // next sibling, -1 if last
integer         in_depth [0:`MAX_INST-1];   // 0 at the root
integer         in_nconn [0:`MAX_INST-1];   // port connections written for it
integer         n_inst;

integer         top_mod;                    // module-table index of the top, -1
integer         top_inst;                   // root instance index, -1
reg             elab_err;                   // table overflow: loud, not silent

// Effective instance-table capacity, the elaboration counterpart of node_cap
// (§9.10).  elaborate() does NOT reset it -- a run may lower it (vsim_top's
// +instcap=<N>) to demonstrate the overflow path on a small design -- so every
// caller sets it before calling elaborate.
integer         inst_cap;

// ===========================================================================
//  small helpers over the AST
// ===========================================================================

// length of an nd_next sibling chain
function automatic integer chain_len;
    input integer h;
    integer c, x;
    begin
        c = 0; x = h;
        while (x != 0) begin c = c + 1; x = nd_next[x]; end
        chain_len = c;
    end
endfunction

// count items of one kind in a module-item chain.  Generate blocks hold their
// items in nd_a, so they are descended into rather than counted (§4.1).
function automatic integer count_items;
    input integer   h;
    input [7:0]     k;
    integer c, it;
    begin
        c = 0; it = h;
        while (it != 0) begin
            if (nd_kind[it] == `ND_GENERATE) c = c + count_items(nd_a[it], k);
            else if (nd_kind[it] == k)       c = c + 1;
            it = nd_next[it];
        end
        count_items = c;
    end
endfunction

// module-table lookup by name; -1 when the module is not defined.  Linear:
// n_mod is tiny (MAX_MODS = 64) and a linear scan keeps the table in
// declaration order, which the dump depends on for determinism.
function automatic integer find_module;
    input [`IDW-1:0] nm;
    integer i, r;
    begin
        r = -1;
        for (i = 0; i < n_mod; i = i + 1)
            if (r < 0 && mt_name[i] == nm) r = i;
        find_module = r;
    end
endfunction

// port handle of the named port of module mi, or 0 if it has no such port
function automatic integer find_port_named;
    input integer    mi;
    input [`IDW-1:0] nm;
    integer p, r;
    begin
        r = 0; p = nd_a[mt_node[mi]];
        while (p != 0) begin
            if (r == 0 && nd_name[p] == nm) r = p;
            p = nd_next[p];
        end
        find_port_named = r;
    end
endfunction

// ===========================================================================
//  1. module table
// ===========================================================================
// Flatten the ND_MODULE chain into the module table and record the per-module
// statistics the later passes and the dump need.  A repeated module name is an
// error (the second definition is unreachable, and silently keeping the first
// would make elaboration depend on declaration order).
function automatic integer build_module_table;
    input integer root;
    integer m;
    begin
        n_mod = 0;
        m = root;
        while (m != 0) begin
            if (n_mod >= `MAX_MODS) begin
                if (!elab_err)
                    $display("FATAL: module table exhausted (capacity=%0d)", `MAX_MODS);
                elab_err = 1'b1;
                m = 0;                          // stop, do not corrupt
            end else begin
                if (find_module(nd_name[m]) >= 0)
                    ignore = add_diag_id(`SEV_ERROR, nd_line[m], nd_col[m],
                                         "duplicate definition of module", nd_name[m]);
                mt_name  [n_mod] = nd_name[m];
                mt_node  [n_mod] = m;
                mt_nports[n_mod] = chain_len(nd_a[m]);
                mt_nparam[n_mod] = chain_len(nd_c[m]);
                mt_ninst [n_mod] = count_items(nd_b[m], `ND_MOD_INST);
                mt_ngate [n_mod] = count_items(nd_b[m], `ND_GATE_INST);
                mt_refs  [n_mod] = 0;
                n_mod = n_mod + 1;
                m = nd_next[m];
            end
        end
        build_module_table = n_mod;
    end
endfunction

// ===========================================================================
//  2-3. resolve instantiations / verify the instantiated modules exist
// ===========================================================================
// Port association check.  Verilog allows either style but not both in one
// instantiation (§4.3):
//   named       .p(expr)   -> every name must be a port of the target module
//   positional  expr       -> at most one expression per port, in order
// A short positional list is legal Verilog (the trailing ports are left
// unconnected) so it is a warning; a long one cannot be mapped at all, so it
// is an error.
function automatic integer check_conns;
    input integer inst;                          // ND_MOD_INST
    input integer tgt;                           // module-table index
    integer c, nnamed, npos, ntot;
    begin
        nnamed = 0; npos = 0;
        c = nd_a[inst];
        while (c != 0) begin
            if (nd_kind[c] == `ND_CONN) begin
                nnamed = nnamed + 1;
                if (find_port_named(tgt, nd_name[c]) == 0)
                    ignore = add_diag_id(`SEV_ERROR, nd_line[inst], nd_col[inst],
                                         "instantiated module has no port named",
                                         nd_name[c]);
            end else
                npos = npos + 1;
            c = nd_next[c];
        end
        ntot = nnamed + npos;
        if (nnamed > 0 && npos > 0)
            ignore = add_diag_id(`SEV_ERROR, nd_line[inst], nd_col[inst],
                                 "mixed named and positional connections to module",
                                 nd_name[inst]);
        else if (npos > 0 && npos > mt_nports[tgt])
            ignore = add_diag_id(`SEV_ERROR, nd_line[inst], nd_col[inst],
                                 "more port connections than ports on module",
                                 nd_name[inst]);
        else if (npos > 0 && npos < mt_nports[tgt])
            ignore = add_diag_id(`SEV_WARNING, nd_line[inst], nd_col[inst],
                                 "fewer port connections than ports on module",
                                 nd_name[inst]);
        check_conns = ntot;
    end
endfunction

// Walk one item chain, binding every ND_MOD_INST to its definition.  This is
// the pass that makes "verify that instantiated modules exist" true of the
// WHOLE source, not only of the part reachable from the top: an undefined
// module inside an unused module is still a design error, and the reference
// counts it produces are what pick_top consumes.
function automatic integer resolve_items;
    input integer items;
    integer it, tgt, n;
    begin
        n = 0; it = items;
        while (it != 0) begin
            if (nd_kind[it] == `ND_GENERATE)
                n = n + resolve_items(nd_a[it]);
            else if (nd_kind[it] == `ND_MOD_INST) begin
                tgt = find_module(nd_name[it]);
                if (tgt < 0)
                    ignore = add_diag_id(`SEV_ERROR, nd_line[it], nd_col[it],
                                         "instantiation of undefined module",
                                         nd_name[it]);
                else begin
                    mt_refs[tgt] = mt_refs[tgt] + 1;
                    ignore = check_conns(it, tgt);
                    n = n + 1;
                end
            end
            it = nd_next[it];
        end
        resolve_items = n;
    end
endfunction

function automatic integer resolve_instances;
    input integer u;                             // dummy (Verilog-2001 §10.3.1)
    integer i, n;
    begin
        n = 0;
        for (i = 0; i < n_mod; i = i + 1)
            n = n + resolve_items(nd_b[mt_node[i]]);
        resolve_instances = n;
    end
endfunction

// ===========================================================================
//  1. identify the top module
// ===========================================================================
// The top module is the one nothing instantiates.  After resolve_instances
// that is simply mt_refs == 0.  Three outcomes:
//   exactly one  -> that is the top
//   none         -> every module is instantiated, so the reference graph has a
//                   cycle and the design has no root at all
//   several      -> the source holds independent designs; take the first
//                   declared and say so, or let +top= choose explicitly
function automatic integer pick_top;
    input [`IDW-1:0] want;                       // 0 = decide automatically
    integer i, cand, nc, r;
    begin
        r = -1;
        if (n_mod == 0)
            ignore = add_diag(`SEV_ERROR, 16'd0, 16'd0,
                              "no modules to elaborate");
        else if (want != `IDW'd0) begin
            r = find_module(want);
            if (r < 0)
                ignore = add_diag_id(`SEV_ERROR, 16'd0, 16'd0,
                                     "requested top module is not defined", want);
        end else begin
            nc = 0; cand = -1;
            for (i = 0; i < n_mod; i = i + 1)
                if (mt_refs[i] == 0) begin
                    if (nc == 0) cand = i;
                    nc = nc + 1;
                end
            if (nc == 0)
                ignore = add_diag(`SEV_ERROR, 16'd0, 16'd0,
                    "no top module: every module is instantiated (cyclic hierarchy)");
            else begin
                if (nc > 1)
                    ignore = add_diag_id(`SEV_WARNING, 16'd0, 16'd0,
                        "several candidate top modules; elaborating", mt_name[cand]);
                r = cand;
            end
        end
        pick_top = r;
    end
endfunction

// ===========================================================================
//  4-5. instance objects and the parent-child hierarchy
// ===========================================================================
// Allocate one instance object.  -1 once the table is exhausted, and the
// exhaustion is announced once (same policy as the AST arena, §9.10).
function automatic integer new_inst;
    input [`IDW-1:0] nm;
    input integer    mi;
    input integer    parent;
    input integer    node;
    integer i;
    begin
        if (n_inst >= inst_cap) begin
            if (!elab_err)
                $display("FATAL: instance table exhausted (capacity=%0d)", inst_cap);
            elab_err = 1'b1;
            new_inst = -1;
        end else begin
            i = n_inst;
            in_name  [i] = nm;
            in_mod   [i] = mi;
            in_parent[i] = parent;
            in_node  [i] = node;
            in_child [i] = -1;
            in_sib   [i] = -1;
            in_depth [i] = (parent < 0) ? 0 : in_depth[parent] + 1;
            in_nconn [i] = (node == 0) ? 0 : chain_len(nd_a[node]);
            n_inst = n_inst + 1;
            new_inst = i;
        end
    end
endfunction

// append kid to parent's child list, keeping source order
function automatic integer add_child;
    input integer parent;
    input integer kid;
    integer t;
    begin
        if (in_child[parent] < 0) in_child[parent] = kid;
        else begin
            t = in_child[parent];
            while (in_sib[t] >= 0) t = in_sib[t];
            in_sib[t] = kid;
        end
        add_child = kid;
    end
endfunction

// Cycle guard: is module mi already somewhere on the path from instance i up
// to the root?  Verilog modules may not be recursively instantiated, and
// without this check elaboration would not terminate.
function automatic integer mod_on_path;
    input integer i;
    input integer mi;
    integer p, r;
    begin
        r = 0; p = i;
        while (p >= 0) begin
            if (in_mod[p] == mi) r = 1;
            p = in_parent[p];
        end
        mod_on_path = r;
    end
endfunction

// Elaborate the body of instance `self`: every module instantiation in its
// item chain becomes a child instance, and each child is elaborated in turn.
// Depth-first and in source order, so the instance table comes out in preorder
// and the dump is deterministic.  Undefined modules were already diagnosed by
// resolve_items; here they are simply not expanded.
function automatic integer elab_body;
    input integer self;
    input integer items;
    integer it, tgt, kid, n;
    begin
        n = 0; it = items;
        while (it != 0 && !elab_err) begin
            if (nd_kind[it] == `ND_GENERATE)
                n = n + elab_body(self, nd_a[it]);
            else if (nd_kind[it] == `ND_MOD_INST) begin
                tgt = find_module(nd_name[it]);
                if (tgt >= 0) begin
                    if (mod_on_path(self, tgt))
                        ignore = add_diag_id(`SEV_ERROR, nd_line[it], nd_col[it],
                                             "recursive instantiation of module",
                                             nd_name[it]);
                    else begin
                        kid = new_inst((nd_c[it] != 0) ? nd_name[nd_c[it]] : `IDW'd0,
                                       tgt, self, it);
                        if (kid >= 0) begin
                            ignore = add_child(self, kid);
                            n = n + 1 + elab_body(kid, nd_b[mt_node[tgt]]);
                        end
                    end
                end
            end
            it = nd_next[it];
        end
        elab_body = n;
    end
endfunction

// ---------------------------------------------------------------------------
// elaborate -- the whole of Task 5.1.  Returns the number of instances built;
// success is  (top_mod >= 0 && !elab_err && !had_error).
// ---------------------------------------------------------------------------
function automatic integer elaborate;
    input integer    root;                       // head of the ND_MODULE chain
    input [`IDW-1:0] want_top;                   // 0 = decide automatically
    begin
        elab_err = 1'b0;
        n_mod    = 0;
        n_inst   = 0;
        top_mod  = -1;
        top_inst = -1;

        ignore = build_module_table(root);        // definitions
        ignore = resolve_instances(0);            // bind + verify + count refs
        top_mod = pick_top(want_top);             // the root of the design

        if (top_mod >= 0 && !elab_err) begin
            top_inst = new_inst(mt_name[top_mod], top_mod, -1, 0);
            if (top_inst >= 0)
                ignore = elab_body(top_inst, nd_b[mt_node[top_mod]]);
        end
        elaborate = n_inst;
    end
endfunction

// ===========================================================================
//  hierarchy dump (§6 style: deterministic, no line/column)
// ===========================================================================

// full dotted path of an instance, root first
task automatic fput_path;
    input integer i;
    input integer fdo;
    begin
        if (in_parent[i] >= 0) begin
            fput_path(in_parent[i], fdo);
            $fwrite(fdo, ".");
        end
        fput_ident(in_name[i], fdo);
    end
endtask

task automatic dump_inst;
    input integer i;
    input integer ind;
    integer c;
    begin
        sp(ind);
        $fwrite(dfd, "(inst \""); fput_ident(in_name[i], dfd);
        $fwrite(dfd, "\" of \""); fput_ident(mt_name[in_mod[i]], dfd);
        $fwrite(dfd, "\" conns=%0d", in_nconn[i]);
        c = in_child[i];
        while (c >= 0) begin
            $fwrite(dfd, "\n");
            dump_inst(c, ind + 2);
            c = in_sib[c];
        end
        $fwrite(dfd, ")");
    end
endtask

// (design "top" (modules ...) (hierarchy ...) (paths ...))
task dump_hier;
    input integer fd;
    integer i;
    begin
        dfd = fd;
        $fwrite(dfd, "(design ");
        if (top_mod >= 0) begin
            $fwrite(dfd, "\""); fput_ident(mt_name[top_mod], dfd); $fwrite(dfd, "\"");
        end else
            $fwrite(dfd, "<none>");

        $fwrite(dfd, "\n"); sp(2); $fwrite(dfd, "(modules");
        if (n_mod == 0) $fwrite(dfd, ")");
        else begin
            for (i = 0; i < n_mod; i = i + 1) begin
                $fwrite(dfd, "\n"); sp(4);
                $fwrite(dfd, "(ND_MODULE \""); fput_ident(mt_name[i], dfd);
                $fwrite(dfd, "\" ports=%0d params=%0d insts=%0d gates=%0d refs=%0d)",
                        mt_nports[i], mt_nparam[i], mt_ninst[i],
                        mt_ngate[i],  mt_refs[i]);
            end
            $fwrite(dfd, ")");
        end

        $fwrite(dfd, "\n"); sp(2); $fwrite(dfd, "(hierarchy");
        if (top_inst < 0) $fwrite(dfd, ")");
        else begin
            $fwrite(dfd, "\n"); dump_inst(top_inst, 4); $fwrite(dfd, ")");
        end

        $fwrite(dfd, "\n"); sp(2); $fwrite(dfd, "(paths");
        if (n_inst == 0) $fwrite(dfd, ")");
        else begin
            for (i = 0; i < n_inst; i = i + 1) begin
                $fwrite(dfd, "\n"); sp(4);
                $fwrite(dfd, "\""); fput_path(i, dfd); $fwrite(dfd, "\"");
            end
            $fwrite(dfd, ")");
        end
        $fwrite(dfd, ")\n");
    end
endtask
