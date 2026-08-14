//============================================================================
// parser.cpp -- ported from vsim_parser.v (§4): recursive-descent +
// precedence-climbing parser. Consumes the token arena, produces the AST
// arena. Recursion is ordinary C++ recursion here; the original needed
// `automatic` Verilog functions sharing a module-level token cursor `cur`
// specifically because Verilog-2001 functions take only inputs (spec §2.6) --
// that constraint doesn't exist in C++, but `cur` and the arenas are kept as
// class members anyway so the structure (and the diffable per-function
// shape) stays 1:1 with the original.
//============================================================================
#include "vsim.hpp"

namespace vsim {

// ------------------------------------------------------------- token helpers
Kind Vsim::cur_kind() const { return tok_kind[static_cast<std::size_t>(cur)]; }

Kind Vsim::pk1() const {                          // one token of lookahead
    return (cur + 1 < n_tok) ? tok_kind[static_cast<std::size_t>(cur + 1)] : T_EOF;
}

// consume the current token, return its index; never step past T_EOF.
int Vsim::eat_tok() {
    int h = cur;
    if (tok_kind[static_cast<std::size_t>(cur)] != T_EOF) cur = cur + 1;
    return h;
}

bool Vsim::accept(Kind k) {                       // eat if kind matches
    if (tok_kind[static_cast<std::size_t>(cur)] == k) { eat_tok(); return true; }
    return false;
}

bool Vsim::expect(Kind k, const std::string& msg) {  // eat, or diagnose at this point
    if (tok_kind[static_cast<std::size_t>(cur)] == k) { eat_tok(); return true; }
    add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)], msg);
    return false;
}

// panic-mode recovery: skip to the next sync point and, for ';', consume it
// (§7). Always makes progress unless already parked on end/endcase/
// endmodule/EOF -- points the enclosing loop tests -- so no caller can spin.
void Vsim::recover() {
    bool d = false;
    while (!d) {
        switch (tok_kind[static_cast<std::size_t>(cur)]) {
            case T_SEMI:                          eat_tok(); d = true; break;
            case K_END: case K_ENDCASE:
            case K_ENDMODULE: case T_EOF:         d = true; break;
            default:                              eat_tok(); break;
        }
    }
}

// last node of a sibling chain (for appending pre-linked declaration lists)
int Vsim::chain_tail(int h) const {
    int t = h;
    while (t != 0 && nd_next[static_cast<std::size_t>(t)] != 0) t = nd_next[static_cast<std::size_t>(t)];
    return t;
}

// ===========================================================================
//  EXPRESSIONS  (§4.2 precedence climbing)
// ===========================================================================

// numeric precedence: larger binds tighter. 0 = not a binary operator.
int Vsim::binop_prec(Kind k) {
    switch (k) {
        case T_STAR: case T_SLASH: case T_PCT:                return 11;
        case T_PLUS: case T_MINUS:                            return 10;
        case T_SHL:  case T_SHR:                              return 9;
        case T_LT:   case T_LE: case T_GT: case T_GE:         return 8;
        case T_EQ:   case T_NEQ: case T_CASE_EQ: case T_CASE_NEQ: return 7;
        case T_AMP:                                           return 6;
        case T_CARET: case T_XNORT:                           return 5;
        case T_PIPE:                                          return 4;
        case T_LAND:                                          return 3;
        case T_LOR:                                           return 2;
        case T_QUES:                                          return 1;   // ?: (right)
        default:                                               return 0;
    }
}

bool Vsim::is_prefix_unop(Kind k) {
    switch (k) {
        case T_NOT: case T_BNOT: case T_PLUS: case T_MINUS:
        case T_AMP: case T_PIPE: case T_CARET: case T_XNORT:
        case T_NANDT: case T_NORT: return true;
        default:                   return false;
    }
}

// primary ::= NUMBER | IDENT | "(" expr ")" | concat | replication  (§4.2)
int Vsim::parse_primary() {
    switch (cur_kind()) {
        case T_NUMBER: {
            int i = eat_tok();
            int n = new_node(ND_LITERAL);
            nd_value[static_cast<std::size_t>(n)]  = tok_value[static_cast<std::size_t>(i)];
            nd_width[static_cast<std::size_t>(n)]  = tok_width[static_cast<std::size_t>(i)];
            nd_signed[static_cast<std::size_t>(n)] = tok_signed[static_cast<std::size_t>(i)];
            nd_line[static_cast<std::size_t>(n)]   = tok_line[static_cast<std::size_t>(i)];
            nd_col[static_cast<std::size_t>(n)]    = tok_col[static_cast<std::size_t>(i)];
            return n;
        }
        case T_IDENT: {
            int i = eat_tok();
            int n = new_node(ND_IDENT);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)];
            nd_line[static_cast<std::size_t>(n)] = tok_line[static_cast<std::size_t>(i)];
            nd_col[static_cast<std::size_t>(n)]  = tok_col[static_cast<std::size_t>(i)];
            return n;
        }
        case T_LPAREN: {
            eat_tok();
            int e = parse_expr(0);
            expect(T_RPAREN, "expected ')'");
            return e;
        }
        case T_LBRACE:
            return parse_concat_or_repl();
        default:
            add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)], "expected expression");
            return 0;
    }
}

