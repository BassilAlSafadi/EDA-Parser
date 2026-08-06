//============================================================================
// vsim_parser.v  --  recursive-descent + precedence-climbing parser   (§4)
//----------------------------------------------------------------------------
// Include fragment.  Consumes the token arena, produces the AST arena.  An FSM
// alone cannot parse target Verilog: begin/end, parens and nested if/case have
// unbounded depth (§4).  Recursion is supplied by `automatic` functions that
// share the module-level token cursor `cur` and the arenas, and communicate
// only through the returned node handle (Verilog-2001 functions take inputs
// only -- exactly the pattern spec §2.6 prescribes).
//============================================================================

integer         cur;              // token cursor (shared, §2.6)
integer         cur_mod_ports;    // head of current module's port list, for
                                  // non-ANSI direction back-patching (§4.3)

// ------------------------------------------------------------- token helpers
function automatic [7:0] cur_kind;
    input integer u;
    cur_kind = tok_kind[cur];
endfunction

function automatic [7:0] pk1;                 // one token of lookahead
    input integer u;
    pk1 = (cur + 1 < n_tok) ? tok_kind[cur+1] : `T_EOF;
endfunction

// consume the current token, return its index; never step past T_EOF.
function automatic integer eat_tok;
    input integer u;
    begin
        eat_tok = cur;
        if (tok_kind[cur] != `T_EOF) cur = cur + 1;
    end
endfunction

function automatic integer accept;            // eat if kind matches
    input [7:0] k;
    begin
        if (tok_kind[cur] == k) begin ignore = eat_tok(0); accept = 1; end
        else                         accept = 0;
    end
endfunction

function automatic integer expect;            // eat, or diagnose at this point
    input [7:0]      k;
    input [8*80-1:0] msg;
    begin
        if (tok_kind[cur] == k) begin ignore = eat_tok(0); expect = 1; end
        else begin
            ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur], msg);
            expect = 0;
        end
    end
endfunction

