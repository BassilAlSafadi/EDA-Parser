//============================================================================
// vsim_top.v  --  standalone tool: plusarg handling and orchestration
//----------------------------------------------------------------------------
// The host tool is a program that runs INSIDE a simulator (decision 0.A): it
// uses $fopen/$fgetc/$fdisplay, unbounded loops and recursive `automatic`
// subprograms -- all simulation-only, non-synthesizable, by design.  This
// module wires the phases together for Phase 1: load -> lex -> parse -> dump.
//
// Invocation (see run/ scripts):
//   +src=<file.v>                 target file to parse             (required)
//   +dump_tokens [+tokout=<f>]    write the token stream (§8)
//   +dump_ast    [+astout=<f>]    write the AST S-expression (§6)
//============================================================================
`include "vsim_defs.vh"

module vsim_top;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"
    `include "vsim_parser.v"
    `include "vsim_dump.v"

    localparam [31:0] STDOUT = 32'h8000_0001;   // multichannel descriptor 1

    reg [8*128-1:0] filename, outname;
    integer         root, fd, cap;

    // count modules in the parsed chain (for the summary line)
    function automatic integer count_modules;
        input integer h;
        integer c, m;
        begin
            c = 0; m = h;
            while (m != 0) begin c = c + 1; m = nd_next[m]; end
            count_modules = c;
        end
    endfunction

    initial begin
        if (!$value$plusargs("src=%s", filename)) begin
            $display("usage: +src=<file.v> [+dump_tokens] [+dump_ast] [+tokout=f] [+astout=f]");
            $finish;
        end
        g_fname = filename;

        load_src(filename);                     // Phase 1a: read into src arena
        // optional artificial cap to demonstrate the overflow path (§9.10)
        if ($value$plusargs("nodecap=%d", cap)) node_cap = cap;
        lex;                                    // Phase 1b: scan tokens
        root = parse_source(0);                 // Phase 1c: build the AST

        if ($test$plusargs("dump_tokens")) begin
            if ($value$plusargs("tokout=%s", outname)) begin
                fd = $fopen(outname, "w"); dump_tokens(fd); $fclose(fd);
            end else dump_tokens(STDOUT);
        end

        if ($test$plusargs("dump_ast")) begin
            if ($value$plusargs("astout=%s", outname)) begin
                fd = $fopen(outname, "w"); dump_ast(root, fd); $fclose(fd);
            end else dump_ast(root, STDOUT);
        end

        print_diags;

        if (had_error || arena_err)
            $display("PARSE FAILED: %0d diagnostic(s)", n_diag);
        else
            $display("PARSE OK: %0d module(s), %0d tokens, %0d nodes",
                     count_modules(root), n_tok, n_node - 1);

        $finish;
    end

endmodule
