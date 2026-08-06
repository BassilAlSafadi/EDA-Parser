//============================================================================
// tb_elab.v  --  structural assertions for Phase 2, Task 5.1 (elaboration)
//----------------------------------------------------------------------------
// Shares the real lexer, parser and elaborator (via `include) and checks the
// module and instance tables directly, independently of the text dump -- the
// same technique tb_parser.v uses on the AST arena.
//
// Covered:
//   1  the module table holds every definition, in declaration order
//   2  the top module is the one nothing instantiates
//   3  instances are created for every instantiation, at every level
//   4  the parent-child tree is built, in source order, with correct depths
//   5  the same module instantiated twice yields two distinct instances
//   6  +top= overrides the deduced top and elaborates only that subtree
//   7  an undefined instantiated module is a positioned error
//   8  a cycle in the instantiation graph is an error, and terminates
//   9  several candidate tops -> warning, first declared is elaborated
//  10  a named connection to a non-existent port is an error
//  11  more connections than ports is an error
//  12  exhausting the instance table is a loud error, not corruption
//============================================================================
`include "vsim_defs.vh"
module tb_elab;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"
    `include "vsim_parser.v"
    `include "vsim_dump.v"
    `include "vsim_elab.v"

    integer fails, root, n, mi;

    task ck; input cond; input [8*60-1:0] msg;
        begin
            if (!cond) begin fails = fails + 1; $display("FAIL: %0s", msg); end
            else $display("ok   %0s", msg);
        end
    endtask

    // parse a snippet and elaborate it; returns nothing, leaves the tables set
    task run_elab; input [8*256-1:0] s; input [`IDW-1:0] want;
        begin
            load_strz(s);
            lex;
            root = parse_source(0);
            inst_cap = `MAX_INST;
            n = elaborate(root, want);
        end
    endtask

    initial begin
        fails    = 0;
        g_fname  = "tb_elab";

        //------------------------------------------------ 1-5: the happy path
        //   t  ->  m m0  ->  h h1, h h2
        run_elab("module h(input a,output y); endmodule module m(input a,output y); h h1(a,y); h h2(a,y); endmodule module t(input a,output y); m m0(a,y); endmodule", `IDW'd0);

        ck(n_diag == 0,                 "hierarchy snippet elaborates cleanly");
        ck(n_mod  == 3,                 "module table holds all 3 definitions");
        ck(mt_name[0] == "h" && mt_name[1] == "m" && mt_name[2] == "t",
                                        "  in declaration order");
        ck(mt_refs[0] == 2,             "h is referenced twice");
        ck(mt_refs[1] == 1,             "m is referenced once");
        ck(mt_refs[2] == 0,             "t is referenced by nobody");
        ck(top_mod == 2,                "top module is t (refs == 0)");
        ck(top_inst == 0 && in_parent[0] == -1, "root instance is t, no parent");
        ck(n_inst == 4,                 "4 instances: t, m0, h1, h2");
        ck(in_child[0] == 1 && in_sib[1] == -1, "t has exactly one child, m0");
        ck(in_name[1] == "m0" && in_mod[1] == 1, "  child 1 is m0, of module m");
        ck(in_child[1] == 2 && in_sib[2] == 3 && in_sib[3] == -1,
                                        "m0 has two children, in source order");
        ck(in_name[2] == "h1" && in_name[3] == "h2",
                                        "  they are h1 then h2");
        ck(in_mod[2] == 0 && in_mod[3] == 0,
                                        "  both are instances of the same module h");
        ck(in_parent[2] == 1 && in_parent[3] == 1, "  both point back at m0");
        ck(in_depth[0] == 0 && in_depth[1] == 1 && in_depth[2] == 2,
                                        "depths are 0 / 1 / 2 down the tree");
        ck(in_nconn[1] == 2,            "m0 records its 2 port connections");

        //------------------------------------------------ 6: explicit +top=
        run_elab("module h(input a,output y); endmodule module m(input a,output y); h h1(a,y); h h2(a,y); endmodule module t(input a,output y); m m0(a,y); endmodule", "m");
        ck(top_mod == 1 && n_inst == 3, "+top=m elaborates m and its subtree only");

        //------------------------------------------------ 7: undefined module
        run_elab("module t(input a,output y); nope u0(a,y); endmodule", `IDW'd0);
        ck(had_error && n_diag == 1,    "undefined instantiated module is an error");
        ck(diag_id[0] == "nope",        "  the diagnostic names 'nope'");
        ck(diag_line[0] == 16'd1,       "  positioned on line 1");
        ck(n_inst == 1,                 "  the bad instance is not elaborated");

        //------------------------------------------------ 8: cyclic hierarchy
        run_elab("module p(input a,output y); q u(a,y); endmodule module q(input a,output y); p u(a,y); endmodule module t(input a,output y); p u(a,y); endmodule", `IDW'd0);
        ck(had_error,                   "recursive instantiation is an error");
        ck(top_mod == 2,                "  top is still t");
        ck(n_inst == 3,                 "  recursion stops at the repeat (t,p,q)");

        //------------------------------------------ 9: several candidate tops
        run_elab("module aa(input a,output y); endmodule module bb(input a,output y); endmodule", `IDW'd0);
        ck(!had_error && n_diag == 1,   "two roots is a warning, not an error");
        ck(diag_sev[0] == `SEV_WARNING, "  severity is warning");
        ck(top_mod == 0 && n_inst == 1, "  the first declared module is elaborated");

        //--------------------------------------------- 10: unknown port name
        run_elab("module h(input a,input b,output y); endmodule module t(input a,output y); h u0(.a(a), .zz(a), .y(y)); endmodule", `IDW'd0);
        ck(had_error && diag_id[0] == "zz",
                                        "named connection to a missing port errors");

        //------------------------------------------ 11: too many connections
        run_elab("module h(input a,input b,output y); endmodule module t(input a,output y); h u0(a,a,y,y,y); endmodule", `IDW'd0);
        ck(had_error,                   "5 positional connections to a 3-port module errors");

        //------------------------------------- 12: instance table exhaustion
        load_strz("module h(input a,output y); endmodule module m(input a,output y); h h1(a,y); h h2(a,y); endmodule module t(input a,output y); m m0(a,y); endmodule");
        lex; root = parse_source(0);
        inst_cap = 2;                   // room for t and m0 only
        n = elaborate(root, `IDW'd0);
        ck(elab_err,                    "exhausting the instance table sets elab_err");
        ck(n_inst == 2,                 "  and stops allocating at the cap");

        if (fails == 0) $display("tb_elab: PASS");
        else            $display("tb_elab: FAIL (%0d)", fails);
        $finish;
    end

endmodule