// panic-mode recovery: skip to the next sync point and, for ';', consume it
// (§7).  Always makes progress unless already parked on end/endcase/endmodule/
// EOF -- points the enclosing loop tests -- so no caller can spin.
function automatic integer recover;
    input integer u;
    reg d;
    begin
        d = 1'b0;
        while (!d) begin
            case (tok_kind[cur])
                `T_SEMI:                      begin ignore = eat_tok(0); d = 1'b1; end
                `K_END, `K_ENDCASE,
                `K_ENDMODULE, `T_EOF:         d = 1'b1;
                default:                      ignore = eat_tok(0);
            endcase
        end
        recover = 0;
    end
endfunction

// last node of a sibling chain (for appending pre-linked declaration lists)
function automatic integer chain_tail;
    input integer h;
    integer t;
    begin
        t = h;
        while (t != 0 && nd_next[t] != 0) t = nd_next[t];
        chain_tail = t;
    end
endfunction

// ===========================================================================
//  EXPRESSIONS  (§4.2 precedence climbing)
// ===========================================================================

// numeric precedence: larger binds tighter.  0 = not a binary operator.
function automatic integer binop_prec;
    input [7:0] k;
    case (k)
        `T_STAR, `T_SLASH, `T_PCT:                 binop_prec = 11;
        `T_PLUS, `T_MINUS:                         binop_prec = 10;
        `T_SHL,  `T_SHR:                           binop_prec = 9;
        `T_LT,   `T_LE, `T_GT, `T_GE:              binop_prec = 8;
        `T_EQ,   `T_NEQ, `T_CASE_EQ, `T_CASE_NEQ:  binop_prec = 7;
        `T_AMP:                                    binop_prec = 6;
        `T_CARET,`T_XNORT:                         binop_prec = 5;
        `T_PIPE:                                   binop_prec = 4;
        `T_LAND:                                   binop_prec = 3;
        `T_LOR:                                    binop_prec = 2;
        `T_QUES:                                   binop_prec = 1;   // ?: (right)
        default:                                   binop_prec = 0;
    endcase
endfunction

function automatic is_prefix_unop;
    input [7:0] k;
    case (k)
        `T_NOT, `T_BNOT, `T_PLUS, `T_MINUS,
        `T_AMP, `T_PIPE, `T_CARET, `T_XNORT,
        `T_NANDT, `T_NORT:  is_prefix_unop = 1'b1;
        default:            is_prefix_unop = 1'b0;
    endcase
endfunction

// primary ::= NUMBER | IDENT | "(" expr ")" | concat | replication  (§4.2)
function automatic integer parse_primary;
    input integer u;
    integer i, e, n;
    begin
        case (cur_kind(0))
            `T_NUMBER: begin
                i = eat_tok(0);
                n = new_node(`ND_LITERAL);
                nd_value[n]  = tok_value[i];
                nd_width[n]  = tok_width[i];
                nd_signed[n] = tok_signed[i];
                nd_line[n]   = tok_line[i]; nd_col[n] = tok_col[i];
                parse_primary = n;
            end
            `T_IDENT: begin
                i = eat_tok(0);
                n = new_node(`ND_IDENT);
                nd_name[n] = tok_text[i];
                nd_line[n] = tok_line[i]; nd_col[n] = tok_col[i];
                parse_primary = n;
            end
            `T_LPAREN: begin
                ignore = eat_tok(0);
                e = parse_expr(0);
                ignore = expect(`T_RPAREN, "expected ')'");
                parse_primary = e;
            end
            `T_LBRACE: parse_primary = parse_concat_or_repl(0);
            default: begin
                ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur],
                                  "expected expression");
                parse_primary = 0;
            end
        endcase
    end
endfunction

// concat ::= "{" expr {"," expr} "}"   /   replication ::= "{" count concat "}"
// After the first inner expression, one token of lookahead separates them:
// "{" => replication (first expr was the count), else concatenation (§4.3).
function automatic integer parse_concat_or_repl;
    input integer u;
    integer first, inner, e, head, tail, n;
    begin
        ignore = eat_tok(0);                     // "{"
        first  = parse_expr(0);
        if (cur_kind(0) == `T_LBRACE) begin      // replication
            inner = parse_concat_braces(0);
            ignore = expect(`T_RBRACE, "expected '}' after replication");
            n = new_node(`ND_REPLICATION);
            nd_a[n] = first; nd_b[n] = inner;
            parse_concat_or_repl = n;
        end else begin                          // concatenation
            head = first; tail = first;
            while (cur_kind(0) == `T_COMMA) begin
                ignore = eat_tok(0);
                e = parse_expr(0);
                nd_next[tail] = e; tail = e;
            end
            ignore = expect(`T_RBRACE, "expected '}' in concatenation");
            n = new_node(`ND_CONCAT);
            nd_a[n] = head;
            parse_concat_or_repl = n;
        end
    end
endfunction

// the inner "{ ... }" of a replication, always a concatenation
function automatic integer parse_concat_braces;
    input integer u;
    integer head, tail, e, n;
    begin
        ignore = expect(`T_LBRACE, "expected '{'");
        head = parse_expr(0); tail = head;
        while (cur_kind(0) == `T_COMMA) begin
            ignore = eat_tok(0);
            e = parse_expr(0);
            nd_next[tail] = e; tail = e;
        end
        ignore = expect(`T_RBRACE, "expected '}'");
        n = new_node(`ND_CONCAT);
        nd_a[n] = head;
        parse_concat_braces = n;
    end
endfunction

// postfix ::= primary { "[" expr [":" expr] "]" }
// After "[", parse an expr then peek: ":" => part-select, "]" => bit-select.
function automatic integer parse_postfix;
    input integer u;
    integer node, idx, lsb, n;
    begin
        node = parse_primary(0);
        while (cur_kind(0) == `T_LBRACK) begin
            ignore = eat_tok(0);
            idx = parse_expr(0);
            if (cur_kind(0) == `T_COLON) begin
                ignore = eat_tok(0);
                lsb = parse_expr(0);
                ignore = expect(`T_RBRACK, "expected ']' after part-select");
                n = new_node(`ND_PART_SELECT);
                nd_a[n] = node; nd_b[n] = idx; nd_c[n] = lsb;
                node = n;
            end else begin
                ignore = expect(`T_RBRACK, "expected ']' after bit-select");
                n = new_node(`ND_BIT_SELECT);
                nd_a[n] = node; nd_b[n] = idx;
                node = n;
            end
        end
        parse_postfix = node;
    end
endfunction

// unary ::= [prefix_unop] unary | postfix.  A leading &,|,^,~&,~|,~^ here is a
// REDUCTION operator (prefix position); the same glyph infix is bitwise (§4.2).
function automatic integer parse_unary;
    input integer u;
    integer opi, operand, n;
    reg [7:0] op;
    begin
        op = cur_kind(0);
        if (is_prefix_unop(op)) begin
            opi = eat_tok(0);
            operand = parse_unary(0);
            n = new_node(`ND_UNARY);
            nd_op[n] = op; nd_a[n] = operand;
            nd_line[n] = tok_line[opi]; nd_col[n] = tok_col[opi];
            parse_unary = n;
        end else
            parse_unary = parse_postfix(0);
    end
endfunction

// expression ::= precedence climbing over binop_prec.  One function for every
// level (spec §4.2), not one function per level.  ?: is right-associative.
function automatic integer parse_expr;
    input integer min_prec;
    integer left, right, thenE, elseE, opi, n, prec;
    reg [7:0] op;
    reg done;
    begin
        left = parse_unary(0);
        done = 1'b0;
        while (!done) begin
            op   = cur_kind(0);
            prec = binop_prec(op);
            if (prec == 0 || prec < min_prec) done = 1'b1;
            else begin
                opi = eat_tok(0);
                if (op == `T_QUES) begin                 // ternary, right assoc
                    thenE = parse_expr(0);
                    ignore = expect(`T_COLON, "expected ':' in conditional");
                    elseE = parse_expr(prec);
                    n = new_node(`ND_TERNARY);
                    nd_a[n] = left; nd_b[n] = thenE; nd_c[n] = elseE;
                    left = n;
                end else begin                           // binary, left assoc
                    right = parse_expr(prec + 1);
                    n = new_node(`ND_BINARY);
                    nd_op[n] = op; nd_a[n] = left; nd_b[n] = right;
                    nd_line[n] = tok_line[opi]; nd_col[n] = tok_col[opi];
                    left = n;
                end
            end
        end
        parse_expr = left;
    end
endfunction

// ===========================================================================
//  DECLARATIONS AND RANGES
// ===========================================================================

// range ::= "[" const_expr ":" const_expr "]"
function automatic integer parse_range;
    input integer u;
    integer msb, lsb, n;
    begin
        ignore = expect(`T_LBRACK, "expected '['");
        msb = parse_expr(0);
        ignore = expect(`T_COLON, "expected ':' in range");
        lsb = parse_expr(0);
        ignore = expect(`T_RBRACK, "expected ']' after range");
        n = new_node(`ND_RANGE);
        nd_a[n] = msb; nd_b[n] = lsb;
        parse_range = n;
    end
endfunction

function automatic integer opt_range;         // range if present, else 0
    input integer u;
    opt_range = (cur_kind(0) == `T_LBRACK) ? parse_range(0) : 0;
endfunction

function automatic integer opt_signed;        // consume "signed", report it
    input integer u;
    opt_signed = accept(`K_SIGNED);
endfunction

// net_decl ::= "wire" [signed] [range] IDENT {"," IDENT} ";"   -> ND_NET chain
function automatic integer parse_net_decl;
    input integer u;
    integer rng, sgn, head, tail, i, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // wire
        sgn = opt_signed(0);
        rng = opt_range(0);
        head = 0; tail = 0; more = 1'b1;
        while (more) begin
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                n = new_node(`ND_NET);
                nd_name[n] = tok_text[i]; nd_a[n] = rng; nd_signed[n] = sgn;
                if (head == 0) head = n; else nd_next[tail] = n;
                tail = n;
            end else
                ignore = expect(`T_IDENT, "expected net name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after net declaration");
        parse_net_decl = head;
    end
endfunction

// reg_decl ::= "reg" [signed] [range] reg_ident {"," reg_ident} ";"
// reg_ident ::= IDENT [range]   (a trailing range makes a memory, §4.3)
function automatic integer parse_reg_decl;
    input integer u;
    integer elemrng, sgn, arng, head, tail, i, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // reg
        sgn = opt_signed(0);
        elemrng = opt_range(0);
        head = 0; tail = 0; more = 1'b1;
        while (more) begin
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                arng = opt_range(0);             // second range => array/memory
                n = new_node(`ND_REG);
                nd_name[n] = tok_text[i];
                nd_a[n] = elemrng; nd_b[n] = arng; nd_signed[n] = sgn;
                if (head == 0) head = n; else nd_next[tail] = n;
                tail = n;
            end else
                ignore = expect(`T_IDENT, "expected reg name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after reg declaration");
        parse_reg_decl = head;
    end
endfunction

// integer_decl ::= "integer" IDENT {"," IDENT} ";"  -> 32-bit signed ND_REG
function automatic integer parse_integer_decl;
    input integer u;
    integer head, tail, i, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // integer
        head = 0; tail = 0; more = 1'b1;
        while (more) begin
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                n = new_node(`ND_REG);
                nd_name[n] = tok_text[i]; nd_signed[n] = 1'b1; nd_width[n] = 16'd32;
                if (head == 0) head = n; else nd_next[tail] = n;
                tail = n;
            end else
                ignore = expect(`T_IDENT, "expected integer name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after integer declaration");
        parse_integer_decl = head;
    end
endfunction

// param_decl ::= "parameter" [signed] [range] param_assign {"," param_assign} ";"
function automatic integer parse_param_decl;
    input integer u;
    integer rng, sgn, head, tail, i, val, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // parameter
        sgn = opt_signed(0);
        rng = opt_range(0);
        head = 0; tail = 0; more = 1'b1;
        while (more) begin
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                ignore = expect(`T_ASSIGN, "expected '=' in parameter");
                val = parse_expr(0);
                n = new_node(`ND_PARAM);
                nd_name[n] = tok_text[i]; nd_a[n] = val; nd_b[n] = rng; nd_signed[n] = sgn;
                if (head == 0) head = n; else nd_next[tail] = n;
                tail = n;
            end else
                ignore = expect(`T_IDENT, "expected parameter name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after parameter declaration");
        parse_param_decl = head;
    end
endfunction

// ---------------------------------------------------------------------------
// find_port -- locate a header port by name for non-ANSI back-patching (§4.3).
// ---------------------------------------------------------------------------
function automatic integer find_port;
    input [`IDW-1:0] nm;
    integer p;
    begin
        p = cur_mod_ports; find_port = 0;
        while (p != 0 && find_port == 0) begin
            if (nd_name[p] == nm) find_port = p;
            p = nd_next[p];
        end
    end
endfunction

// dir keyword -> DIR_* code
function automatic [7:0] dir_code;
    input [7:0] k;
    case (k)
        `K_INPUT:  dir_code = `DIR_INPUT;
        `K_OUTPUT: dir_code = `DIR_OUTPUT;
        default:   dir_code = `DIR_INOUT;
    endcase
endfunction

// port_decl (body) ::= direction [wire|reg] [signed] [range] IDENT {","IDENT} ";"
// Non-ANSI: back-patches the header port's direction/net-type/range and adds
// NOTHING to the item list (returns 0).  A name with no header port is an error
// (§4.3).
function automatic integer parse_port_decl;
    input integer u;
    integer rng, sgn, nt, i, p;
    reg [7:0] dir;
    reg more;
    begin
        dir = dir_code(cur_kind(0));
        ignore = eat_tok(0);                     // direction
        nt = `NT_NONE;
        if      (accept(`K_WIRE)) nt = `NT_WIRE;
        else if (accept(`K_REG))  nt = `NT_REG;
        sgn = opt_signed(0);
        rng = opt_range(0);
        more = 1'b1;
        while (more) begin
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                p = find_port(tok_text[i]);
                if (p == 0)
                    ignore = add_diag(`SEV_ERROR, tok_line[i], tok_col[i],
                                      "port declared but not in module header");
                else begin
                    nd_op[p] = dir;
                    if (nt  != `NT_NONE) nd_b[p] = nt;
                    if (rng != 0)        nd_a[p] = rng;
                    nd_signed[p] = sgn;
                end
            end else
                ignore = expect(`T_IDENT, "expected port name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after port declaration");
        parse_port_decl = 0;                    // not an item
    end
