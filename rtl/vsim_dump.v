//============================================================================
// vsim_dump.v  --  deterministic token and AST dumps   (§6, §8)
//----------------------------------------------------------------------------
// Include fragment.  Two dumps drive the golden tests:
//   * dump_tokens : one line "LINE:COL KIND lexeme" per token (§8).
//   * dump_ast    : an S-expression walk of the AST (§6).
// Both are written with $fwrite to a caller-supplied descriptor (dfd).  The
// AST walk uses recursive `automatic` tasks and OMITS line/column, so golden
// files stay stable under target reformatting (§6).
//============================================================================

integer dfd;                    // active dump file descriptor

// ---------------------------------------------------------------- name tables
function automatic [8*10-1:0] tok_kind_name;
    input [7:0] k;
    case (k)
        `T_EOF:    tok_kind_name = "EOF";
        `T_IDENT:  tok_kind_name = "IDENT";
        `T_NUMBER: tok_kind_name = "NUMBER";
        `T_LBRACK: tok_kind_name = "LBRACK";
        `T_RBRACK: tok_kind_name = "RBRACK";
        `T_LPAREN: tok_kind_name = "LPAREN";
        `T_RPAREN: tok_kind_name = "RPAREN";
        `T_LBRACE: tok_kind_name = "LBRACE";
        `T_RBRACE: tok_kind_name = "RBRACE";
        `T_SEMI:   tok_kind_name = "SEMI";
        `T_COMMA:  tok_kind_name = "COMMA";
        `T_COLON:  tok_kind_name = "COLON";
        `T_HASH:   tok_kind_name = "HASH";
        `T_AT:     tok_kind_name = "AT";
        `T_DOT:    tok_kind_name = "DOT";
        `T_ASSIGN: tok_kind_name = "ASSIGN";
        `T_LE:     tok_kind_name = "LE";
        `T_EQ:     tok_kind_name = "EQ";
        `T_CASE_EQ:tok_kind_name = "CASEEQ";
        `T_NEQ:    tok_kind_name = "NEQ";
        `T_CASE_NEQ:tok_kind_name= "CASENEQ";
        `T_LT:     tok_kind_name = "LT";
        `T_GT:     tok_kind_name = "GT";
        `T_GE:     tok_kind_name = "GE";
        `T_SHL:    tok_kind_name = "SHL";
        `T_SHR:    tok_kind_name = "SHR";
        `T_LAND:   tok_kind_name = "LAND";
        `T_LOR:    tok_kind_name = "LOR";
        `T_NOT:    tok_kind_name = "NOT";
        `T_BNOT:   tok_kind_name = "BNOT";
        `T_AMP:    tok_kind_name = "AMP";
        `T_PIPE:   tok_kind_name = "PIPE";
        `T_CARET:  tok_kind_name = "CARET";
        `T_XNORT:  tok_kind_name = "XNOR";
        `T_NANDT:  tok_kind_name = "RNAND";
        `T_NORT:   tok_kind_name = "RNOR";
        `T_PLUS:   tok_kind_name = "PLUS";
        `T_MINUS:  tok_kind_name = "MINUS";
        `T_STAR:   tok_kind_name = "STAR";
        `T_SLASH:  tok_kind_name = "SLASH";
        `T_PCT:    tok_kind_name = "PCT";
        `T_QUES:   tok_kind_name = "QUES";
        default:   tok_kind_name = "KW";          // keyword: text carries detail
    endcase
endfunction

