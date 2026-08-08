//============================================================================
// tb_sig.v  --  structural assertions for Phase 2, Task 5.2 (signal & port
// resolution)
//----------------------------------------------------------------------------
// Shares the real lexer, parser, elaborator and signal resolver, and checks
// the sg_*/pm_*/cn_* tables directly -- same technique tb_elab.v uses on the
// instance tables.
//
// Covered:
//   1  ports become signal-table rows, in header order, at sg_* index 0..
//   2  locally declared wires/regs are added after the ports
//   3  a 1-bit signal (no range) resolves to width 1
//   4  a ranged port whose range references a parameter resolves using that
//      module's own default parameter value
//   5  a named parameter override changes the resolved width
//   6  a positional parameter override changes the resolved width
//   7  a named connection flattens to the right net in the parent's scope
//   8  a positional connection flattens in port order
//   9  a connection to an undefined signal is a positioned error
//  10  an unconnected (short positional list) port resolves to cn_net == -1,
//      not a crash
//  11  duplicate signal declaration in one scope is an error
//============================================================================
`include "vsim_defs.vh"
module tb_sig;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"
    `include "vsim_parser.v"
    `include "vsim_dump.v"
    `include "vsim_elab.v"
    `include "vsim_sig.v"

    integer fails, root, n, nc, i;

    task ck; input cond; input [8*60-1:0] msg;
        begin
            if (!cond) begin fails = fails + 1; $display("FAIL: %0s", msg); end
            else $display("ok   %0s", msg);
        end
    endtask

    // parse + elaborate + resolve a snippet in one shot
    task run_sig; input [8*512-1:0] s; input [`IDW-1:0] want;
        begin
            load_strz(s);
            lex;
            root = parse_source(0);
            inst_cap = `MAX_INST;
            n  = elaborate(root, want);
            nc = resolve_signals(0);
        end
    endtask

    initial begin
        fails   = 0;
        g_fname = "tb_sig";

        //---------------------------------------------- 1-3: basic scope build
        run_sig("module m(input a,input b,output y); wire t; assign y = t; endmodule module t(input a,output y); m m0(a,a,y); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "basic snippet resolves cleanly");
        // instance 0 = t (root), instance 1 = m0
        ck(in_nsig[1] == 4,                    "m0's scope holds 3 ports + 1 local wire");
        ck(sg_name[in_sig0[1]+0] == "a" && sg_dir[in_sig0[1]+0] == `DIR_INPUT,
                                               "  port 0 is 'a', input");
        ck(sg_name[in_sig0[1]+1] == "b" && sg_dir[in_sig0[1]+1] == `DIR_INPUT,
                                               "  port 1 is 'b', input");
        ck(sg_name[in_sig0[1]+2] == "y" && sg_dir[in_sig0[1]+2] == `DIR_OUTPUT,
                                               "  port 2 is 'y', output");
        ck(sg_name[in_sig0[1]+3] == "t" && !sg_isport[in_sig0[1]+3],
                                               "  local wire 't' follows the ports");
        ck(sg_width[in_sig0[1]+0] == 1,        "unranged port resolves to width 1");

        //---------------------------------------- 4: ranged port, own default
        run_sig("module m #(parameter WIDTH=8)(input clk,output [WIDTH-1:0] q); endmodule module t; m m0(); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "ranged-port snippet resolves cleanly");
        ck(sg_width[in_sig0[1]+1] == 8,        "q resolves to WIDTH (default 8) bits");

        //--------------------------------------------- 5: named param override
        run_sig("module m #(parameter WIDTH=8)(input clk,output [WIDTH-1:0] q); endmodule module t; m #(.WIDTH(4)) m0(); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "named override snippet resolves cleanly");
        ck(sg_width[in_sig0[1]+1] == 4,        "named #(.WIDTH(4)) overrides the default");

        //---------------------------------------- 6: positional param override
        run_sig("module m #(parameter WIDTH=8)(input clk,output [WIDTH-1:0] q); endmodule module t; m #(16) m0(); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "positional override snippet resolves cleanly");
        ck(sg_width[in_sig0[1]+1] == 16,       "positional #(16) overrides the default");

        //--------------------------------------------------- 7: named connection
        run_sig("module h(input a,output y); endmodule module t(input p,output q); wire w; h u0(.a(p), .y(w)); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "named-connection snippet resolves cleanly");
        ck(nc == 2,                            "two connections flattened");
        ck(cn_port[0] == in_sig0[1]+0 && cn_net[0] == in_sig0[0]+0,
                                               "u0.a binds to t's port 'p'");
        ck(cn_port[1] == in_sig0[1]+1 && cn_net[1] == in_sig0[0]+2,
                                               "u0.y binds to t's local wire 'w'");

        //---------------------------------------------- 8: positional connection
        run_sig("module h(input a,output y); endmodule module t(input p,output q); h u0(p,q); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "positional-connection snippet resolves cleanly");
        ck(cn_port[0] == in_sig0[1]+0 && cn_net[0] == in_sig0[0]+0,
                                               "position 0 binds to port 0 ('a' <- 'p')");
        ck(cn_port[1] == in_sig0[1]+1 && cn_net[1] == in_sig0[0]+1,
                                               "position 1 binds to port 1 ('y' <- 'q')");

        //----------------------------------------- 9: undefined signal in conn
        run_sig("module h(input a,output y); endmodule module t(input p,output q); h u0(.a(p), .y(nope)); endmodule", `IDW'd0);
        ck(had_error,                          "connection to an undefined signal is an error");
        ck(diag_id[n_diag-1] == "nope",        "  the diagnostic names 'nope'");

        //--------------------------------------- 10: unconnected trailing port
        run_sig("module h(input a,input b,output y); endmodule module t(input p); h u0(p); endmodule", `IDW'd0);
        // Task 5.1 warns (fewer connections than ports) but does not error.
        ck(!had_error,                         "short positional list is a warning, not an error");
        ck(cn_net[1] == -1,                    "the unconnected port resolves to cn_net == -1");

        //----------------------------------------------- 11: duplicate signal
        run_sig("module t(input a); wire a; endmodule", `IDW'd0);
        ck(had_error,                          "redeclaring a port name as a wire is an error");

        //---------------------------- 12: two instances, independent overrides
        run_sig("module m #(parameter WIDTH=8)(output [WIDTH-1:0] q); endmodule module t; m #(.WIDTH(4)) m0(); m #(.WIDTH(16)) m1(); endmodule", `IDW'd0);
        ck(n_diag == 0,                        "two-instance override snippet resolves cleanly");
        // in_child[0] is m0 (first instantiated), in_sib[in_child[0]] is m1
        ck(sg_width[in_sig0[in_child[0]]] == 4,
                                               "m0's #(.WIDTH(4)) resolves q to 4 bits");
        ck(sg_width[in_sig0[in_sib[in_child[0]]]] == 16,
                                               "m1's #(.WIDTH(16)) resolves q to 16 bits, independently of m0");

        if (fails == 0) $display("tb_sig: PASS");
        else            $display("tb_sig: FAIL (%0d)", fails);
        $finish;
    end

endmodule