endfunction

// ===========================================================================
//  CONTINUOUS ASSIGN, GATES, INSTANCES
// ===========================================================================

function automatic integer opt_delay;         // "#" NUMBER, stored as literal
    input integer u;
    integer i, n;
    begin
        opt_delay = 0;
        if (cur_kind(0) == `T_HASH) begin
            ignore = eat_tok(0);
            if (cur_kind(0) == `T_NUMBER) begin
                i = eat_tok(0);
                n = new_node(`ND_LITERAL);
                nd_value[n] = tok_value[i]; nd_width[n] = tok_width[i];
                opt_delay = n;
            end else if (cur_kind(0) == `T_LPAREN) begin
                // #(...) delay control -- accept and discard for Phase 1
                ignore = eat_tok(0);
                ignore = parse_expr(0);
                ignore = expect(`T_RPAREN, "expected ')' after delay");
            end
        end
    end
endfunction

function automatic integer parse_lvalue;
    input integer u;
    parse_lvalue = (cur_kind(0) == `T_LBRACE) ? parse_concat_or_repl(0)
                                             : parse_postfix(0);
endfunction

// continuous_assign ::= "assign" [delay] lvalue "=" expr {"," ...} ";"
function automatic integer parse_cont_assign;
    input integer u;
    integer dly, lv, ex, head, tail, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // assign
        dly = opt_delay(0);
        head = 0; tail = 0; more = 1'b1;
        while (more) begin
            lv = parse_lvalue(0);
            ignore = expect(`T_ASSIGN, "expected '=' in continuous assignment");
            ex = parse_expr(0);
            n = new_node(`ND_CONT_ASSIGN);
            nd_a[n] = lv; nd_b[n] = ex; nd_c[n] = dly;
            if (head == 0) head = n; else nd_next[tail] = n;
            tail = n;
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_SEMI, "expected ';' after continuous assignment");
        parse_cont_assign = head;
    end
