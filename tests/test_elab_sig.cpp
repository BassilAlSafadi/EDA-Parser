
#include "vsim.hpp"

#include <cstdio>
#include <iostream>

using namespace vsim;

static int fails = 0;

static void ck(bool cond, const char* msg) {
    if (!cond) {
        fails += 1;
        std::printf("FAIL: %s\n", msg);
    } else {
        std::printf("ok   %s\n", msg);
    }
}

// parse a snippet and elaborate it; leaves v's tables set
static void run_elab(Vsim& v, const std::string& src, const std::string& want) {
    v.load_str(src);
    v.lex();
    int root = v.parse_source();
    v.inst_cap = MAX_INST;
    v.elaborate(root, want);
}

static void run_sig(Vsim& v, const std::string& src, const std::string& want, int& nc) {
    v.load_str(src);
    v.lex();
    int root = v.parse_source();
    v.inst_cap = MAX_INST;
    v.elaborate(root, want);
    nc = v.resolve_signals();
}

int main() {
    //====================================================== tb_elab.v ports
    {
        Vsim v;
        v.g_fname = "test_elab_sig";

        // 1-5: happy path.  t -> m m0 -> h h1, h h2
        run_elab(v,
            "module h(input a,output y); endmodule "
            "module m(input a,output y); h h1(a,y); h h2(a,y); endmodule "
            "module t(input a,output y); m m0(a,y); endmodule",
            "");
        ck(v.n_diag == 0,                 "hierarchy snippet elaborates cleanly");
        ck(v.n_mod  == 3,                 "module table holds all 3 definitions");
        ck(v.mt_name[0] == "h" && v.mt_name[1] == "m" && v.mt_name[2] == "t",
                                          "  in declaration order");
        ck(v.mt_refs[0] == 2,             "h is referenced twice");
        ck(v.mt_refs[1] == 1,             "m is referenced once");
        ck(v.mt_refs[2] == 0,             "t is referenced by nobody");
        ck(v.top_mod == 2,                "top module is t (refs == 0)");
        ck(v.top_inst == 0 && v.in_parent[0] == -1, "root instance is t, no parent");
        ck(v.n_inst == 4,                 "4 instances: t, m0, h1, h2");
        ck(v.in_child[0] == 1 && v.in_sib[1] == -1, "t has exactly one child, m0");
        ck(v.in_name[1] == "m0" && v.in_mod[1] == 1, "  child 1 is m0, of module m");
        ck(v.in_child[1] == 2 && v.in_sib[2] == 3 && v.in_sib[3] == -1,
                                          "m0 has two children, in source order");
        ck(v.in_name[2] == "h1" && v.in_name[3] == "h2",
                                          "  they are h1 then h2");
        ck(v.in_mod[2] == 0 && v.in_mod[3] == 0,
                                          "  both are instances of the same module h");
        ck(v.in_parent[2] == 1 && v.in_parent[3] == 1, "  both point back at m0");
        ck(v.in_depth[0] == 0 && v.in_depth[1] == 1 && v.in_depth[2] == 2,
                                          "depths are 0 / 1 / 2 down the tree");
        ck(v.in_nconn[1] == 2,            "m0 records its 2 port connections");

        // 6: explicit top=
        run_elab(v,
            "module h(input a,output y); endmodule "
            "module m(input a,output y); h h1(a,y); h h2(a,y); endmodule "
            "module t(input a,output y); m m0(a,y); endmodule",
            "m");
        ck(v.top_mod == 1 && v.n_inst == 3, "top=m elaborates m and its subtree only");

        // 7: undefined module
        run_elab(v, "module t(input a,output y); nope u0(a,y); endmodule", "");
        ck(v.had_error && v.n_diag == 1, "undefined instantiated module is an error");
        ck(v.diag_msg[0].find("'nope'") != std::string::npos, "  the diagnostic names 'nope'");
        ck(v.diag_line[0] == 1,          "  positioned on line 1");
        ck(v.n_inst == 1,                "  the bad instance is not elaborated");

        // 8: cyclic hierarchy
        run_elab(v,
            "module p(input a,output y); q u(a,y); endmodule "
            "module q(input a,output y); p u(a,y); endmodule "
            "module t(input a,output y); p u(a,y); endmodule",
            "");
        ck(v.had_error,   "recursive instantiation is an error");
        ck(v.top_mod == 2, "  top is still t");
        ck(v.n_inst == 3,  "  recursion stops at the repeat (t,p,q)");

        // 9: several candidate tops
        run_elab(v, "module aa(input a,output y); endmodule module bb(input a,output y); endmodule", "");
        ck(!v.had_error && v.n_diag == 1, "two roots is a warning, not an error");
        ck(v.diag_sev[0] == SEV_WARNING,  "  severity is warning");
        ck(v.top_mod == 0 && v.n_inst == 1, "  the first declared module is elaborated");

        // 10: unknown port name
        run_elab(v,
            "module h(input a,input b,output y); endmodule "
            "module t(input a,output y); h u0(.a(a), .zz(a), .y(y)); endmodule",
            "");
        ck(v.had_error && v.diag_msg[0].find("'zz'") != std::string::npos,
                                          "named connection to a missing port errors");

        // 11: too many connections
        run_elab(v,
            "module h(input a,input b,output y); endmodule "
            "module t(input a,output y); h u0(a,a,y,y,y); endmodule",
            "");
        ck(v.had_error, "5 positional connections to a 3-port module errors");

        // 12: instance table exhaustion
        v.load_str(
            "module h(input a,output y); endmodule "
            "module m(input a,output y); h h1(a,y); h h2(a,y); endmodule "
            "module t(input a,output y); m m0(a,y); endmodule");
        v.lex();
        int root = v.parse_source();
        v.inst_cap = 2;   // room for t and m0 only
        v.elaborate(root, "");
        ck(v.elab_err, "exhausting the instance table sets elab_err");
        ck(v.n_inst == 2, "  and stops allocating at the cap");
    }

    //======================================================= tb_sig.v ports
    {
        Vsim v;
        v.g_fname = "test_elab_sig";
        int nc = 0;

        run_sig(v,
            "module m(input a,input b,output y); wire t; assign y = t; endmodule "
            "module t(input a,output y); m m0(a,a,y); endmodule",
            "", nc);
        ck(v.n_diag == 0, "basic snippet resolves cleanly");
        ck(v.in_nsig[1] == 4, "m0's scope holds 3 ports + 1 local wire");
        ck(v.sg_name[v.in_sig0[1]+0] == "a" && v.sg_dir[v.in_sig0[1]+0] == DIR_INPUT,
                                          "  port 0 is 'a', input");
        ck(v.sg_name[v.in_sig0[1]+3] == "t" && !v.sg_isport[v.in_sig0[1]+3],
                                          "  local wire 't' follows the ports");
        ck(v.sg_width[v.in_sig0[1]+0] == 1, "unranged port resolves to width 1");

        run_sig(v,
            "module m #(parameter WIDTH=8)(input clk,output [WIDTH-1:0] q); endmodule "
            "module t; m m0(); endmodule",
            "", nc);
        ck(v.n_diag == 0, "ranged-port snippet resolves cleanly");
        ck(v.sg_width[v.in_sig0[1]+1] == 8, "q resolves to WIDTH (default 8) bits");

        run_sig(v,
            "module m #(parameter WIDTH=8)(input clk,output [WIDTH-1:0] q); endmodule "
            "module t; m #(.WIDTH(4)) m0(); endmodule",
            "", nc);
        ck(v.n_diag == 0, "named override snippet resolves cleanly");
        ck(v.sg_width[v.in_sig0[1]+1] == 4, "named #(.WIDTH(4)) overrides the default");

        run_sig(v,
            "module h(input a,output y); endmodule "
            "module t(input p,output q); wire w; h u0(.a(p), .y(w)); endmodule",
            "", nc);
        ck(v.n_diag == 0, "named-connection snippet resolves cleanly");
        ck(nc == 2, "two connections flattened");

        // undefined signal reference (new fixture, top_undef_sig.v)
        run_sig(v,
            "module leaf(input a, output y); assign y = a; endmodule "
            "module top_undef_sig(input p, output q); leaf u0(.a(p), .y(ghost)); endmodule",
            "", nc);
        ck(v.had_error, "connection to an undefined signal is an error");
        ck(v.diag_msg[v.n_diag-1].find("'ghost'") != std::string::npos,
                                          "  the diagnostic names 'ghost'");

        // duplicate signal declaration (new fixture, top_dup_sig.v)
        run_sig(v,
            "module top_dup_sig(input a, output y); wire a; assign y = a; endmodule",
            "", nc);
        ck(v.had_error, "redeclaring a port name as a wire is an error");
    }

    if (fails == 0) std::printf("test_elab_sig: PASS\n");
    else            std::printf("test_elab_sig: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}