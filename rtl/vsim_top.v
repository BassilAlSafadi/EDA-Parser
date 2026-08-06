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
//   +elab                         run Phase 2 Task 5.1 (elaboration)
//   +dump_hier   [+hierout=<f>]   write the elaborated hierarchy (implies +elab)
//   +top=<name>                   force the top module instead of deducing it
//   +instcap=<N>                  cap the instance table (demonstrate overflow)
//============================================================================
`include "vsim_defs.vh"

module vsim_top;

    `include "vsim_arena.v"
    `include "vsim_diag.v"
    `include "vsim_lexer.v"
    `include "vsim_parser.v"
    `include "vsim_dump.v"
    `include "vsim_elab.v"

    localparam [31:0] STDOUT = 32'h8000_0001;   // multichannel descriptor 1

    reg [8*128-1:0] filename, outname, topname;
    reg [`IDW-1:0]  want_top;
    reg             do_elab, parse_bad;
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
            $display("usage: +src=<file.v> [+dump_tokens] [+dump_ast] [+dump_hier]");
            $display("       [+tokout=f] [+astout=f] [+hierout=f] [+elab] [+top=name]");
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

        // ---------------------------------------------- Phase 2, Task 5.1
        // Elaboration runs only when asked for, so every Phase-1 invocation
        // (and its golden output) is exactly as before.  A failed parse means
        // the AST is incomplete, so there is nothing sound to elaborate.
        // Latch the Phase-1 verdict BEFORE elaborating: elaboration diagnostics
        // also set had_error, and "PARSE OK" must keep meaning "the parse was
        // clean" rather than "the whole run was clean".
        parse_bad = had_error || arena_err;
        do_elab   = $test$plusargs("elab") || $test$plusargs("dump_hier");
        if (do_elab) begin
            if (parse_bad)
                $display("ELABORATE SKIPPED: parse failed");
            else begin
                want_top = `IDW'd0;
                if ($value$plusargs("top=%s", topname))
                    want_top = topname[`IDW-1:0];
                inst_cap = `MAX_INST;
                if ($value$plusargs("instcap=%d", cap)) inst_cap = cap;
                ignore = elaborate(root, want_top);
                if ($test$plusargs("dump_hier")) begin
                    if ($value$plusargs("hierout=%s", outname)) begin
                        fd = $fopen(outname, "w"); dump_hier(fd); $fclose(fd);
                    end else dump_hier(STDOUT);
                end
            end
        end

        print_diags;

        if (parse_bad)
            $display("PARSE FAILED: %0d diagnostic(s)", n_diag);
        else begin
            $display("PARSE OK: %0d module(s), %0d tokens, %0d nodes",
                     count_modules(root), n_tok, n_node - 1);
            if (do_elab) begin
                if (top_mod < 0 || elab_err || had_error)
                    $display("ELABORATE FAILED: %0d diagnostic(s)", n_diag);
                else begin
                    $fwrite(STDOUT, "ELABORATE OK: top='");
                    fput_ident(mt_name[top_mod], STDOUT);
                    $fwrite(STDOUT, "', %0d module(s), %0d instance(s)\n",
                            n_mod, n_inst);
                end
            end
        end

        $finish;
    end

endmodule