// concat ::= "{" expr {"," expr} "}"   /   replication ::= "{" count concat "}"
// After the first inner expression, one token of lookahead separates them:
// "{" => replication (first expr was the count), else concatenation (§4.3).
int Vsim::parse_concat_or_repl() {
    eat_tok();                                    // "{"
    int first = parse_expr(0);
    if (cur_kind() == T_LBRACE) {                 // replication
        int inner = parse_concat_braces();
        expect(T_RBRACE, "expected '}' after replication");
        int n = new_node(ND_REPLICATION);
        nd_a[static_cast<std::size_t>(n)] = first; nd_b[static_cast<std::size_t>(n)] = inner;
        return n;
    }
    // concatenation
    int head = first, tail = first;
    while (cur_kind() == T_COMMA) {
        eat_tok();
        int e = parse_expr(0);
        nd_next[static_cast<std::size_t>(tail)] = e; tail = e;
    }
    expect(T_RBRACE, "expected '}' in concatenation");
    int n = new_node(ND_CONCAT);
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// the inner "{ ... }" of a replication, always a concatenation
int Vsim::parse_concat_braces() {
    expect(T_LBRACE, "expected '{'");
    int head = parse_expr(0), tail = head;
    while (cur_kind() == T_COMMA) {
        eat_tok();
        int e = parse_expr(0);
        nd_next[static_cast<std::size_t>(tail)] = e; tail = e;
    }
    expect(T_RBRACE, "expected '}'");
    int n = new_node(ND_CONCAT);
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// postfix ::= primary { "[" expr [":" expr] "]" }
// After "[", parse an expr then peek: ":" => part-select, "]" => bit-select.
int Vsim::parse_postfix() {
    int node = parse_primary();
    while (cur_kind() == T_LBRACK) {
        eat_tok();
        int idx = parse_expr(0);
        if (cur_kind() == T_COLON) {
            eat_tok();
            int lsb = parse_expr(0);
            expect(T_RBRACK, "expected ']' after part-select");
            int n = new_node(ND_PART_SELECT);
            nd_a[static_cast<std::size_t>(n)] = node; nd_b[static_cast<std::size_t>(n)] = idx; nd_c[static_cast<std::size_t>(n)] = lsb;
            node = n;
        } else {
            expect(T_RBRACK, "expected ']' after bit-select");
            int n = new_node(ND_BIT_SELECT);
            nd_a[static_cast<std::size_t>(n)] = node; nd_b[static_cast<std::size_t>(n)] = idx;
            node = n;
        }
    }
    return node;
}

// unary ::= [prefix_unop] unary | postfix. A leading &,|,^,~&,~|,~^ here is a
// REDUCTION operator (prefix position); the same glyph infix is bitwise (§4.2).
int Vsim::parse_unary() {
    Kind op = cur_kind();
    if (is_prefix_unop(op)) {
        int opi = eat_tok();
        int operand = parse_unary();
        int n = new_node(ND_UNARY);
        nd_op[static_cast<std::size_t>(n)] = op; nd_a[static_cast<std::size_t>(n)] = operand;
        nd_line[static_cast<std::size_t>(n)] = tok_line[static_cast<std::size_t>(opi)];
        nd_col[static_cast<std::size_t>(n)]  = tok_col[static_cast<std::size_t>(opi)];
        return n;
    }
    return parse_postfix();
}

// expression ::= precedence climbing over binop_prec. One function for
// every level (spec §4.2), not one function per level. ?: is right-associative.
int Vsim::parse_expr(int min_prec) {
    int left = parse_unary();
    bool done = false;
    while (!done) {
        Kind op = cur_kind();
        int prec = binop_prec(op);
        if (prec == 0 || prec < min_prec) { done = true; continue; }
        int opi = eat_tok();
        if (op == T_QUES) {                       // ternary, right assoc
            int thenE = parse_expr(0);
            expect(T_COLON, "expected ':' in conditional");
            int elseE = parse_expr(prec);
            int n = new_node(ND_TERNARY);
            nd_a[static_cast<std::size_t>(n)] = left; nd_b[static_cast<std::size_t>(n)] = thenE; nd_c[static_cast<std::size_t>(n)] = elseE;
            left = n;
        } else {                                  // binary, left assoc
            int right = parse_expr(prec + 1);
            int n = new_node(ND_BINARY);
            nd_op[static_cast<std::size_t>(n)] = op; nd_a[static_cast<std::size_t>(n)] = left; nd_b[static_cast<std::size_t>(n)] = right;
            nd_line[static_cast<std::size_t>(n)] = tok_line[static_cast<std::size_t>(opi)];
            nd_col[static_cast<std::size_t>(n)]  = tok_col[static_cast<std::size_t>(opi)];
            left = n;
        }
    }
    return left;
}

// ===========================================================================
//  DECLARATIONS AND RANGES
// ===========================================================================

// range ::= "[" const_expr ":" const_expr "]"
int Vsim::parse_range() {
    expect(T_LBRACK, "expected '['");
    int msb = parse_expr(0);
    expect(T_COLON, "expected ':' in range");
    int lsb = parse_expr(0);
    expect(T_RBRACK, "expected ']' after range");
    int n = new_node(ND_RANGE);
    nd_a[static_cast<std::size_t>(n)] = msb; nd_b[static_cast<std::size_t>(n)] = lsb;
    return n;
}

int Vsim::opt_range() {                           // range if present, else 0
    return (cur_kind() == T_LBRACK) ? parse_range() : 0;
}

bool Vsim::opt_signed() {                         // consume "signed", report it
    return accept(K_SIGNED);
}

// net_decl ::= "wire" [signed] [range] IDENT {"," IDENT} ";"   -> ND_NET chain
int Vsim::parse_net_decl() {
    eat_tok();                                    // wire
    bool sgn = opt_signed();
    int rng = opt_range();
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            int n = new_node(ND_NET);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)];
            nd_a[static_cast<std::size_t>(n)] = rng; nd_signed[static_cast<std::size_t>(n)] = sgn;
            if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
            tail = n;
        } else
            expect(T_IDENT, "expected net name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after net declaration");
    return head;
}