endfunction

function automatic is_gate_kw;
    input [7:0] k;
    case (k)
        `K_AND, `K_OR, `K_NOT, `K_NAND, `K_NOR,
        `K_XOR, `K_XNOR, `K_BUF: is_gate_kw = 1'b1;
        default:                 is_gate_kw = 1'b0;
    endcase
endfunction

// gate_inst ::= GATE [IDENT] "(" expr {"," expr} ")" ";"
function automatic integer parse_gate_inst;
    input integer u;
    integer e, head, tail, n;
    reg [7:0]  gk;
    reg [15:0] gl, gc;
    reg more;
    begin
        gk = cur_kind(0);
        gl = tok_line[cur]; gc = tok_col[cur];
        ignore = eat_tok(0);                     // gate keyword
        n = new_node(`ND_GATE_INST);
        nd_op[n] = gk;
        nd_line[n] = gl; nd_col[n] = gc;         // position for elaboration (§2)
        if (cur_kind(0) == `T_IDENT) begin
            nd_name[n] = tok_text[cur]; ignore = eat_tok(0);
        end
        ignore = expect(`T_LPAREN, "expected '(' in gate instantiation");
        head = 0; tail = 0;
        if (cur_kind(0) != `T_RPAREN) begin
            more = 1'b1;
            while (more) begin
                e = parse_expr(0);
                if (head == 0) head = e; else nd_next[tail] = e;
                tail = e;
                if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                else                         more = 1'b0;
            end
        end
        ignore = expect(`T_RPAREN, "expected ')' in gate instantiation");
        ignore = expect(`T_SEMI, "expected ';' after gate instantiation");
        nd_a[n] = head;
        parse_gate_inst = n;
    end
endfunction

// one connection: ".port(expr)" (named) or a positional expression
function automatic integer parse_conn;
    input integer u;
    integer e, n, i;
    begin
        if (cur_kind(0) == `T_DOT) begin
            ignore = eat_tok(0);
            i = cur;
            ignore = expect(`T_IDENT, "expected port name after '.'");
            ignore = expect(`T_LPAREN, "expected '(' in named connection");
            e = (cur_kind(0) == `T_RPAREN) ? 0 : parse_expr(0);
            ignore = expect(`T_RPAREN, "expected ')' in named connection");
            n = new_node(`ND_CONN);
            nd_name[n] = tok_text[i]; nd_a[n] = e;
            parse_conn = n;
        end else
            parse_conn = parse_expr(0);
    end
endfunction

// module_inst ::= IDENT [param_override] IDENT "(" [conn {"," conn}] ")" ";"
function automatic integer parse_mod_inst;
    input integer u;
    integer n, inst, head, tail, c, ov, ovh, ovt, e, i;
    reg more;
    begin
        n = new_node(`ND_MOD_INST);
        nd_name[n] = tok_text[cur];             // module name
        // position of the module name: elaboration reports undefined modules,
        // recursion and bad port connections here (§2 Task 5.1).
        nd_line[n] = tok_line[cur]; nd_col[n] = tok_col[cur];
        ignore = eat_tok(0);
        // optional parameter override  #( ... )
        if (cur_kind(0) == `T_HASH) begin
            ignore = eat_tok(0);
            ignore = expect(`T_LPAREN, "expected '(' in parameter override");
            ovh = 0; ovt = 0;
            if (cur_kind(0) != `T_RPAREN) begin
                more = 1'b1;
                while (more) begin
                    if (cur_kind(0) == `T_DOT) begin
                        ignore = eat_tok(0);
                        i = cur;
                        ignore = expect(`T_IDENT, "expected parameter name");
                        ignore = expect(`T_LPAREN, "expected '('");
                        e = parse_expr(0);
                        ignore = expect(`T_RPAREN, "expected ')'");
                        ov = new_node(`ND_PARAM_ASSIGN);
                        nd_name[ov] = tok_text[i]; nd_a[ov] = e;
                    end else begin
                        e = parse_expr(0);
                        ov = new_node(`ND_PARAM_ASSIGN);
                        nd_a[ov] = e;
                    end
                    if (ovh == 0) ovh = ov; else nd_next[ovt] = ov;
                    ovt = ov;
                    if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                    else                         more = 1'b0;
                end
            end
            ignore = expect(`T_RPAREN, "expected ')' after parameter override");
            nd_b[n] = ovh;
        end
        // instance name
        inst = 0;
        if (cur_kind(0) == `T_IDENT) begin
            inst = new_node(`ND_IDENT);
            nd_name[inst] = tok_text[cur];
            ignore = eat_tok(0);
        end else
            ignore = expect(`T_IDENT, "expected instance name");
        nd_c[n] = inst;
        // connection list
        ignore = expect(`T_LPAREN, "expected '(' in instantiation");
        head = 0; tail = 0;
        if (cur_kind(0) != `T_RPAREN) begin
            more = 1'b1;
            while (more) begin
                c = parse_conn(0);
                if (head == 0) head = c; else nd_next[tail] = c;
                tail = c;
                if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                else                         more = 1'b0;
            end
        end
        ignore = expect(`T_RPAREN, "expected ')' in instantiation");
        ignore = expect(`T_SEMI, "expected ';' after instantiation");
        nd_a[n] = head;
        parse_mod_inst = n;
    end
