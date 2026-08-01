//============================================================================
// tb_lexer.v  --  scanner assertions for the §9 acceptance criteria
//----------------------------------------------------------------------------
// Includes the same fragments as the tool so it shares the real scanner, then
// lexes small in-memory snippets and checks:
//   §9.5  "==="  is ONE token; "= = ="  is THREE (maximal munch).
//   §9.6  "a / b" is three tokens; "a // b" is one token + discarded comment.
//   §9.8  4'b1x0z stores width-4 reading back 1x0z; 8'hFF extends per §3.5.
//============================================================================
`include "vsim_defs.vh"
module tb_lexer;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"

    integer fails;

    // lex a snippet, then require the token count (incl. EOF) to equal `want`.
    task check_count;
        input [8*256-1:0] s; input integer len; input integer want;
        input [8*40-1:0]  label;
        begin
            load_str(s, len); lex;
            if (n_tok !== want) begin
                fails = fails + 1;
                $display("FAIL %0s: n_tok=%0d want=%0d", label, n_tok, want);
            end else
                $display("ok   %0s: %0d tokens", label, n_tok);
        end
    endtask

    initial begin
        fails = 0;

        // §9.5 maximal munch of the '=' family
        check_count("===",   3, 2, "=== is one token");        // CASEEQ, EOF
        if (tok_kind[0] !== `T_CASE_EQ) begin
            fails = fails + 1; $display("FAIL: === did not scan as T_CASE_EQ");
        end
        check_count("= = =", 5, 4, "= = = is three tokens");   // 3x ASSIGN, EOF
        if (tok_kind[0] !== `T_ASSIGN || tok_kind[1] !== `T_ASSIGN ||
            tok_kind[2] !== `T_ASSIGN) begin
            fails = fails + 1; $display("FAIL: spaced = = = not three ASSIGN");
        end

        // §9.6 divide vs. comment
        check_count("a / b",  5, 4, "a / b is three tokens");  // IDENT SLASH IDENT EOF
        if (tok_kind[1] !== `T_SLASH) begin
            fails = fails + 1; $display("FAIL: a / b middle token not SLASH");
        end
        check_count("a // b", 6, 2, "a // b is one token");    // IDENT EOF

        // §9.8 four-state literal storage
        load_str("4'b1x0z", 7); lex;
        if (tok_kind[0] !== `T_NUMBER || tok_width[0] !== 16'd4 ||
            tok_value[0][3:0] !== 4'b1x0z) begin
            fails = fails + 1;
            $display("FAIL: 4'b1x0z stored as w=%0d v=%b", tok_width[0], tok_value[0][3:0]);
        end else
            $display("ok   4'b1x0z: w=4 v=%b", tok_value[0][3:0]);

        load_str("8'hFF", 5); lex;
        if (tok_kind[0] !== `T_NUMBER || tok_width[0] !== 16'd8 ||
            tok_value[0][7:0] !== 8'hFF) begin
            fails = fails + 1;
            $display("FAIL: 8'hFF stored as w=%0d v=%b", tok_width[0], tok_value[0][7:0]);
        end else
            $display("ok   8'hFF: w=8 v=%b", tok_value[0][7:0]);

        // 8'bx must sign-... x-extend to all x across 8 bits (§3.5)
        load_str("8'bx", 4); lex;
        if (tok_value[0][7:0] !== 8'bxxxxxxxx) begin
            fails = fails + 1;
            $display("FAIL: 8'bx did not x-extend: v=%b", tok_value[0][7:0]);
        end else
            $display("ok   8'bx x-extends: v=%b", tok_value[0][7:0]);

        if (fails == 0) $display("tb_lexer: PASS");
        else            $display("tb_lexer: FAIL (%0d)", fails);
        $finish;
    end

endmodule