// reg_decl ::= "reg" [signed] [range] reg_ident {"," reg_ident} ";"
// reg_ident ::= IDENT [range]   (a trailing range makes a memory, §4.3)
int Vsim::parse_reg_decl() {
    eat_tok();                                    // reg
    bool sgn = opt_signed();
    int elemrng = opt_range();
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            int arng = opt_range();               // second range => array/memory
            int n = new_node(ND_REG);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)];
            nd_a[static_cast<std::size_t>(n)] = elemrng; nd_b[static_cast<std::size_t>(n)] = arng; nd_signed[static_cast<std::size_t>(n)] = sgn;
            if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
            tail = n;
        } else
            expect(T_IDENT, "expected reg name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after reg declaration");
    return head;
}

// integer_decl ::= "integer" IDENT {"," IDENT} ";"  -> 32-bit signed ND_REG
int Vsim::parse_integer_decl() {
    eat_tok();                                    // integer
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            int n = new_node(ND_REG);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)];
            nd_signed[static_cast<std::size_t>(n)] = true; nd_width[static_cast<std::size_t>(n)] = 32;
            if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
            tail = n;
        } else
            expect(T_IDENT, "expected integer name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after integer declaration");
    return head;
}

// param_decl ::= "parameter" [signed] [range] param_assign {"," param_assign} ";"
int Vsim::parse_param_decl() {
    eat_tok();                                    // parameter
    bool sgn = opt_signed();
    int rng = opt_range();
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            expect(T_ASSIGN, "expected '=' in parameter");
            int val = parse_expr(0);
            int n = new_node(ND_PARAM);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)];
            nd_a[static_cast<std::size_t>(n)] = val; nd_b[static_cast<std::size_t>(n)] = rng; nd_signed[static_cast<std::size_t>(n)] = sgn;
            if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
            tail = n;
        } else
            expect(T_IDENT, "expected parameter name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after parameter declaration");
    return head;
}

// ---------------------------------------------------------------------------
// find_port -- locate a header port by name for non-ANSI back-patching (§4.3).
// ---------------------------------------------------------------------------
int Vsim::find_port(const std::string& nm) const {
    int p = cur_mod_ports;
    int found = 0;
    while (p != 0 && found == 0) {
        if (nd_name[static_cast<std::size_t>(p)] == nm) found = p;
        p = nd_next[static_cast<std::size_t>(p)];
    }
    return found;
}

// dir keyword -> DIR_* code
Kind Vsim::dir_code(Kind k) {
    switch (k) {
        case K_INPUT:  return DIR_INPUT;
        case K_OUTPUT: return DIR_OUTPUT;
        default:       return DIR_INOUT;
    }
}

// port_decl (body) ::= direction [wire|reg] [signed] [range] IDENT {","IDENT} ";"
// Non-ANSI: back-patches the header port's direction/net-type/range and adds
// NOTHING to the item list (returns 0). A name with no header port is an
// error (§4.3).
int Vsim::parse_port_decl() {
    Kind dir = dir_code(cur_kind());
    eat_tok();                                    // direction
    std::uint16_t nt = NT_NONE;
    if      (accept(K_WIRE)) nt = NT_WIRE;
    else if (accept(K_REG))  nt = NT_REG;
    bool sgn = opt_signed();
    int rng = opt_range();
    bool more = true;
    while (more) {
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            int p = find_port(tok_text[static_cast<std::size_t>(i)]);
            if (p == 0)
                add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(i)], tok_col[static_cast<std::size_t>(i)],
                         "port declared but not in module header");
            else {
                nd_op[static_cast<std::size_t>(p)] = dir;
                if (nt  != NT_NONE) nd_b[static_cast<std::size_t>(p)] = nt;
                if (rng != 0)       nd_a[static_cast<std::size_t>(p)] = rng;
                nd_signed[static_cast<std::size_t>(p)] = sgn;
            }
        } else
            expect(T_IDENT, "expected port name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after port declaration");
    return 0;                                     // not an item
}

// ===========================================================================
//  CONTINUOUS ASSIGN, GATES, INSTANCES
// ===========================================================================