endfunction

// ===========================================================================
//  PROCEDURAL:  always / initial / statements
// ===========================================================================

// event_expr ::= "*" | event_term {("or"|",") event_term}      (§4.1)
// Returns 0 for "@*" (spec §5.1: ND_ALWAYS.a == 0 means @*).
function automatic integer parse_event_expr;
    input integer u;
    integer head, tail, term, ex, n;
    reg [7:0] edge_kind;
    reg more;
    begin
        ignore = expect(`T_LPAREN, "expected '(' after '@'");
        if (cur_kind(0) == `T_STAR) begin
            ignore = eat_tok(0);
            ignore = expect(`T_RPAREN, "expected ')' after '@(*'");
            parse_event_expr = 0;
        end else begin
            head = 0; tail = 0; more = 1'b1;
            while (more) begin
                edge_kind = `EDGE_NONE;
                if      (accept(`K_POSEDGE)) edge_kind = `EDGE_POS;
                else if (accept(`K_NEGEDGE)) edge_kind = `EDGE_NEG;
                ex = parse_expr(0);
                term = new_node(`ND_EVENT_TERM);
                nd_a[term] = ex; nd_op[term] = edge_kind;
                if (head == 0) head = term; else nd_next[tail] = term;
                tail = term;
                if (cur_kind(0) == `K_OR || cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                else                                                more = 1'b0;
            end
            ignore = expect(`T_RPAREN, "expected ')' after event list");
            n = new_node(`ND_EVENT_EXPR);
            nd_a[n] = head;
            parse_event_expr = n;
        end
    end
endfunction

// always ::= "always" ["@" "(" event_expr ")"] statement
function automatic integer parse_always;
    input integer u;
    integer ev, body, n;
    begin
        ignore = eat_tok(0);                     // always
        ev = 0;
        if (cur_kind(0) == `T_AT) begin
            ignore = eat_tok(0);
            ev = parse_event_expr(0);
        end
        body = parse_statement(0);
        n = new_node(`ND_ALWAYS);
        nd_a[n] = ev; nd_b[n] = body;
        parse_always = n;
    end
endfunction

// initial ::= "initial" statement
function automatic integer parse_initial;
    input integer u;
    integer body, n;
    begin
        ignore = eat_tok(0);                     // initial
        body = parse_statement(0);
        n = new_node(`ND_INITIAL);
        nd_a[n] = body;
        parse_initial = n;
    end
endfunction

// blocking / nonblocking assignment.  Which one is decided HERE by the "="
// vs "<=" token in statement position (§4.4).
function automatic integer parse_assign_stmt;
    input integer u;
    integer lv, dly, ex, n;
    reg [7:0] op;
    begin
        lv = parse_lvalue(0);
        op = cur_kind(0);
        if (op == `T_ASSIGN) begin
            ignore = eat_tok(0); dly = opt_delay(0); ex = parse_expr(0);
            ignore = expect(`T_SEMI, "expected ';' after assignment");
            n = new_node(`ND_BLK_ASSIGN);
            nd_a[n] = lv; nd_b[n] = ex; nd_c[n] = dly;
            parse_assign_stmt = n;
        end else if (op == `T_LE) begin
            ignore = eat_tok(0); dly = opt_delay(0); ex = parse_expr(0);
            ignore = expect(`T_SEMI, "expected ';' after assignment");
            n = new_node(`ND_NB_ASSIGN);
            nd_a[n] = lv; nd_b[n] = ex; nd_c[n] = dly;
            parse_assign_stmt = n;
        end else begin
            ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur],
                              "expected '=' or '<=' in assignment");
            ignore = recover(0);
            parse_assign_stmt = 0;
        end
    end
endfunction

// assign_nosemi ::= lvalue "=" expr   (for-loop init/step, no ';')
function automatic integer parse_assign_nosemi;
    input integer u;
    integer lv, ex, n;
    begin
        lv = parse_lvalue(0);
        ignore = expect(`T_ASSIGN, "expected '=' in for-loop assignment");
        ex = parse_expr(0);
        n = new_node(`ND_BLK_ASSIGN);
        nd_a[n] = lv; nd_b[n] = ex;
        parse_assign_nosemi = n;
    end
endfunction

// if ::= "if" "(" expr ")" statement ["else" statement]
// Dangling else binds to the nearest if: on seeing "else" we consume it in the
// current call rather than returning (§4.3).
function automatic integer parse_if;
    input integer u;
    integer cond, thenS, elseS, n;
    begin
        ignore = eat_tok(0);                     // if
        ignore = expect(`T_LPAREN, "expected '(' after if");
        cond = parse_expr(0);
        ignore = expect(`T_RPAREN, "expected ')' after if condition");
        thenS = parse_statement(0);
        elseS = 0;
        if (cur_kind(0) == `K_ELSE) begin
            ignore = eat_tok(0);
            elseS = parse_statement(0);
        end
        n = new_node(`ND_IF);
        nd_a[n] = cond; nd_b[n] = thenS; nd_c[n] = elseS;
        parse_if = n;
    end
endfunction

// case ::= ("case"|"casex"|"casez") "(" expr ")" case_item+ "endcase"
function automatic integer parse_case;
    input integer u;
    integer sel, head, tail, item, mh, mt, ex, st, n;
    reg [7:0] ck;
    reg more;
    begin
        ck = cur_kind(0);
        ignore = eat_tok(0);                     // case/casex/casez
        ignore = expect(`T_LPAREN, "expected '(' after case");
        sel = parse_expr(0);
        ignore = expect(`T_RPAREN, "expected ')' after case selector");
        head = 0; tail = 0;
        while (cur_kind(0) != `K_ENDCASE && cur_kind(0) != `T_EOF) begin
            mh = 0; mt = 0;
            if (accept(`K_DEFAULT)) begin
                ignore = accept(`T_COLON);      // colon optional after default
            end else begin
                more = 1'b1;
                while (more) begin
                    ex = parse_expr(0);
                    if (mh == 0) mh = ex; else nd_next[mt] = ex;
                    mt = ex;
                    if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                    else                         more = 1'b0;
                end
                ignore = expect(`T_COLON, "expected ':' in case item");
            end
            st = parse_statement(0);
            item = new_node(`ND_CASE_ITEM);
            nd_a[item] = mh; nd_b[item] = st;   // mh==0 => default
            if (head == 0) head = item; else nd_next[tail] = item;
            tail = item;
        end
        ignore = expect(`K_ENDCASE, "expected 'endcase'");
        n = new_node(`ND_CASE);
        nd_a[n] = sel; nd_b[n] = head; nd_op[n] = ck;
        parse_case = n;
    end
endfunction

// seq_block ::= "begin" [":" IDENT] {reg_decl|param_decl|integer} {stmt} "end"
function automatic integer parse_seq_block;
    input integer u;
    integer dh, dt, sh, st, d, s, n, prev;
    reg [`IDW-1:0] lbl;
    begin
        ignore = eat_tok(0);                     // begin
        lbl = 0;
        if (cur_kind(0) == `T_COLON) begin
            ignore = eat_tok(0);
            if (cur_kind(0) == `T_IDENT) begin lbl = tok_text[cur]; ignore = eat_tok(0); end
            else ignore = expect(`T_IDENT, "expected block label");
        end
        dh = 0; dt = 0;
        while (cur_kind(0) == `K_REG || cur_kind(0) == `K_PARAMETER ||
               cur_kind(0) == `K_INTEGER) begin
            if      (cur_kind(0) == `K_REG)       d = parse_reg_decl(0);
            else if (cur_kind(0) == `K_PARAMETER) d = parse_param_decl(0);
            else                                 d = parse_integer_decl(0);
            if (d != 0) begin
                if (dh == 0) dh = d; else nd_next[dt] = d;
                dt = chain_tail(d);
            end
        end
        sh = 0; st = 0;
        while (cur_kind(0) != `K_END && cur_kind(0) != `T_EOF) begin
            prev = cur;
            s = parse_statement(0);
            if (s != 0) begin
                if (sh == 0) sh = s; else nd_next[st] = s;
                st = s;
            end
            if (cur == prev && cur_kind(0) != `K_END && cur_kind(0) != `T_EOF)
                ignore = eat_tok(0);             // guarantee progress
        end
        ignore = expect(`K_END, "expected 'end'");
        n = new_node(`ND_SEQ_BLOCK);
        nd_name[n] = lbl; nd_a[n] = dh; nd_b[n] = sh;
        parse_seq_block = n;
    end
endfunction

// loop ::= for(...) stmt | while(...) stmt | repeat(...) stmt
function automatic integer parse_loop;
    input integer u;
    integer init, cond, step, body, n;
    reg [7:0] lk;
    begin
        lk = cur_kind(0);
        ignore = eat_tok(0);
        init = 0; cond = 0; step = 0;
        ignore = expect(`T_LPAREN, "expected '(' in loop");
        if (lk == `K_FOR) begin
            init = parse_assign_nosemi(0);
            ignore = expect(`T_SEMI, "expected ';' in for-loop");
            cond = parse_expr(0);
            ignore = expect(`T_SEMI, "expected ';' in for-loop");
            step = parse_assign_nosemi(0);
        end else begin                          // while / repeat: single expr
            cond = parse_expr(0);
        end
        ignore = expect(`T_RPAREN, "expected ')' in loop");
        body = parse_statement(0);
        n = new_node(`ND_LOOP);
        nd_op[n] = lk; nd_a[n] = init; nd_b[n] = cond; nd_c[n] = step; nd_d[n] = body;
        parse_loop = n;
    end
endfunction

// statement dispatch (§4.1)
function automatic integer parse_statement;
    input integer u;
    begin
        case (cur_kind(0))
            `T_SEMI:  begin ignore = eat_tok(0); parse_statement = new_node(`ND_NULL_STMT); end
            `K_BEGIN:  parse_statement = parse_seq_block(0);
            `K_IF:     parse_statement = parse_if(0);
            `K_CASE, `K_CASEX, `K_CASEZ:
                       parse_statement = parse_case(0);
            `K_FOR, `K_WHILE, `K_REPEAT:
                       parse_statement = parse_loop(0);
            `T_IDENT, `T_LBRACE:
                       parse_statement = parse_assign_stmt(0);
            default: begin
                ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur],
                                  "expected statement");
                ignore = recover(0);
                parse_statement = 0;
            end
        endcase
    end
endfunction

// ===========================================================================
//  GENERATE  (§4.1 -- basic support; none of the §10 circuits require it)
// ===========================================================================
function automatic integer parse_gen_block;    // "begin [:lbl] items end" | item
    input integer u;
    integer head, tail, it, n, prev;
    begin
        if (cur_kind(0) == `K_BEGIN) begin
            ignore = eat_tok(0);
            if (cur_kind(0) == `T_COLON) begin
                ignore = eat_tok(0);
                ignore = accept(`T_IDENT);
            end
            head = 0; tail = 0;
            while (cur_kind(0) != `K_END && cur_kind(0) != `T_EOF) begin
                prev = cur;
                it = parse_module_item(0);
                if (it != 0) begin
                    if (head == 0) head = it; else nd_next[tail] = it;
                    tail = chain_tail(it);
                end
                if (cur == prev && cur_kind(0) != `K_END) ignore = eat_tok(0);
            end
            ignore = expect(`K_END, "expected 'end' in generate block");
            n = new_node(`ND_GENERATE);
            nd_a[n] = head;
            parse_gen_block = n;
        end else
            parse_gen_block = parse_module_item(0);
    end
endfunction

// generate ::= "generate" generate_item* "endgenerate"
function automatic integer parse_generate;
    input integer u;
    integer head, tail, it, n, prev;
    begin
        ignore = eat_tok(0);                     // generate
        head = 0; tail = 0;
        while (cur_kind(0) != `K_ENDGENERATE && cur_kind(0) != `T_EOF) begin
            prev = cur;
            if (cur_kind(0) == `K_FOR) begin
                ignore = eat_tok(0);
                ignore = expect(`T_LPAREN, "expected '(' in generate for");
                ignore = parse_assign_nosemi(0);
                ignore = expect(`T_SEMI, "expected ';'");
                ignore = parse_expr(0);
                ignore = expect(`T_SEMI, "expected ';'");
                ignore = parse_assign_nosemi(0);
                ignore = expect(`T_RPAREN, "expected ')'");
                it = parse_gen_block(0);
            end else if (cur_kind(0) == `K_IF) begin
                ignore = eat_tok(0);
                ignore = expect(`T_LPAREN, "expected '('");
                ignore = parse_expr(0);
                ignore = expect(`T_RPAREN, "expected ')'");
                it = parse_gen_block(0);
                if (cur_kind(0) == `K_ELSE) begin ignore = eat_tok(0); ignore = parse_gen_block(0); end
            end else
                it = parse_module_item(0);
            if (it != 0) begin
                if (head == 0) head = it; else nd_next[tail] = it;
                tail = chain_tail(it);
            end
            if (cur == prev && cur_kind(0) != `K_ENDGENERATE) ignore = eat_tok(0);
        end
        ignore = expect(`K_ENDGENERATE, "expected 'endgenerate'");
        n = new_node(`ND_GENERATE);
        nd_a[n] = head;
        parse_generate = n;
    end
endfunction

// ===========================================================================
//  MODULE STRUCTURE
// ===========================================================================

// one ANSI port: direction [wire|reg] [signed] [range] IDENT
function automatic integer parse_ansi_port;
    input integer u;
    integer rng, sgn, nt, i, n;
    reg [7:0] dir;
    begin
        dir = dir_code(cur_kind(0));
        ignore = eat_tok(0);
        nt = `NT_NONE;
        if      (accept(`K_WIRE)) nt = `NT_WIRE;
        else if (accept(`K_REG))  nt = `NT_REG;
        sgn = opt_signed(0);
        rng = opt_range(0);
        n = new_node(`ND_PORT);
        nd_op[n] = dir; nd_a[n] = rng; nd_b[n] = nt; nd_signed[n] = sgn;
        if (cur_kind(0) == `T_IDENT) begin nd_name[n] = tok_text[cur]; ignore = eat_tok(0); end
        else ignore = expect(`T_IDENT, "expected port name");
        parse_ansi_port = n;
    end
