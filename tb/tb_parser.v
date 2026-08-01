//============================================================================
// tb_parser.v  --  structural AST assertions for the §9 acceptance criteria
//----------------------------------------------------------------------------
// Shares the real parser (via `include) and checks the AST shape directly on
// the arena, independent of the text dump:
//   §9.3  COUT = (A&B)|(A&CIN)|(B&CIN) builds exactly the §5.2 tree
//         (| left-associative on top, every & one level deeper).
//   §9.7  if(a) if(b) x=1; else y=1;  binds else to the inner if.
// It also confirms both snippets parse with zero diagnostics (§9.1).
//============================================================================
`include "vsim_defs.vh"
module tb_parser;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"
    `include "vsim_parser.v"

    integer fails, root, m, casgn, top, L, R, LL, LR;
    integer alwys, outer, inner;

    task ck; input cond; input [8*60-1:0] msg;
        begin
            if (!cond) begin fails = fails + 1; $display("FAIL: %0s", msg); end
            else $display("ok   %0s", msg);
        end
    endtask

    // true iff h is an ND_IDENT named nm
    function is_id; input integer h; input [`IDW-1:0] nm;
        is_id = (h != 0 && nd_kind[h] == `ND_IDENT && nd_name[h] == nm);
    endfunction

    // true iff h is an ND_BINARY with the given operator
    function is_bin; input integer h; input [7:0] op;
        is_bin = (h != 0 && nd_kind[h] == `ND_BINARY && nd_op[h] == op);
    endfunction

    initial begin
        fails = 0;

        //---------------------------------------------------- §9.3 COUT tree
        load_strz("module fa(A,B,CIN,COUT); input A,B,CIN; output COUT; assign COUT=(A&B)|(A&CIN)|(B&CIN); endmodule");
        lex; root = parse_source(0);
        ck(n_diag == 0, "COUT parses with zero diagnostics");
        m     = root;
        casgn = nd_b[m];                          // first (only) item
        ck(nd_kind[casgn] == `ND_CONT_ASSIGN, "top item is a continuous assign");
        top = nd_b[casgn];                        // RHS expression
        ck(is_bin(top, `T_PIPE),        "root is (| ...)");
        L = nd_a[top];  R = nd_b[top];
        ck(is_bin(L, `T_PIPE),          "left child is (| ...) -- left-assoc");
        ck(is_bin(R, `T_AMP),           "right child is (& B CIN)");
        ck(is_id(nd_a[R], "B") && is_id(nd_b[R], "CIN"), "  right & has B,CIN");
        LL = nd_a[L];  LR = nd_b[L];
        ck(is_bin(LL, `T_AMP),          "L.left is (& A B)");
        ck(is_id(nd_a[LL], "A") && is_id(nd_b[LL], "B"),   "  it has A,B");
        ck(is_bin(LR, `T_AMP),          "L.right is (& A CIN)");
        ck(is_id(nd_a[LR], "A") && is_id(nd_b[LR], "CIN"), "  it has A,CIN");

        //------------------------------------------------ §9.7 dangling else
        load_strz("module m(input a,input b,output reg x,output reg y); always @(*) if(a) if(b) x=1; else y=1; endmodule");
        lex; root = parse_source(0);
        ck(n_diag == 0, "dangling-else snippet parses with zero diagnostics");
        m     = root;
        alwys = nd_b[m];                          // the always block
        ck(nd_kind[alwys] == `ND_ALWAYS, "item is an always block");
        outer = nd_b[alwys];                      // outer if
        ck(nd_kind[outer] == `ND_IF, "outer statement is an if");
        ck(nd_c[outer] == 0,          "outer if has NO else");
        inner = nd_b[outer];                      // then-branch = inner if
        ck(nd_kind[inner] == `ND_IF, "inner statement is an if");
        ck(nd_c[inner] != 0,          "inner if HAS the else (binds inner)");

        if (fails == 0) $display("tb_parser: PASS");
        else            $display("tb_parser: FAIL (%0d)", fails);
        $finish;
    end

endmodule