function automatic [8*3-1:0] opsym;               // binary/unary operator glyph
    input [7:0] op;
    case (op)
        `T_PLUS:    opsym = "+";   `T_MINUS:   opsym = "-";
        `T_STAR:    opsym = "*";   `T_SLASH:   opsym = "/";  `T_PCT: opsym = "%";
        `T_AMP:     opsym = "&";   `T_PIPE:    opsym = "|";  `T_CARET: opsym = "^";
        `T_XNORT:   opsym = "~^";  `T_NANDT:   opsym = "~&"; `T_NORT: opsym = "~|";
        `T_BNOT:    opsym = "~";   `T_NOT:     opsym = "!";
        `T_LAND:    opsym = "&&";  `T_LOR:     opsym = "||";
        `T_EQ:      opsym = "==";  `T_NEQ:     opsym = "!=";
        `T_CASE_EQ: opsym = "==="; `T_CASE_NEQ: opsym = "!==";
        `T_LT:      opsym = "<";   `T_LE:      opsym = "<=";
        `T_GT:      opsym = ">";   `T_GE:      opsym = ">=";
        `T_SHL:     opsym = "<<";  `T_SHR:     opsym = ">>";
        default:    opsym = "?";
    endcase
endfunction

function automatic [8*4-1:0] gate_name;
    input [7:0] g;
    case (g)
        `K_AND:  gate_name = "and";   `K_OR:   gate_name = "or";
        `K_NOT:  gate_name = "not";   `K_NAND: gate_name = "nand";
        `K_NOR:  gate_name = "nor";   `K_XOR:  gate_name = "xor";
        `K_XNOR: gate_name = "xnor";  `K_BUF:  gate_name = "buf";
        default: gate_name = "?";
    endcase
endfunction

function automatic [8*6-1:0] dir_name;
    input [7:0] d;
    case (d)
        `DIR_INPUT:  dir_name = "input";
        `DIR_OUTPUT: dir_name = "output";
        `DIR_INOUT:  dir_name = "inout";
        default:     dir_name = "port";
    endcase
endfunction

function automatic [8*6-1:0] case_name;
    input [7:0] c;
    case (c)
        `K_CASEX: case_name = "casex";
        `K_CASEZ: case_name = "casez";
        default:  case_name = "case";
    endcase
endfunction

function automatic [8*6-1:0] loop_name;
    input [7:0] c;
    case (c)
        `K_WHILE:  loop_name = "while";
        `K_REPEAT: loop_name = "repeat";
        default:   loop_name = "for";
    endcase
endfunction

function automatic [8*7-1:0] edge_name;
    input [7:0] e;
    case (e)
        `EDGE_POS: edge_name = "posedge";
        `EDGE_NEG: edge_name = "negedge";
        default:   edge_name = "level";
    endcase
endfunction

// ---------------------------------------------------------------- primitives
task sp;                        // emit n spaces
    input integer n;
    integer k;
    for (k = 0; k < n; k = k + 1) $fwrite(dfd, " ");
endtask