endfunction

// port_list ::= "(" [port {"," port}] ")".  ANSI vs non-ANSI decided by the
// first port token (§4.3): a direction keyword => ANSI, a bare IDENT => non-ANSI
// (directions arrive later via body port_decls).
function automatic integer parse_ports;
    input integer u;
    integer head, tail, p, i;
    reg ansi, more;
    begin
        if (cur_kind(0) != `T_LPAREN) begin parse_ports = 0; end
        else begin
            ignore = eat_tok(0);                 // (
            if (cur_kind(0) == `T_RPAREN) begin ignore = eat_tok(0); parse_ports = 0; end
            else begin
                ansi = (cur_kind(0) == `K_INPUT || cur_kind(0) == `K_OUTPUT ||
                        cur_kind(0) == `K_INOUT);
                head = 0; tail = 0; more = 1'b1;
                while (more) begin
                    if (ansi)
                        p = parse_ansi_port(0);
                    else if (cur_kind(0) == `T_IDENT) begin
                        i = eat_tok(0);
                        p = new_node(`ND_PORT);
                        nd_name[p] = tok_text[i]; nd_op[p] = `DIR_NONE;
                    end else begin
                        ignore = expect(`T_IDENT, "expected port");
                        p = 0;
                    end
                    if (p != 0) begin
                        if (head == 0) head = p; else nd_next[tail] = p;
                        tail = p;
                    end
                    if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
                    else                         more = 1'b0;
                end
                ignore = expect(`T_RPAREN, "expected ')' after port list");
                parse_ports = head;
            end
        end
    end
endfunction

// param_port_list ::= "#" "(" param_assign {"," param_assign} ")"
function automatic integer parse_param_port_list;
    input integer u;
    integer head, tail, i, val, n;
    reg more;
    begin
        ignore = eat_tok(0);                     // #
        ignore = expect(`T_LPAREN, "expected '(' in parameter port list");
        head = 0; tail = 0; more = 1'b1;
        while (more && cur_kind(0) != `T_RPAREN && cur_kind(0) != `T_EOF) begin
            ignore = accept(`K_PARAMETER);      // optional
            ignore = opt_signed(0);
            ignore = opt_range(0);
            if (cur_kind(0) == `T_IDENT) begin
                i = eat_tok(0);
                ignore = expect(`T_ASSIGN, "expected '=' in parameter");
                val = parse_expr(0);
                n = new_node(`ND_PARAM);
                nd_name[n] = tok_text[i]; nd_a[n] = val;
                if (head == 0) head = n; else nd_next[tail] = n;
                tail = n;
            end else
                ignore = expect(`T_IDENT, "expected parameter name");
            if (cur_kind(0) == `T_COMMA) ignore = eat_tok(0);
            else                         more = 1'b0;
        end
        ignore = expect(`T_RPAREN, "expected ')' after parameter port list");
        parse_param_port_list = head;
    end
endfunction

// module_item dispatch.  A bare IDENT can only begin a module instantiation --
// every other alternative starts with a keyword or gate primitive, so this is
// LL(1) (§4.3).  Returns 0 for body port_decls (they annotate header ports).
function automatic integer parse_module_item;
    input integer u;
    begin
        case (cur_kind(0))
            `K_WIRE:                       parse_module_item = parse_net_decl(0);
            `K_REG:                        parse_module_item = parse_reg_decl(0);
            `K_INTEGER:                    parse_module_item = parse_integer_decl(0);
            `K_INPUT, `K_OUTPUT, `K_INOUT: parse_module_item = parse_port_decl(0);
            `K_PARAMETER:                  parse_module_item = parse_param_decl(0);
            `K_ASSIGN:                     parse_module_item = parse_cont_assign(0);
            `K_ALWAYS:                     parse_module_item = parse_always(0);
            `K_INITIAL:                    parse_module_item = parse_initial(0);
            `K_GENERATE:                   parse_module_item = parse_generate(0);
            `T_IDENT:                      parse_module_item = parse_mod_inst(0);
            default: begin
                if (is_gate_kw(cur_kind(0)))
                    parse_module_item = parse_gate_inst(0);
                else begin
                    ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur],
                                      "expected module item");
                    ignore = recover(0);
                    parse_module_item = 0;
                end
            end
        endcase
    end