int Vsim::opt_delay() {                           // "#" NUMBER, stored as literal
    if (cur_kind() != T_HASH) return 0;
    eat_tok();
    if (cur_kind() == T_NUMBER) {
        int i = eat_tok();
        int n = new_node(ND_LITERAL);
        nd_value[static_cast<std::size_t>(n)] = tok_value[static_cast<std::size_t>(i)];
        nd_width[static_cast<std::size_t>(n)] = tok_width[static_cast<std::size_t>(i)];
        return n;
    }
    if (cur_kind() == T_LPAREN) {
        // #(...) delay control -- accept and discard for Phase 1
        eat_tok();
        parse_expr(0);
        expect(T_RPAREN, "expected ')' after delay");
    }
    return 0;
}

int Vsim::parse_lvalue() {
    return (cur_kind() == T_LBRACE) ? parse_concat_or_repl() : parse_postfix();
}

// continuous_assign ::= "assign" [delay] lvalue "=" expr {"," ...} ";"
int Vsim::parse_cont_assign() {
    eat_tok();                                    // assign
    int dly = opt_delay();
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        int lv = parse_lvalue();
        expect(T_ASSIGN, "expected '=' in continuous assignment");
        int ex = parse_expr(0);
        int n = new_node(ND_CONT_ASSIGN);
        nd_a[static_cast<std::size_t>(n)] = lv; nd_b[static_cast<std::size_t>(n)] = ex; nd_c[static_cast<std::size_t>(n)] = dly;
        if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
        tail = n;
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_SEMI, "expected ';' after continuous assignment");
    return head;
}

bool Vsim::is_gate_kw(Kind k) {
    switch (k) {
        case K_AND: case K_OR: case K_NOT: case K_NAND: case K_NOR:
        case K_XOR: case K_XNOR: case K_BUF: return true;
        default:                             return false;
    }
}

// gate_inst ::= GATE [IDENT] "(" expr {"," expr} ")" ";"
int Vsim::parse_gate_inst() {
    Kind gk = cur_kind();
    eat_tok();                                    // gate keyword
    int n = new_node(ND_GATE_INST);
    nd_op[static_cast<std::size_t>(n)] = gk;
    if (cur_kind() == T_IDENT) {
        nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(cur)];
        eat_tok();
    }
    expect(T_LPAREN, "expected '(' in gate instantiation");
    int head = 0, tail = 0;
    if (cur_kind() != T_RPAREN) {
        bool more = true;
        while (more) {
            int e = parse_expr(0);
            if (head == 0) head = e; else nd_next[static_cast<std::size_t>(tail)] = e;
            tail = e;
            if (cur_kind() == T_COMMA) eat_tok();
            else                       more = false;
        }
    }
    expect(T_RPAREN, "expected ')' in gate instantiation");
    expect(T_SEMI, "expected ';' after gate instantiation");
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// one connection: ".port(expr)" (named) or a positional expression
int Vsim::parse_conn() {
    if (cur_kind() == T_DOT) {
        eat_tok();
        int i = cur;
        expect(T_IDENT, "expected port name after '.'");
        expect(T_LPAREN, "expected '(' in named connection");
        int e = (cur_kind() == T_RPAREN) ? 0 : parse_expr(0);
        expect(T_RPAREN, "expected ')' in named connection");
        int n = new_node(ND_CONN);
        nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)]; nd_a[static_cast<std::size_t>(n)] = e;
        return n;
    }
    return parse_expr(0);
}

// module_inst ::= IDENT [param_override] IDENT "(" [conn {"," conn}] ")" ";"
int Vsim::parse_mod_inst() {
    int n = new_node(ND_MOD_INST);
    nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(cur)];   // module name
    // elab.cpp positions every hierarchy diagnostic (undefined module, bad
    // connection, recursion, ...) at nd_line[n]/nd_col[n]; new_node() zeroes
    // both, so without this the position is always 0:0 (Elaboration.md §4's
    // "bad_undef_inst.v:6:3: ..." example, golden/bad_undef_inst.v).
    nd_line[static_cast<std::size_t>(n)] = tok_line[static_cast<std::size_t>(cur)];
    nd_col[static_cast<std::size_t>(n)]  = tok_col[static_cast<std::size_t>(cur)];
    eat_tok();
    // optional parameter override  #( ... )
    if (cur_kind() == T_HASH) {
        eat_tok();
        expect(T_LPAREN, "expected '(' in parameter override");
        int ovh = 0, ovt = 0;
        if (cur_kind() != T_RPAREN) {
            bool more = true;
            while (more) {
                int ov;
                if (cur_kind() == T_DOT) {
                    eat_tok();
                    int i = cur;
                    expect(T_IDENT, "expected parameter name");
                    expect(T_LPAREN, "expected '('");
                    int e = parse_expr(0);
                    expect(T_RPAREN, "expected ')'");
                    ov = new_node(ND_PARAM_ASSIGN);
                    nd_name[static_cast<std::size_t>(ov)] = tok_text[static_cast<std::size_t>(i)]; nd_a[static_cast<std::size_t>(ov)] = e;
                } else {
                    int e = parse_expr(0);
                    ov = new_node(ND_PARAM_ASSIGN);
                    nd_a[static_cast<std::size_t>(ov)] = e;
                }
                if (ovh == 0) ovh = ov; else nd_next[static_cast<std::size_t>(ovt)] = ov;
                ovt = ov;
                if (cur_kind() == T_COMMA) eat_tok();
                else                       more = false;
            }
        }
        expect(T_RPAREN, "expected ')' after parameter override");
        nd_b[static_cast<std::size_t>(n)] = ovh;
    }
    // instance name
    int inst = 0;
    if (cur_kind() == T_IDENT) {
        inst = new_node(ND_IDENT);
        nd_name[static_cast<std::size_t>(inst)] = tok_text[static_cast<std::size_t>(cur)];
        eat_tok();
    } else
        expect(T_IDENT, "expected instance name");
    nd_c[static_cast<std::size_t>(n)] = inst;
    // connection list
    expect(T_LPAREN, "expected '(' in instantiation");
    int head = 0, tail = 0;
    if (cur_kind() != T_RPAREN) {
        bool more = true;
        while (more) {
            int c = parse_conn();
            if (head == 0) head = c; else nd_next[static_cast<std::size_t>(tail)] = c;
            tail = c;
            if (cur_kind() == T_COMMA) eat_tok();
            else                       more = false;
        }
    }
    expect(T_RPAREN, "expected ')' in instantiation");
    expect(T_SEMI, "expected ';' after instantiation");
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// ===========================================================================
//  PROCEDURAL:  always / initial / statements
// ===========================================================================

