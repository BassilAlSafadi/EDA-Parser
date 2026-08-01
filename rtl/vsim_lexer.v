//============================================================================
// vsim_lexer.v  --  host scanner over the target byte arena   (§3)
//----------------------------------------------------------------------------
// Include fragment.  Loads the whole target file into `src` first, then scans
// in memory (avoids pushback, mitigates $fgetc cost -- risk table §12).  The
// scanner is a DFA family: identifiers/keywords (§3.3), whitespace & comments
// (§3.4), four-state numeric literals (§3.5), maximal-munch operators (§3.6).
//============================================================================

// -------------------------------------------------------------- scanner state
integer         lp;            // cursor into src
integer         lln, lcol;     // current line / column
integer         tln, tcol;     // start line/col of the token being built
reg  [`IDW-1:0] t_text;        // scratch: identifier/keyword text
reg  [`VALW-1:0]t_value;       // scratch: literal value (four-state)
reg  [15:0]     t_width;       // scratch: literal width
reg             t_signed;      // scratch: literal signedness

// ---------------------------------------------------------------------------
// load_src -- read the entire target file into the byte arena (§3.1).
// ---------------------------------------------------------------------------
task load_src;
    input [8*128-1:0] filename;
    integer fd, ch;
    begin
        // fresh run: reset every arena counter and flag (regs are x at t=0).
        arena_err = 1'b0;
        had_error = 1'b0;
        n_tok     = 0;
        n_node    = 1;           // handle 0 reserved as NULL (§2.5)
        n_diag    = 0;
        node_cap  = `MAX_NODES;
        src_len   = 0;
        fd = $fopen(filename, "r");
        if (fd == 0) begin
            $display("FATAL: cannot open %0s", filename);
            $finish;
        end
        ch = $fgetc(fd);
        while (ch != -1 && src_len < `MAX_SRC) begin
            src[src_len] = ch[7:0];
            src_len = src_len + 1;
            ch = $fgetc(fd);
        end
        if (ch != -1) begin
            ignore = add_diag(`SEV_ERROR, 16'd0, 16'd0,
                              "source exceeds MAX_SRC bytes");
            arena_err = 1'b1;
        end
        $fclose(fd);
    end
endtask

// ---------------------------------------------------------------------------
// load_str -- populate the source arena directly from a packed string (used by
// the testbenches for self-contained snippets).  A Verilog string literal is
// stored MSB-first, so byte k of the text is s[(len-1-k)*8 +: 8].
// ---------------------------------------------------------------------------
task load_str;
    input [8*256-1:0] s;
    input integer     len;
    integer k;
    begin
        arena_err = 1'b0; had_error = 1'b0;
        n_tok = 0; n_node = 1; n_diag = 0; node_cap = `MAX_NODES;
        for (k = 0; k < len; k = k + 1)
            src[k] = s[(len-1-k)*8 +: 8];
        src_len = len;
    end
endtask

// load_str, auto-detecting length: the first character is the highest nonzero
// byte of the right-aligned literal, so its index+1 is the length.
task load_strz;
    input [8*256-1:0] s;
    integer k, hi;
    begin
        hi = -1;
        for (k = 255; k >= 0; k = k - 1)
            if (hi < 0 && s[k*8 +: 8] != 8'd0) hi = k;
        load_str(s, (hi < 0) ? 0 : hi + 1);
    end
endtask

// current char (0 past EOF)
function automatic [7:0] peekc;
    input integer off;
    peekc = ((lp + off) < src_len) ? src[lp + off] : 8'd0;
endfunction

// consume one src byte, tracking line/column (CR left column-neutral so CRLF
// files column-count like LF files, §3.4).
task adv;
    begin
        if (src[lp] == 8'h0A)      begin lln = lln + 1; lcol = 1; end
        else if (src[lp] == 8'h0D) ;                 // no column change
        else                          lcol = lcol + 1;
        lp = lp + 1;
    end
endtask

// ---------------------------------------------------------------------------
// push_tok -- append the token currently described by the t_* scratch regs.
// Overflowing the token arena is a loud failure, not corruption (§9.10).
// ---------------------------------------------------------------------------
task push_tok;
    input [7:0] k;
    begin
        if (n_tok >= `MAX_TOK) begin
            if (!arena_err)
                $display("FATAL: token arena exhausted (MAX_TOK=%0d)", `MAX_TOK);
            arena_err = 1'b1;
        end else begin
            tok_kind  [n_tok] = k;
            tok_text  [n_tok] = t_text;
            tok_value [n_tok] = t_value;
            tok_width [n_tok] = t_width;
            tok_signed[n_tok] = t_signed;
            tok_line  [n_tok] = tln[15:0];
            tok_col   [n_tok] = tcol[15:0];
            n_tok = n_tok + 1;
        end
        t_text = 0; t_value = 0; t_width = 0; t_signed = 0;
    end
endtask

// ---------------------------------------------------------------------------
// kw_lookup -- reclassify a scanned identifier as a keyword or T_IDENT (§3.3).
// One wide `==` per entry; string literals zero-extend to IDW and thus align
// with the right-aligned identifier vector.  Keywords must NOT be scanner
// states -- they are a post-scan table lookup.
// ---------------------------------------------------------------------------
function automatic [7:0] kw_lookup;
    input [`IDW-1:0] s;
    case (s)
        pad("module"):      kw_lookup = `K_MODULE;
        pad("endmodule"):   kw_lookup = `K_ENDMODULE;
        pad("input"):       kw_lookup = `K_INPUT;
        pad("output"):      kw_lookup = `K_OUTPUT;
        pad("inout"):       kw_lookup = `K_INOUT;
        pad("wire"):        kw_lookup = `K_WIRE;
        pad("reg"):         kw_lookup = `K_REG;
        pad("parameter"):   kw_lookup = `K_PARAMETER;
        pad("always"):      kw_lookup = `K_ALWAYS;
        pad("initial"):     kw_lookup = `K_INITIAL;
        pad("assign"):      kw_lookup = `K_ASSIGN;
        pad("begin"):       kw_lookup = `K_BEGIN;
        pad("end"):         kw_lookup = `K_END;
        pad("if"):          kw_lookup = `K_IF;
        pad("else"):        kw_lookup = `K_ELSE;
        pad("case"):        kw_lookup = `K_CASE;
        pad("casex"):       kw_lookup = `K_CASEX;
        pad("casez"):       kw_lookup = `K_CASEZ;
        pad("endcase"):     kw_lookup = `K_ENDCASE;
        pad("default"):     kw_lookup = `K_DEFAULT;
        pad("for"):         kw_lookup = `K_FOR;
        pad("while"):       kw_lookup = `K_WHILE;
        pad("repeat"):      kw_lookup = `K_REPEAT;
        pad("posedge"):     kw_lookup = `K_POSEDGE;
        pad("negedge"):     kw_lookup = `K_NEGEDGE;
        pad("or"):          kw_lookup = `K_OR;
        pad("signed"):      kw_lookup = `K_SIGNED;
        pad("integer"):     kw_lookup = `K_INTEGER;
        pad("generate"):    kw_lookup = `K_GENERATE;
        pad("endgenerate"): kw_lookup = `K_ENDGENERATE;
        pad("and"):         kw_lookup = `K_AND;
        pad("not"):         kw_lookup = `K_NOT;
        pad("nand"):        kw_lookup = `K_NAND;
        pad("nor"):         kw_lookup = `K_NOR;
        pad("xor"):         kw_lookup = `K_XOR;
        pad("xnor"):        kw_lookup = `K_XNOR;
        pad("buf"):         kw_lookup = `K_BUF;
        default:            kw_lookup = `T_IDENT;
    endcase
endfunction

// valid value digit for a based literal (x/z/? always accepted, §3.5)
function automatic valid_base_digit;
    input [7:0] c;
    input [7:0] base;   // 'b' 'o' 'd' 'h' (lower-case)
    reg [7:0] lcc;
    begin
        lcc = lc(c);
        if (lcc == "x" || lcc == "z" || c == "?")
            valid_base_digit = 1'b1;
        else case (base)
            "b":     valid_base_digit = (c == "0" || c == "1");
            "o":     valid_base_digit = (c >= "0" && c <= "7");
            "d":     valid_base_digit = is_digit(c);
            "h":     valid_base_digit = is_hex(c);
            default: valid_base_digit = 1'b0;
        endcase
    end
endfunction

// ---------------------------------------------------------------------------
// skip_ws_comments -- consume whitespace, // line comments and /* */ block
// comments (§3.4).  `/` alone is left for the operator scanner (divide), the
// classic divide-vs-comment case resolved by looking at the following char.
// ---------------------------------------------------------------------------
task skip_ws_comments;
    reg     progressed, found;
    integer cs_line, cs_col;
    begin
        progressed = 1'b1;
        while (progressed) begin
            progressed = 1'b0;
            while (lp < src_len && is_space(src[lp])) begin adv; progressed = 1'b1; end
            if (lp + 1 < src_len && src[lp] == "/" && src[lp+1] == "/") begin
                while (lp < src_len && src[lp] != 8'h0A) adv;
                progressed = 1'b1;
            end else if (lp + 1 < src_len && src[lp] == "/" && src[lp+1] == "*") begin
                cs_line = lln; cs_col = lcol;
                adv; adv;                          // consume "/*"
                found = 1'b0;
                while (lp + 1 < src_len && !found) begin
                    if (src[lp] == "*" && src[lp+1] == "/") begin adv; adv; found = 1'b1; end
                    else adv;
                end
                if (!found) begin
                    while (lp < src_len) adv;       // swallow to EOF
                    ignore = add_diag(`SEV_ERROR, cs_line[15:0], cs_col[15:0],
                                      "unterminated block comment");
                end
                progressed = 1'b1;
            end
        end
    end
endtask

// ---------------------------------------------------------------------------
// scan_ident -- identifier/keyword (§3.3).  $ is deliberately excluded from
// the continuation set (decision 0.6) so malformed names are not silently
// admitted.
// ---------------------------------------------------------------------------
task scan_ident;
    integer s, len;
    reg [`IDW-1:0] idv;
    begin
        s = lp;
        while (lp < src_len && is_alnum(src[lp])) adv;
        len = lp - s;
        if (len > `MAX_IDENT)
            ignore = add_diag(`SEV_ERROR, tln[15:0], tcol[15:0],
                              "identifier exceeds MAX_IDENT chars");
        idv    = mk_ident(s, len);
        t_text = idv;
        push_tok(kw_lookup(idv));
    end
endtask

// ---------------------------------------------------------------------------
// scan_number -- four-state numeric literal (§3.5).  Handles unsized decimals,
// sized/based literals, x/z/? digits, and size extension/truncation.  Values
// are assembled directly into a native four-state reg -- no aval/bval planes.
// ---------------------------------------------------------------------------
task scan_number;
    reg [`VALW-1:0] decval, val;
    reg             haverun, is_based, is_signed, dec_special;
    integer         size, nw, W, i, bits;
    reg  [7:0]      base, d, fillbit, dch;
    reg             saw_digit;
    begin
        decval = 0; val = 0; haverun = 0; is_based = 0; is_signed = 0;
        dec_special = 0; size = 0; nw = 0; base = 0; fillbit = 1'b0;

        // ---- leading decimal run: either the size or an unsized decimal ----
        if (src[lp] != "'") begin
            while (lp < src_len && (is_digit(src[lp]) || src[lp] == "_")) begin
                if (src[lp] != "_") begin decval = decval*10 + (src[lp]-"0"); haverun = 1; end
                adv;
            end
        end

        if (lp < src_len && src[lp] == "'") begin
            // ------------------------------- based literal --------------------
            is_based = 1;
            size     = haverun ? decval : 0;
            adv;                                        // consume '
            if (lp < src_len && (src[lp] == "s" || src[lp] == "S")) begin
                is_signed = 1; adv;
            end
            base = (lp < src_len) ? lc(src[lp]) : "b";
            case (base) "b": bits = 1; "o": bits = 3; "h": bits = 4; "d": bits = 0;
                default: begin
                    ignore = add_diag(`SEV_ERROR, tln[15:0], tcol[15:0],
                                      "invalid base in numeric literal");
                    base = "b"; bits = 1;
                end
            endcase
            adv;                                        // consume base char

            if (base == "d") begin
                // whole-value x/z, else a decimal digit run
                if (lp < src_len && (lc(src[lp]) == "x" || lc(src[lp]) == "z" ||
                                     src[lp] == "?")) begin
                    dec_special = 1;
                    fillbit = (lc(src[lp]) == "x") ? 1'bx : 1'bz;
                    adv;
                    while (lp < src_len && src[lp] == "_") adv;
                end else begin
                    while (lp < src_len && (is_digit(src[lp]) || src[lp] == "_")) begin
                        if (src[lp] != "_") val = val*10 + (src[lp]-"0");
                        adv;
                    end
                    nw = 32;
                end
            end else begin
                saw_digit = 0;
                while (lp < src_len && (valid_base_digit(src[lp], base) ||
                                        src[lp] == "_")) begin
                    if (src[lp] == "_") begin
                        adv;
                    end else begin
                        d = src[lp]; dch = lc(d); saw_digit = 1;
                        if (base == "b") begin
                            val = val << 1;
                            if      (dch == "x")               val[0]   = 1'bx;
                            else if (dch == "z" || d == "?")   val[0]   = 1'bz;
                            else                               val[0]   = (d - "0");
                            nw = nw + 1;
                        end else if (base == "o") begin
                            val = val << 3;
                            if      (dch == "x")               val[2:0] = 3'bxxx;
                            else if (dch == "z" || d == "?")   val[2:0] = 3'bzzz;
                            else                               val[2:0] = (d - "0");
                            nw = nw + 3;
                        end else begin                          // hex
                            val = val << 4;
                            if      (dch == "x")               val[3:0] = 4'bxxxx;
                            else if (dch == "z" || d == "?")   val[3:0] = 4'bzzzz;
                            else if (is_digit(d))              val[3:0] = (d - "0");
                            else                               val[3:0] = (dch - "a" + 8'd10);
                            nw = nw + 4;
                        end
                        adv;
                    end
                end
                if (!saw_digit)
                    ignore = add_diag(`SEV_ERROR, tln[15:0], tcol[15:0],
                                      "based literal has no value digits");
            end

            // ------------------------- size extend / truncate (§3.5) ----------
            W = haverun ? size : 32;
            if (W > `VALW) begin
                ignore = add_diag(`SEV_ERROR, tln[15:0], tcol[15:0],
                                  "literal size exceeds VALW bits");
                W = `VALW;
            end
            if (W == 0) W = 32;
            if (nw > `VALW) nw = `VALW;

            if (dec_special) begin
                for (i = 0; i < W; i = i + 1) val[i] = fillbit;
            end else begin
                fillbit = 1'b0;
                if (nw > 0) begin
                    if      (val[nw-1] === 1'bx) fillbit = 1'bx;
                    else if (val[nw-1] === 1'bz) fillbit = 1'bz;
                end
                for (i = nw; i < W; i = i + 1) val[i] = fillbit;     // left-extend
            end
            for (i = W; i < `VALW; i = i + 1) val[i] = 1'b0;         // clear above W

            t_value  = val;
            t_width  = W[15:0];
            t_signed = is_signed;
            push_tok(`T_NUMBER);
        end else begin
            // ------------------------------ unsized decimal -------------------
            t_value  = decval;
            t_width  = 16'd32;
            t_signed = 1'b1;
            push_tok(`T_NUMBER);
        end
    end
endtask

// ---------------------------------------------------------------------------
// scan_op -- punctuation and operators via maximal munch (§3.6).  Every prefix
// of a multi-char operator is itself accepting, so we must take the LONGEST
// match: "===" is one token, not "==" then "=".
// ---------------------------------------------------------------------------
task scan_op;
    reg [7:0] c;
    begin
        c = src[lp];
        case (c)
            "(": begin adv; push_tok(`T_LPAREN); end
            ")": begin adv; push_tok(`T_RPAREN); end
            "[": begin adv; push_tok(`T_LBRACK); end
            "]": begin adv; push_tok(`T_RBRACK); end
            "{": begin adv; push_tok(`T_LBRACE); end
            "}": begin adv; push_tok(`T_RBRACE); end
            ";": begin adv; push_tok(`T_SEMI);   end
            ",": begin adv; push_tok(`T_COMMA);  end
            ":": begin adv; push_tok(`T_COLON);  end
            "#": begin adv; push_tok(`T_HASH);   end
            "@": begin adv; push_tok(`T_AT);     end
            ".": begin adv; push_tok(`T_DOT);    end
            "?": begin adv; push_tok(`T_QUES);   end
            "+": begin adv; push_tok(`T_PLUS);   end
            "-": begin adv; push_tok(`T_MINUS);  end
            "*": begin adv; push_tok(`T_STAR);   end
            "/": begin adv; push_tok(`T_SLASH);  end
            "%": begin adv; push_tok(`T_PCT);    end
            "=": begin
                adv;
                if (peekc(0) == "=") begin
                    adv;
                    if (peekc(0) == "=") begin adv; push_tok(`T_CASE_EQ); end
                    else                       push_tok(`T_EQ);
                end else push_tok(`T_ASSIGN);
            end
            "!": begin
                adv;
                if (peekc(0) == "=") begin
                    adv;
                    if (peekc(0) == "=") begin adv; push_tok(`T_CASE_NEQ); end
                    else                       push_tok(`T_NEQ);
                end else push_tok(`T_NOT);
            end
            "<": begin
                adv;
                if      (peekc(0) == "=") begin adv; push_tok(`T_LE);  end
                else if (peekc(0) == "<") begin adv; push_tok(`T_SHL); end
                else                            push_tok(`T_LT);
            end
            ">": begin
                adv;
                if      (peekc(0) == "=") begin adv; push_tok(`T_GE);  end
                else if (peekc(0) == ">") begin adv; push_tok(`T_SHR); end
                else                            push_tok(`T_GT);
            end
            "&": begin
                adv;
                if (peekc(0) == "&") begin adv; push_tok(`T_LAND); end
                else                       push_tok(`T_AMP);
            end
            "|": begin
                adv;
                if (peekc(0) == "|") begin adv; push_tok(`T_LOR); end
                else                       push_tok(`T_PIPE);
            end
            "^": begin
                adv;
                if (peekc(0) == "~") begin adv; push_tok(`T_XNORT); end
                else                       push_tok(`T_CARET);
            end
            "~": begin
                adv;
                if      (peekc(0) == "^") begin adv; push_tok(`T_XNORT); end
                else if (peekc(0) == "&") begin adv; push_tok(`T_NANDT); end
                else if (peekc(0) == "|") begin adv; push_tok(`T_NORT);  end
                else                            push_tok(`T_BNOT);
            end
            default: begin
                ignore = add_diag(`SEV_ERROR, tln[15:0], tcol[15:0],
                                  "unexpected character");
                adv;
            end
        endcase
    end
endtask

// ---------------------------------------------------------------------------
// lex -- scan the entire source arena into the token arena, ending with T_EOF.
// ---------------------------------------------------------------------------
task lex;
    reg     done;
    reg [7:0] c;
    begin
        lp = 0; lln = 1; lcol = 1; n_tok = 0; done = 1'b0;
        while (!done && !arena_err) begin
            skip_ws_comments;
            tln = lln; tcol = lcol;
            if (lp >= src_len) begin
                push_tok(`T_EOF);
                done = 1'b1;
            end else begin
                c = src[lp];
                if      (is_alpha(c))            scan_ident;
                else if (is_digit(c) || c == "'")scan_number;
                else                             scan_op;
            end
        end
    end
endtask