endfunction

// module_decl ::= "module" IDENT [param_port_list] [port_list] ";"
//                 module_item* "endmodule"
function automatic integer parse_module;
    input integer u;
    integer m, params, ports, items, head, tail, it, prev;
    begin
        ignore = eat_tok(0);                     // module
        m = new_node(`ND_MODULE);
        // position of the module name: elaboration diagnostics (duplicate
        // definition, §2 Task 5.1) need a source location to point at.
        nd_line[m] = tok_line[cur]; nd_col[m] = tok_col[cur];
        if (cur_kind(0) == `T_IDENT) begin nd_name[m] = tok_text[cur]; ignore = eat_tok(0); end
        else ignore = expect(`T_IDENT, "expected module name");

        params = (cur_kind(0) == `T_HASH) ? parse_param_port_list(0) : 0;
        nd_c[m] = params;

        ports = parse_ports(0);
        nd_a[m] = ports;
        cur_mod_ports = ports;                  // enable non-ANSI back-patching

        ignore = expect(`T_SEMI, "expected ';' after module header");

        head = 0; tail = 0;
        while (cur_kind(0) != `K_ENDMODULE && cur_kind(0) != `T_EOF) begin
            prev = cur;
            it = parse_module_item(0);
            if (it != 0) begin
                if (head == 0) head = it; else nd_next[tail] = it;
                tail = chain_tail(it);
            end
            if (cur == prev && cur_kind(0) != `K_ENDMODULE && cur_kind(0) != `T_EOF)
                ignore = eat_tok(0);             // guarantee progress
        end
        nd_b[m] = head;
        ignore = expect(`K_ENDMODULE, "expected 'endmodule'");
        parse_module = m;
    end
endfunction

// source_text ::= module_decl* EOF.  Returns the head of the module chain.
function automatic integer parse_source;
    input integer u;
    integer head, tail, m, prev;
    reg stop;
    begin
        cur = 0;
        head = 0; tail = 0; stop = 1'b0;
        while (!stop && cur_kind(0) != `T_EOF && !arena_err) begin
            prev = cur;
            if (cur_kind(0) == `K_MODULE) begin
                m = parse_module(0);
                if (m != 0) begin
                    if (head == 0) head = m; else nd_next[tail] = m;
                    tail = m;
                end
            end else begin
                ignore = add_diag(`SEV_ERROR, tok_line[cur], tok_col[cur],
                                  "expected 'module'");
                ignore = eat_tok(0);
            end
            if (cur == prev && cur_kind(0) != `T_EOF) ignore = eat_tok(0);
            if (n_diag >= `MAX_DIAG) stop = 1'b1;         // stop after MAX_DIAG
        end
        parse_source = head;
    end
endfunction