// event_expr ::= "*" | event_term {("or"|",") event_term}      (§4.1)
// Returns 0 for "@*" (spec §5.1: ND_ALWAYS.a == 0 means @*).
int Vsim::parse_event_expr() {
    expect(T_LPAREN, "expected '(' after '@'");
    if (cur_kind() == T_STAR) {
        eat_tok();
        expect(T_RPAREN, "expected ')' after '@(*'");
        return 0;
    }
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        Kind edge_kind = EDGE_NONE;
        if      (accept(K_POSEDGE)) edge_kind = EDGE_POS;
        else if (accept(K_NEGEDGE)) edge_kind = EDGE_NEG;
        int ex = parse_expr(0);
        int term = new_node(ND_EVENT_TERM);
        nd_a[static_cast<std::size_t>(term)] = ex; nd_op[static_cast<std::size_t>(term)] = edge_kind;
        if (head == 0) head = term; else nd_next[static_cast<std::size_t>(tail)] = term;
        tail = term;
        if (cur_kind() == K_OR || cur_kind() == T_COMMA) eat_tok();
        else                                             more = false;
    }
    expect(T_RPAREN, "expected ')' after event list");
    int n = new_node(ND_EVENT_EXPR);
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// always ::= "always" ["@" "(" event_expr ")"] statement
int Vsim::parse_always() {
    eat_tok();                                    // always
    int ev = 0;
    if (cur_kind() == T_AT) {
        eat_tok();
        ev = parse_event_expr();
    }
    int body = parse_statement();
    int n = new_node(ND_ALWAYS);
    nd_a[static_cast<std::size_t>(n)] = ev; nd_b[static_cast<std::size_t>(n)] = body;
    return n;
}

// initial ::= "initial" statement
int Vsim::parse_initial() {
    eat_tok();                                    // initial
    int body = parse_statement();
    int n = new_node(ND_INITIAL);
    nd_a[static_cast<std::size_t>(n)] = body;
    return n;
}

// blocking / nonblocking assignment. Which one is decided HERE by the "="
// vs "<=" token in statement position (§4.4).
int Vsim::parse_assign_stmt() {
    int lv = parse_lvalue();
    Kind op = cur_kind();
    if (op == T_ASSIGN) {
        eat_tok();
        int dly = opt_delay();
        int ex = parse_expr(0);
        expect(T_SEMI, "expected ';' after assignment");
        int n = new_node(ND_BLK_ASSIGN);
        nd_a[static_cast<std::size_t>(n)] = lv; nd_b[static_cast<std::size_t>(n)] = ex; nd_c[static_cast<std::size_t>(n)] = dly;
        return n;
    }
    if (op == T_LE) {
        eat_tok();
        int dly = opt_delay();
        int ex = parse_expr(0);
        expect(T_SEMI, "expected ';' after assignment");
        int n = new_node(ND_NB_ASSIGN);
        nd_a[static_cast<std::size_t>(n)] = lv; nd_b[static_cast<std::size_t>(n)] = ex; nd_c[static_cast<std::size_t>(n)] = dly;
        return n;
    }
    add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)],
             "expected '=' or '<=' in assignment");
    recover();
    return 0;
}

// assign_nosemi ::= lvalue "=" expr   (for-loop init/step, no ';')
int Vsim::parse_assign_nosemi() {
    int lv = parse_lvalue();
    expect(T_ASSIGN, "expected '=' in for-loop assignment");
    int ex = parse_expr(0);
    int n = new_node(ND_BLK_ASSIGN);
    nd_a[static_cast<std::size_t>(n)] = lv; nd_b[static_cast<std::size_t>(n)] = ex;
    return n;
}