// print the low `width` bits of a four-state value as width'bXXXX (§9.8 form)
task dump_val;
    input [`VALW-1:0] val;
    input [15:0]      width;
    integer w, i;
    reg [7:0] c;
    reg has_xz;
    begin
        w = width; if (w <= 0 || w > `VALW) w = 32;
        // any x/z bit forces binary form so four-state values read back exactly
        // (§9.8); otherwise decimal is far more legible in golden dumps.
        has_xz = 1'b0;
        for (i = 0; i < w; i = i + 1)
            if (val[i] === 1'bx || val[i] === 1'bz) has_xz = 1'b1;
        if (has_xz) begin
            $fwrite(dfd, "%0d'b", w);
            for (i = w - 1; i >= 0; i = i - 1) begin
                case (val[i])
                    1'b0: c = "0"; 1'b1: c = "1"; 1'bx: c = "x"; default: c = "z";
                endcase
                $fwrite(dfd, "%c", c);
            end
        end else
            $fwrite(dfd, "%0d'd%0d", w, val & ((`VALW'd1 << w) - 1));
    end
endtask

// ---------------------------------------------------------------------------
// dexpr -- print an expression inline (no indent, no newline).  Ranges and
// event expressions are expressions here too.
// ---------------------------------------------------------------------------
task automatic dexpr;
    input integer h;
    integer c;
    reg first;
    begin
        if (h == 0) begin $fwrite(dfd, "()"); end
        else case (nd_kind[h])
            `ND_IDENT: begin
                $fwrite(dfd, "(ND_IDENT \""); fput_ident(nd_name[h], dfd); $fwrite(dfd, "\")");
            end
            `ND_LITERAL: begin
                $fwrite(dfd, "(ND_LITERAL "); dump_val(nd_value[h], nd_width[h]); $fwrite(dfd, ")");
            end
            `ND_BINARY: begin
                $fwrite(dfd, "(ND_BINARY %0s ", opsym(nd_op[h]));
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_UNARY: begin
                $fwrite(dfd, "(ND_UNARY %0s ", opsym(nd_op[h]));
                dexpr(nd_a[h]); $fwrite(dfd, ")");
            end
            `ND_TERNARY: begin
                $fwrite(dfd, "(ND_TERNARY ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, " ");
                dexpr(nd_c[h]); $fwrite(dfd, ")");
            end
            `ND_BIT_SELECT: begin
                $fwrite(dfd, "(ND_BIT_SELECT ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_PART_SELECT: begin
                $fwrite(dfd, "(ND_PART_SELECT ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, " ");
                dexpr(nd_c[h]); $fwrite(dfd, ")");
            end
            `ND_CONCAT: begin
                $fwrite(dfd, "(ND_CONCAT"); c = nd_a[h];
                while (c != 0) begin $fwrite(dfd, " "); dexpr(c); c = nd_next[c]; end
                $fwrite(dfd, ")");
            end
            `ND_REPLICATION: begin
                $fwrite(dfd, "(ND_REPLICATION ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_RANGE: begin
                $fwrite(dfd, "["); dexpr(nd_a[h]); $fwrite(dfd, ":");
                dexpr(nd_b[h]); $fwrite(dfd, "]");
            end
            `ND_CONN: begin
                $fwrite(dfd, "(ND_CONN \""); fput_ident(nd_name[h], dfd);
                $fwrite(dfd, "\" "); dexpr(nd_a[h]); $fwrite(dfd, ")");
            end
            `ND_EVENT_EXPR: begin
                $fwrite(dfd, "(ND_EVENT_EXPR"); c = nd_a[h];
                while (c != 0) begin $fwrite(dfd, " "); dexpr(c); c = nd_next[c]; end
                $fwrite(dfd, ")");
            end
            `ND_EVENT_TERM: begin
                $fwrite(dfd, "(ND_EVENT_TERM %0s ", edge_name(nd_op[h]));
                dexpr(nd_a[h]); $fwrite(dfd, ")");
            end
            default: $fwrite(dfd, "(?expr %0d)", nd_kind[h]);
        endcase
    end
endtask

// event control of an always block, inline:  @*  or the event-expr S-expr
task automatic devent;
    input integer h;
    begin
        if (h == 0) $fwrite(dfd, "@*");
        else dexpr(h);
    end
endtask

// ---------------------------------------------------------------------------
// dstmt -- print a statement / module item with leading indent, possibly over
// several lines, with NO trailing newline (the caller adds separators).
// ---------------------------------------------------------------------------
task automatic dstmt;
    input integer h;
    input integer ind;
    integer c;
    begin
        if (h == 0) begin sp(ind); $fwrite(dfd, "()"); end
        else case (nd_kind[h])
            `ND_NET: begin
                sp(ind); $fwrite(dfd, "(ND_NET \""); fput_ident(nd_name[h], dfd); $fwrite(dfd, "\"");
                if (nd_a[h] != 0) begin $fwrite(dfd, " "); dexpr(nd_a[h]); end
                $fwrite(dfd, ")");
            end
            `ND_REG: begin
                sp(ind); $fwrite(dfd, "(ND_REG \""); fput_ident(nd_name[h], dfd); $fwrite(dfd, "\"");
                if (nd_a[h] != 0) begin $fwrite(dfd, " "); dexpr(nd_a[h]); end
                if (nd_b[h] != 0) begin $fwrite(dfd, " mem="); dexpr(nd_b[h]); end
                $fwrite(dfd, ")");
            end
            `ND_PARAM: begin
                sp(ind); $fwrite(dfd, "(ND_PARAM \""); fput_ident(nd_name[h], dfd);
                $fwrite(dfd, "\" "); dexpr(nd_a[h]); $fwrite(dfd, ")");
            end
            `ND_CONT_ASSIGN: begin
                sp(ind); $fwrite(dfd, "(ND_CONT_ASSIGN ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_BLK_ASSIGN: begin
                sp(ind); $fwrite(dfd, "(ND_BLK_ASSIGN ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_NB_ASSIGN: begin
                sp(ind); $fwrite(dfd, "(ND_NB_ASSIGN ");
                dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
            end
            `ND_NULL_STMT: begin sp(ind); $fwrite(dfd, "(ND_NULL_STMT)"); end
            `ND_GATE_INST: begin
                sp(ind); $fwrite(dfd, "(ND_GATE_INST %0s \"", gate_name(nd_op[h]));
                fput_ident(nd_name[h], dfd); $fwrite(dfd, "\" (");
                c = nd_a[h]; while (c != 0) begin
                    dexpr(c); if (nd_next[c] != 0) $fwrite(dfd, " "); c = nd_next[c];
                end
                $fwrite(dfd, "))");
            end
            `ND_MOD_INST: begin
                sp(ind); $fwrite(dfd, "(ND_MOD_INST \""); fput_ident(nd_name[h], dfd);
                $fwrite(dfd, "\" \""); if (nd_c[h] != 0) fput_ident(nd_name[nd_c[h]], dfd);
                $fwrite(dfd, "\" ("); c = nd_a[h];
                while (c != 0) begin
                    dexpr(c); if (nd_next[c] != 0) $fwrite(dfd, " "); c = nd_next[c];
                end
                $fwrite(dfd, "))");
            end
            `ND_ALWAYS: begin
                sp(ind); $fwrite(dfd, "(ND_ALWAYS "); devent(nd_a[h]); $fwrite(dfd, "\n");
                dstmt(nd_b[h], ind + 2); $fwrite(dfd, ")");
            end
            `ND_INITIAL: begin
                sp(ind); $fwrite(dfd, "(ND_INITIAL\n");
                dstmt(nd_a[h], ind + 2); $fwrite(dfd, ")");
            end
            `ND_SEQ_BLOCK: begin
                sp(ind); $fwrite(dfd, "(ND_SEQ_BLOCK \""); fput_ident(nd_name[h], dfd); $fwrite(dfd, "\"");
                c = nd_a[h]; while (c != 0) begin $fwrite(dfd, "\n"); dstmt(c, ind + 2); c = nd_next[c]; end
                c = nd_b[h]; while (c != 0) begin $fwrite(dfd, "\n"); dstmt(c, ind + 2); c = nd_next[c]; end
                $fwrite(dfd, ")");
            end
            `ND_IF: begin
                sp(ind); $fwrite(dfd, "(ND_IF "); dexpr(nd_a[h]); $fwrite(dfd, "\n");
                dstmt(nd_b[h], ind + 2);
                if (nd_c[h] != 0) begin $fwrite(dfd, "\n"); dstmt(nd_c[h], ind + 2); end
                $fwrite(dfd, ")");
            end
            `ND_CASE: begin
                sp(ind); $fwrite(dfd, "(ND_CASE %0s ", case_name(nd_op[h])); dexpr(nd_a[h]);
                c = nd_b[h]; while (c != 0) begin $fwrite(dfd, "\n"); dstmt(c, ind + 2); c = nd_next[c]; end
                $fwrite(dfd, ")");
            end
            `ND_CASE_ITEM: begin
                sp(ind); $fwrite(dfd, "(ND_CASE_ITEM ");
                if (nd_a[h] == 0) $fwrite(dfd, "default");
                else begin
                    $fwrite(dfd, "("); c = nd_a[h];
                    while (c != 0) begin dexpr(c); if (nd_next[c] != 0) $fwrite(dfd, " "); c = nd_next[c]; end
                    $fwrite(dfd, ")");
                end
                $fwrite(dfd, "\n"); dstmt(nd_b[h], ind + 2); $fwrite(dfd, ")");
            end
            `ND_LOOP: begin
                sp(ind); $fwrite(dfd, "(ND_LOOP %0s ", loop_name(nd_op[h]));
                if (nd_a[h] != 0) begin dexpr_stmt(nd_a[h]); $fwrite(dfd, " "); end
                dexpr(nd_b[h]);
                if (nd_c[h] != 0) begin $fwrite(dfd, " "); dexpr_stmt(nd_c[h]); end
                $fwrite(dfd, "\n"); dstmt(nd_d[h], ind + 2); $fwrite(dfd, ")");
            end
            `ND_GENERATE: begin
                sp(ind); $fwrite(dfd, "(ND_GENERATE");
                c = nd_a[h]; while (c != 0) begin $fwrite(dfd, "\n"); dstmt(c, ind + 2); c = nd_next[c]; end
                $fwrite(dfd, ")");
            end
            default: begin sp(ind); $fwrite(dfd, "(?stmt %0d)", nd_kind[h]); end
        endcase
    end
endtask

// a bare assignment used as a for-loop init/step, printed inline
task automatic dexpr_stmt;
    input integer h;
    begin
        if (h != 0 && (nd_kind[h] == `ND_BLK_ASSIGN)) begin
            $fwrite(dfd, "(ND_BLK_ASSIGN ");
            dexpr(nd_a[h]); $fwrite(dfd, " "); dexpr(nd_b[h]); $fwrite(dfd, ")");
        end else dexpr(h);
    end
endtask

// one port line
task automatic dport;
    input integer p;
    input integer ind;
    begin
        sp(ind); $fwrite(dfd, "(ND_PORT \""); fput_ident(nd_name[p], dfd);
        $fwrite(dfd, "\" %0s", dir_name(nd_op[p]));
        if (nd_a[p] != 0) begin $fwrite(dfd, " "); dexpr(nd_a[p]); end
        $fwrite(dfd, ")");
    end
endtask

// ---------------------------------------------------------------------------
// dump_module -- the top-level S-expression for one module (§6 layout).
// ---------------------------------------------------------------------------
task automatic dump_module;
    input integer m;
    integer p, it;
    begin
        $fwrite(dfd, "(ND_MODULE \""); fput_ident(nd_name[m], dfd); $fwrite(dfd, "\"");
        // ports
        $fwrite(dfd, "\n"); sp(2); $fwrite(dfd, "(ports");
        if (nd_a[m] == 0) $fwrite(dfd, ")");
        else begin
            p = nd_a[m];
            while (p != 0) begin $fwrite(dfd, "\n"); dport(p, 4); p = nd_next[p]; end
            $fwrite(dfd, ")");
        end
        // items
        $fwrite(dfd, "\n"); sp(2); $fwrite(dfd, "(items");
        if (nd_b[m] == 0) $fwrite(dfd, ")");
        else begin
            it = nd_b[m];
            while (it != 0) begin $fwrite(dfd, "\n"); dstmt(it, 4); it = nd_next[it]; end
            $fwrite(dfd, ")");
        end
        $fwrite(dfd, ")\n");
    end
endtask

task dump_ast;
    input integer root;
    input integer fd;
    integer m;
    begin
        dfd = fd;
        m = root;
        while (m != 0) begin dump_module(m); m = nd_next[m]; end
    end
endtask

// ---------------------------------------------------------------------------
// dump_tokens -- "LINE:COL KIND lexeme" per token (§8).
// ---------------------------------------------------------------------------
task dump_tokens;
    input integer fd;
    integer i;
    reg [7:0] k;
    begin
        for (i = 0; i < n_tok; i = i + 1) begin
            k = tok_kind[i];
            $fwrite(fd, "%0d:%0d %0s ", tok_line[i], tok_col[i], tok_kind_name(k));
            if (k == `T_NUMBER) begin
                dfd = fd; dump_val(tok_value[i], tok_width[i]);
            end else if (k == `T_IDENT || k >= 8'd64) begin   // ident or keyword
                fput_ident(tok_text[i], fd);
            end
            $fwrite(fd, "\n");
        end
    end
endtask