// if ::= "if" "(" expr ")" statement ["else" statement]
// Dangling else binds to the nearest if: on seeing "else" we consume it in
// the current call rather than returning (§4.3).
int Vsim::parse_if() {
    eat_tok();                                    // if
    expect(T_LPAREN, "expected '(' after if");
    int cond = parse_expr(0);
    expect(T_RPAREN, "expected ')' after if condition");
    int thenS = parse_statement();
    int elseS = 0;
    if (cur_kind() == K_ELSE) {
        eat_tok();
        elseS = parse_statement();
    }
    int n = new_node(ND_IF);
    nd_a[static_cast<std::size_t>(n)] = cond; nd_b[static_cast<std::size_t>(n)] = thenS; nd_c[static_cast<std::size_t>(n)] = elseS;
    return n;
}

// case ::= ("case"|"casex"|"casez") "(" expr ")" case_item+ "endcase"
int Vsim::parse_case() {
    Kind ck = cur_kind();
    eat_tok();                                    // case/casex/casez
    expect(T_LPAREN, "expected '(' after case");
    int sel = parse_expr(0);
    expect(T_RPAREN, "expected ')' after case selector");
    int head = 0, tail = 0;
    while (cur_kind() != K_ENDCASE && cur_kind() != T_EOF) {
        int mh = 0, mt = 0;
        if (accept(K_DEFAULT)) {
            accept(T_COLON);                      // colon optional after default
        } else {
            bool more = true;
            while (more) {
                int ex = parse_expr(0);
                if (mh == 0) mh = ex; else nd_next[static_cast<std::size_t>(mt)] = ex;
                mt = ex;
                if (cur_kind() == T_COMMA) eat_tok();
                else                       more = false;
            }
            expect(T_COLON, "expected ':' in case item");
        }
        int st = parse_statement();
        int item = new_node(ND_CASE_ITEM);
        nd_a[static_cast<std::size_t>(item)] = mh; nd_b[static_cast<std::size_t>(item)] = st;   // mh==0 => default
        if (head == 0) head = item; else nd_next[static_cast<std::size_t>(tail)] = item;
        tail = item;
    }
    expect(K_ENDCASE, "expected 'endcase'");
    int n = new_node(ND_CASE);
    nd_a[static_cast<std::size_t>(n)] = sel; nd_b[static_cast<std::size_t>(n)] = head; nd_op[static_cast<std::size_t>(n)] = ck;
    return n;
}

// seq_block ::= "begin" [":" IDENT] {reg_decl|param_decl|integer} {stmt} "end"
int Vsim::parse_seq_block() {
    eat_tok();                                    // begin
    std::string lbl;
    if (cur_kind() == T_COLON) {
        eat_tok();
        if (cur_kind() == T_IDENT) { lbl = tok_text[static_cast<std::size_t>(cur)]; eat_tok(); }
        else expect(T_IDENT, "expected block label");
    }
    int dh = 0, dt = 0;
    while (cur_kind() == K_REG || cur_kind() == K_PARAMETER || cur_kind() == K_INTEGER) {
        int d;
        if      (cur_kind() == K_REG)       d = parse_reg_decl();
        else if (cur_kind() == K_PARAMETER) d = parse_param_decl();
        else                                 d = parse_integer_decl();
        if (d != 0) {
            if (dh == 0) dh = d; else nd_next[static_cast<std::size_t>(dt)] = d;
            dt = chain_tail(d);
        }
    }
    int sh = 0, st = 0;
    while (cur_kind() != K_END && cur_kind() != T_EOF) {
        int prev = cur;
        int s = parse_statement();
        if (s != 0) {
            if (sh == 0) sh = s; else nd_next[static_cast<std::size_t>(st)] = s;
            st = s;
        }
        if (cur == prev && cur_kind() != K_END && cur_kind() != T_EOF)
            eat_tok();                            // guarantee progress
    }
    expect(K_END, "expected 'end'");
    int n = new_node(ND_SEQ_BLOCK);
    nd_name[static_cast<std::size_t>(n)] = lbl; nd_a[static_cast<std::size_t>(n)] = dh; nd_b[static_cast<std::size_t>(n)] = sh;
    return n;
}

// loop ::= for(...) stmt | while(...) stmt | repeat(...) stmt
int Vsim::parse_loop() {
    Kind lk = cur_kind();
    eat_tok();
    int init = 0, cond = 0, step = 0;
    expect(T_LPAREN, "expected '(' in loop");
    if (lk == K_FOR) {
        init = parse_assign_nosemi();
        expect(T_SEMI, "expected ';' in for-loop");
        cond = parse_expr(0);
        expect(T_SEMI, "expected ';' in for-loop");
        step = parse_assign_nosemi();
    } else {                                      // while / repeat: single expr
        cond = parse_expr(0);
    }
    expect(T_RPAREN, "expected ')' in loop");
    int body = parse_statement();
    int n = new_node(ND_LOOP);
    nd_op[static_cast<std::size_t>(n)] = lk; nd_a[static_cast<std::size_t>(n)] = init; nd_b[static_cast<std::size_t>(n)] = cond;
    nd_c[static_cast<std::size_t>(n)] = step; nd_d[static_cast<std::size_t>(n)] = body;
    return n;
}

// statement dispatch (§4.1)
int Vsim::parse_statement() {
    switch (cur_kind()) {
        case T_SEMI:  eat_tok(); return new_node(ND_NULL_STMT);
        case K_BEGIN: return parse_seq_block();
        case K_IF:    return parse_if();
        case K_CASE: case K_CASEX: case K_CASEZ:
                      return parse_case();
        case K_FOR: case K_WHILE: case K_REPEAT:
                      return parse_loop();
        case T_IDENT: case T_LBRACE:
                      return parse_assign_stmt();
        default:
            add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)], "expected statement");
            recover();
            return 0;
    }
}

// ===========================================================================
//  GENERATE  (§4.1 -- basic support; none of the §10 circuits require it)
// ===========================================================================
int Vsim::parse_gen_block() {                     // "begin [:lbl] items end" | item
    if (cur_kind() == K_BEGIN) {
        eat_tok();
        if (cur_kind() == T_COLON) {
            eat_tok();
            accept(T_IDENT);
        }
        int head = 0, tail = 0;
        while (cur_kind() != K_END && cur_kind() != T_EOF) {
            int prev = cur;
            int it = parse_module_item();
            if (it != 0) {
                if (head == 0) head = it; else nd_next[static_cast<std::size_t>(tail)] = it;
                tail = chain_tail(it);
            }
            if (cur == prev && cur_kind() != K_END) eat_tok();
        }
        expect(K_END, "expected 'end' in generate block");
        int n = new_node(ND_GENERATE);
        nd_a[static_cast<std::size_t>(n)] = head;
        return n;
    }
    return parse_module_item();
}

// generate ::= "generate" generate_item* "endgenerate"
int Vsim::parse_generate() {
    eat_tok();                                    // generate
    int head = 0, tail = 0;
    while (cur_kind() != K_ENDGENERATE && cur_kind() != T_EOF) {
        int prev = cur;
        int it;
        if (cur_kind() == K_FOR) {
            eat_tok();
            expect(T_LPAREN, "expected '(' in generate for");
            parse_assign_nosemi();
            expect(T_SEMI, "expected ';'");
            parse_expr(0);
            expect(T_SEMI, "expected ';'");
            parse_assign_nosemi();
            expect(T_RPAREN, "expected ')'");
            it = parse_gen_block();
        } else if (cur_kind() == K_IF) {
            eat_tok();
            expect(T_LPAREN, "expected '('");
            parse_expr(0);
            expect(T_RPAREN, "expected ')'");
            it = parse_gen_block();
            if (cur_kind() == K_ELSE) { eat_tok(); parse_gen_block(); }
        } else
            it = parse_module_item();
        if (it != 0) {
            if (head == 0) head = it; else nd_next[static_cast<std::size_t>(tail)] = it;
            tail = chain_tail(it);
        }
        if (cur == prev && cur_kind() != K_ENDGENERATE) eat_tok();
    }
    expect(K_ENDGENERATE, "expected 'endgenerate'");
    int n = new_node(ND_GENERATE);
    nd_a[static_cast<std::size_t>(n)] = head;
    return n;
}

// ===========================================================================
//  MODULE STRUCTURE
// ===========================================================================

// one ANSI port: direction [wire|reg] [signed] [range] IDENT
int Vsim::parse_ansi_port() {
    Kind dir = dir_code(cur_kind());
    eat_tok();
    std::uint16_t nt = NT_NONE;
    if      (accept(K_WIRE)) nt = NT_WIRE;
    else if (accept(K_REG))  nt = NT_REG;
    bool sgn = opt_signed();
    int rng = opt_range();
    int n = new_node(ND_PORT);
    nd_op[static_cast<std::size_t>(n)] = dir; nd_a[static_cast<std::size_t>(n)] = rng; nd_b[static_cast<std::size_t>(n)] = nt;
    nd_signed[static_cast<std::size_t>(n)] = sgn;
    if (cur_kind() == T_IDENT) { nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(cur)]; eat_tok(); }
    else expect(T_IDENT, "expected port name");
    return n;
}

// port_list ::= "(" [port {"," port}] ")". ANSI vs non-ANSI decided by the
// first port token (§4.3): a direction keyword => ANSI, a bare IDENT =>
// non-ANSI (directions arrive later via body port_decls).
int Vsim::parse_ports() {
    if (cur_kind() != T_LPAREN) return 0;
    eat_tok();                                    // (
    if (cur_kind() == T_RPAREN) { eat_tok(); return 0; }
    bool ansi = (cur_kind() == K_INPUT || cur_kind() == K_OUTPUT || cur_kind() == K_INOUT);
    int head = 0, tail = 0;
    bool more = true;
    while (more) {
        int p;
        if (ansi)
            p = parse_ansi_port();
        else if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            p = new_node(ND_PORT);
            nd_name[static_cast<std::size_t>(p)] = tok_text[static_cast<std::size_t>(i)]; nd_op[static_cast<std::size_t>(p)] = DIR_NONE;
        } else {
            expect(T_IDENT, "expected port");
            p = 0;
        }
        if (p != 0) {
            if (head == 0) head = p; else nd_next[static_cast<std::size_t>(tail)] = p;
            tail = p;
        }
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_RPAREN, "expected ')' after port list");
    return head;
}

// param_port_list ::= "#" "(" param_assign {"," param_assign} ")"
int Vsim::parse_param_port_list() {
    eat_tok();                                    // #
    expect(T_LPAREN, "expected '(' in parameter port list");
    int head = 0, tail = 0;
    bool more = true;
    while (more && cur_kind() != T_RPAREN && cur_kind() != T_EOF) {
        accept(K_PARAMETER);                      // optional
        opt_signed();
        opt_range();
        if (cur_kind() == T_IDENT) {
            int i = eat_tok();
            expect(T_ASSIGN, "expected '=' in parameter");
            int val = parse_expr(0);
            int n = new_node(ND_PARAM);
            nd_name[static_cast<std::size_t>(n)] = tok_text[static_cast<std::size_t>(i)]; nd_a[static_cast<std::size_t>(n)] = val;
            if (head == 0) head = n; else nd_next[static_cast<std::size_t>(tail)] = n;
            tail = n;
        } else
            expect(T_IDENT, "expected parameter name");
        if (cur_kind() == T_COMMA) eat_tok();
        else                       more = false;
    }
    expect(T_RPAREN, "expected ')' after parameter port list");
    return head;
}

// module_item dispatch. A bare IDENT can only begin a module instantiation --
// every other alternative starts with a keyword or gate primitive, so this
// is LL(1) (§4.3). Returns 0 for body port_decls (they annotate header ports).
int Vsim::parse_module_item() {
    switch (cur_kind()) {
        case K_WIRE:      return parse_net_decl();
        case K_REG:       return parse_reg_decl();
        case K_INTEGER:   return parse_integer_decl();
        case K_INPUT: case K_OUTPUT: case K_INOUT:
                           return parse_port_decl();
        case K_PARAMETER: return parse_param_decl();
        case K_ASSIGN:    return parse_cont_assign();
        case K_ALWAYS:    return parse_always();
        case K_INITIAL:   return parse_initial();
        case K_GENERATE:  return parse_generate();
        case T_IDENT:     return parse_mod_inst();
        default:
            if (is_gate_kw(cur_kind()))
                return parse_gate_inst();
            add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)], "expected module item");
            recover();
            return 0;
    }
}

// module_decl ::= "module" IDENT [param_port_list] [port_list] ";"
//                 module_item* "endmodule"
int Vsim::parse_module() {
    eat_tok();                                    // module
    int m = new_node(ND_MODULE);
    if (cur_kind() == T_IDENT) {
        nd_name[static_cast<std::size_t>(m)] = tok_text[static_cast<std::size_t>(cur)];
        // same reason as parse_mod_inst() above: elab.cpp's "duplicate
        // definition of module" diagnostic reads nd_line[m]/nd_col[m].
        nd_line[static_cast<std::size_t>(m)] = tok_line[static_cast<std::size_t>(cur)];
        nd_col[static_cast<std::size_t>(m)]  = tok_col[static_cast<std::size_t>(cur)];
        eat_tok();
    }
    else expect(T_IDENT, "expected module name");

    int params = (cur_kind() == T_HASH) ? parse_param_port_list() : 0;
    nd_c[static_cast<std::size_t>(m)] = params;

    int ports = parse_ports();
    nd_a[static_cast<std::size_t>(m)] = ports;
    cur_mod_ports = ports;                        // enable non-ANSI back-patching

    expect(T_SEMI, "expected ';' after module header");

    int head = 0, tail = 0;
    while (cur_kind() != K_ENDMODULE && cur_kind() != T_EOF) {
        int prev = cur;
        int it = parse_module_item();
        if (it != 0) {
            if (head == 0) head = it; else nd_next[static_cast<std::size_t>(tail)] = it;
            tail = chain_tail(it);
        }
        if (cur == prev && cur_kind() != K_ENDMODULE && cur_kind() != T_EOF)
            eat_tok();                            // guarantee progress
    }
    nd_b[static_cast<std::size_t>(m)] = head;
    expect(K_ENDMODULE, "expected 'endmodule'");
    return m;
}

// source_text ::= module_decl* EOF. Returns the head of the module chain.
int Vsim::parse_source() {
    cur = 0;
    int head = 0, tail = 0;
    bool stop = false;
    while (!stop && cur_kind() != T_EOF && !arena_err) {
        int prev = cur;
        if (cur_kind() == K_MODULE) {
            int m = parse_module();
            if (m != 0) {
                if (head == 0) head = m; else nd_next[static_cast<std::size_t>(tail)] = m;
                tail = m;
            }
        } else {
            add_diag(SEV_ERROR, tok_line[static_cast<std::size_t>(cur)], tok_col[static_cast<std::size_t>(cur)], "expected 'module'");
            eat_tok();
        }
        if (cur == prev && cur_kind() != T_EOF) eat_tok();
        if (n_diag >= MAX_DIAG) stop = true;       // stop after MAX_DIAG
    }
    return head;
}

} // namespace vsim
