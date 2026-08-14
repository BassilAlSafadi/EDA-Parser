//============================================================================
// dump.cpp -- ported from vsim_dump.v (§6, §8): deterministic token and AST
// dumps. Two dumps drive the golden tests:
//   * dump_tokens : one line "LINE:COL KIND lexeme" per token (§8).
//   * dump_ast    : an S-expression walk of the AST (§6).
// The AST walk OMITS line/column, so golden files stay stable under target
// reformatting (§6). Output goes to an explicit std::ostream& parameter
// instead of the original's module-level `dfd` file descriptor -- same role,
// idiomatic C++ spelling.
//============================================================================
#include "vsim.hpp"

#include <ostream>

namespace vsim {

namespace {
void sp(std::ostream& o, int n) {                 // emit n spaces
    for (int k = 0; k < n; ++k) o << ' ';
}
} // namespace

// ---------------------------------------------------------------- name tables
std::string Vsim::tok_kind_name(Kind k) {
    switch (k) {
        case T_EOF:      return "EOF";
        case T_IDENT:    return "IDENT";
        case T_NUMBER:   return "NUMBER";
        case T_LBRACK:   return "LBRACK";
        case T_RBRACK:   return "RBRACK";
        case T_LPAREN:   return "LPAREN";
        case T_RPAREN:   return "RPAREN";
        case T_LBRACE:   return "LBRACE";
        case T_RBRACE:   return "RBRACE";
        case T_SEMI:     return "SEMI";
        case T_COMMA:    return "COMMA";
        case T_COLON:    return "COLON";
        case T_HASH:     return "HASH";
        case T_AT:       return "AT";
        case T_DOT:      return "DOT";
        case T_ASSIGN:   return "ASSIGN";
        case T_LE:       return "LE";
        case T_EQ:       return "EQ";
        case T_CASE_EQ:  return "CASEEQ";
        case T_NEQ:      return "NEQ";
        case T_CASE_NEQ: return "CASENEQ";
        case T_LT:       return "LT";
        case T_GT:       return "GT";
        case T_GE:       return "GE";
        case T_SHL:      return "SHL";
        case T_SHR:      return "SHR";
        case T_LAND:     return "LAND";
        case T_LOR:      return "LOR";
        case T_NOT:      return "NOT";
        case T_BNOT:     return "BNOT";
        case T_AMP:      return "AMP";
        case T_PIPE:     return "PIPE";
        case T_CARET:    return "CARET";
        case T_XNORT:    return "XNOR";
        case T_NANDT:    return "RNAND";
        case T_NORT:     return "RNOR";
        case T_PLUS:     return "PLUS";
        case T_MINUS:    return "MINUS";
        case T_STAR:     return "STAR";
        case T_SLASH:    return "SLASH";
        case T_PCT:      return "PCT";
        case T_QUES:     return "QUES";
        default:         return "KW";      // keyword: text carries detail
    }
}

std::string Vsim::opsym(Kind op) {                 // binary/unary operator glyph
    switch (op) {
        case T_PLUS:     return "+";   case T_MINUS: return "-";
        case T_STAR:     return "*";   case T_SLASH: return "/"; case T_PCT: return "%";
        case T_AMP:      return "&";   case T_PIPE:  return "|"; case T_CARET: return "^";
        case T_XNORT:    return "~^";  case T_NANDT: return "~&"; case T_NORT: return "~|";
        case T_BNOT:     return "~";   case T_NOT:   return "!";
        case T_LAND:     return "&&";  case T_LOR:   return "||";
        case T_EQ:       return "==";  case T_NEQ:   return "!=";
        case T_CASE_EQ:  return "==="; case T_CASE_NEQ: return "!==";
        case T_LT:       return "<";   case T_LE:    return "<=";
        case T_GT:       return ">";   case T_GE:    return ">=";
        case T_SHL:      return "<<";  case T_SHR:   return ">>";
        default:         return "?";
    }
}

std::string Vsim::gate_name(Kind g) {
    switch (g) {
        case K_AND:  return "and";   case K_OR:   return "or";
        case K_NOT:  return "not";   case K_NAND: return "nand";
        case K_NOR:  return "nor";   case K_XOR:  return "xor";
        case K_XNOR: return "xnor";  case K_BUF:  return "buf";
        default:     return "?";
    }
}

std::string Vsim::dir_name(Kind d) {
    switch (d) {
        case DIR_INPUT:  return "input";
        case DIR_OUTPUT: return "output";
        case DIR_INOUT:  return "inout";
        default:         return "port";
    }
}

std::string Vsim::case_name(Kind c) {
    switch (c) {
        case K_CASEX: return "casex";
        case K_CASEZ: return "casez";
        default:      return "case";
    }
}

std::string Vsim::loop_name(Kind c) {
    switch (c) {
        case K_WHILE:  return "while";
        case K_REPEAT: return "repeat";
        default:       return "for";
    }
}

std::string Vsim::edge_name(Kind e) {
    switch (e) {
        case EDGE_POS: return "posedge";
        case EDGE_NEG: return "negedge";
        default:       return "level";
    }
}

// print the low `width` bits of a four-state value as width'bXXXX (§9.8 form)
void Vsim::dump_val(std::ostream& o, const FourState& val, int width) {
    int w = width;
    if (w <= 0 || w > VALW) w = 32;
    // any x/z bit forces binary form so four-state values read back exactly
    // (§9.8); otherwise decimal is far more legible in golden dumps.
    if (val.hasUnknown(w))
        o << w << "'b" << val.toBitString(w);
    else
        o << w << "'d" << val.toUint(w);
}

// ---------------------------------------------------------------------------
// dexpr -- print an expression inline (no indent, no newline). Ranges and
// event expressions are expressions here too.
// ---------------------------------------------------------------------------
void Vsim::dexpr(std::ostream& o, int h) const {
    if (h == 0) { o << "()"; return; }
    switch (nd_kind[static_cast<std::size_t>(h)]) {
        case ND_IDENT:
            o << "(ND_IDENT \"" << nd_name[static_cast<std::size_t>(h)] << "\")";
            break;
        case ND_LITERAL:
            o << "(ND_LITERAL ";
            dump_val(o, nd_value[static_cast<std::size_t>(h)], nd_width[static_cast<std::size_t>(h)]);
            o << ")";
            break;
        case ND_BINARY:
            o << "(ND_BINARY " << opsym(nd_op[static_cast<std::size_t>(h)]) << " ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_UNARY:
            o << "(ND_UNARY " << opsym(nd_op[static_cast<std::size_t>(h)]) << " ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_TERNARY:
            o << "(ND_TERNARY ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << " ";
            dexpr(o, nd_c[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_BIT_SELECT:
            o << "(ND_BIT_SELECT ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_PART_SELECT:
            o << "(ND_PART_SELECT ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << " ";
            dexpr(o, nd_c[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_CONCAT: {
            o << "(ND_CONCAT";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) { o << " "; dexpr(o, c); c = nd_next[static_cast<std::size_t>(c)]; }
            o << ")";
            break;
        }
        case ND_REPLICATION:
            o << "(ND_REPLICATION ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_RANGE:
            o << "["; dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << ":";
            dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << "]";
            break;
        case ND_CONN:
            o << "(ND_CONN \"" << nd_name[static_cast<std::size_t>(h)] << "\" ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_EVENT_EXPR: {
            o << "(ND_EVENT_EXPR";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) { o << " "; dexpr(o, c); c = nd_next[static_cast<std::size_t>(c)]; }
            o << ")";
            break;
        }
        case ND_EVENT_TERM:
            o << "(ND_EVENT_TERM " << edge_name(nd_op[static_cast<std::size_t>(h)]) << " ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << ")";
            break;
        default:
            o << "(?expr " << static_cast<int>(nd_kind[static_cast<std::size_t>(h)]) << ")";
            break;
    }
}

// event control of an always block, inline:  @*  or the event-expr S-expr
void Vsim::devent(std::ostream& o, int h) const {
    if (h == 0) o << "@*";
    else        dexpr(o, h);
}

// ---------------------------------------------------------------------------
// dstmt -- print a statement / module item with leading indent, possibly
// over several lines, with NO trailing newline (the caller adds separators).
// ---------------------------------------------------------------------------
void Vsim::dstmt(std::ostream& o, int h, int ind) const {
    if (h == 0) { sp(o, ind); o << "()"; return; }
    switch (nd_kind[static_cast<std::size_t>(h)]) {
        case ND_NET:
            sp(o, ind); o << "(ND_NET \"" << nd_name[static_cast<std::size_t>(h)] << "\"";
            if (nd_a[static_cast<std::size_t>(h)] != 0) { o << " "; dexpr(o, nd_a[static_cast<std::size_t>(h)]); }
            o << ")";
            break;
        case ND_REG:
            sp(o, ind); o << "(ND_REG \"" << nd_name[static_cast<std::size_t>(h)] << "\"";
            if (nd_a[static_cast<std::size_t>(h)] != 0) { o << " "; dexpr(o, nd_a[static_cast<std::size_t>(h)]); }
            if (nd_b[static_cast<std::size_t>(h)] != 0) { o << " mem="; dexpr(o, nd_b[static_cast<std::size_t>(h)]); }
            o << ")";
            break;
        case ND_PARAM:
            sp(o, ind); o << "(ND_PARAM \"" << nd_name[static_cast<std::size_t>(h)] << "\" ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_CONT_ASSIGN:
            sp(o, ind); o << "(ND_CONT_ASSIGN ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_BLK_ASSIGN:
            sp(o, ind); o << "(ND_BLK_ASSIGN ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_NB_ASSIGN:
            sp(o, ind); o << "(ND_NB_ASSIGN ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
            break;
        case ND_NULL_STMT:
            sp(o, ind); o << "(ND_NULL_STMT)";
            break;
        case ND_GATE_INST: {
            sp(o, ind); o << "(ND_GATE_INST " << gate_name(nd_op[static_cast<std::size_t>(h)]) << " \""
                          << nd_name[static_cast<std::size_t>(h)] << "\" (";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) {
                dexpr(o, c); if (nd_next[static_cast<std::size_t>(c)] != 0) o << " "; c = nd_next[static_cast<std::size_t>(c)];
            }
            o << "))";
            break;
        }
        case ND_MOD_INST: {
            sp(o, ind); o << "(ND_MOD_INST \"" << nd_name[static_cast<std::size_t>(h)] << "\" \"";
            if (nd_c[static_cast<std::size_t>(h)] != 0) o << nd_name[static_cast<std::size_t>(nd_c[static_cast<std::size_t>(h)])];
            o << "\" (";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) {
                dexpr(o, c); if (nd_next[static_cast<std::size_t>(c)] != 0) o << " "; c = nd_next[static_cast<std::size_t>(c)];
            }
            o << "))";
            break;
        }
        case ND_ALWAYS:
            sp(o, ind); o << "(ND_ALWAYS "; devent(o, nd_a[static_cast<std::size_t>(h)]); o << "\n";
            dstmt(o, nd_b[static_cast<std::size_t>(h)], ind + 2); o << ")";
            break;
        case ND_INITIAL:
            sp(o, ind); o << "(ND_INITIAL\n";
            dstmt(o, nd_a[static_cast<std::size_t>(h)], ind + 2); o << ")";
            break;
        case ND_SEQ_BLOCK: {
            sp(o, ind); o << "(ND_SEQ_BLOCK \"" << nd_name[static_cast<std::size_t>(h)] << "\"";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) { o << "\n"; dstmt(o, c, ind + 2); c = nd_next[static_cast<std::size_t>(c)]; }
            c = nd_b[static_cast<std::size_t>(h)];
            while (c != 0) { o << "\n"; dstmt(o, c, ind + 2); c = nd_next[static_cast<std::size_t>(c)]; }
            o << ")";
            break;
        }
        case ND_IF:
            sp(o, ind); o << "(ND_IF "; dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << "\n";
            dstmt(o, nd_b[static_cast<std::size_t>(h)], ind + 2);
            if (nd_c[static_cast<std::size_t>(h)] != 0) { o << "\n"; dstmt(o, nd_c[static_cast<std::size_t>(h)], ind + 2); }
            o << ")";
            break;
        case ND_CASE: {
            sp(o, ind); o << "(ND_CASE " << case_name(nd_op[static_cast<std::size_t>(h)]) << " ";
            dexpr(o, nd_a[static_cast<std::size_t>(h)]);
            int c = nd_b[static_cast<std::size_t>(h)];
            while (c != 0) { o << "\n"; dstmt(o, c, ind + 2); c = nd_next[static_cast<std::size_t>(c)]; }
            o << ")";
            break;
        }
        case ND_CASE_ITEM: {
            sp(o, ind); o << "(ND_CASE_ITEM ";
            if (nd_a[static_cast<std::size_t>(h)] == 0) o << "default";
            else {
                o << "(";
                int c = nd_a[static_cast<std::size_t>(h)];
                while (c != 0) { dexpr(o, c); if (nd_next[static_cast<std::size_t>(c)] != 0) o << " "; c = nd_next[static_cast<std::size_t>(c)]; }
                o << ")";
            }
            o << "\n"; dstmt(o, nd_b[static_cast<std::size_t>(h)], ind + 2); o << ")";
            break;
        }
        case ND_LOOP:
            sp(o, ind); o << "(ND_LOOP " << loop_name(nd_op[static_cast<std::size_t>(h)]) << " ";
            if (nd_a[static_cast<std::size_t>(h)] != 0) { dexpr_stmt(o, nd_a[static_cast<std::size_t>(h)]); o << " "; }
            dexpr(o, nd_b[static_cast<std::size_t>(h)]);
            if (nd_c[static_cast<std::size_t>(h)] != 0) { o << " "; dexpr_stmt(o, nd_c[static_cast<std::size_t>(h)]); }
            o << "\n"; dstmt(o, nd_d[static_cast<std::size_t>(h)], ind + 2); o << ")";
            break;
        case ND_GENERATE: {
            sp(o, ind); o << "(ND_GENERATE";
            int c = nd_a[static_cast<std::size_t>(h)];
            while (c != 0) { o << "\n"; dstmt(o, c, ind + 2); c = nd_next[static_cast<std::size_t>(c)]; }
            o << ")";
            break;
        }
        default:
            sp(o, ind); o << "(?stmt " << static_cast<int>(nd_kind[static_cast<std::size_t>(h)]) << ")";
            break;
    }
}

// a bare assignment used as a for-loop init/step, printed inline
void Vsim::dexpr_stmt(std::ostream& o, int h) const {
    if (h != 0 && nd_kind[static_cast<std::size_t>(h)] == ND_BLK_ASSIGN) {
        o << "(ND_BLK_ASSIGN ";
        dexpr(o, nd_a[static_cast<std::size_t>(h)]); o << " "; dexpr(o, nd_b[static_cast<std::size_t>(h)]); o << ")";
    } else
        dexpr(o, h);
}

// one port line
void Vsim::dport(std::ostream& o, int p, int ind) const {
    sp(o, ind); o << "(ND_PORT \"" << nd_name[static_cast<std::size_t>(p)] << "\" " << dir_name(nd_op[static_cast<std::size_t>(p)]);
    if (nd_a[static_cast<std::size_t>(p)] != 0) { o << " "; dexpr(o, nd_a[static_cast<std::size_t>(p)]); }
    o << ")";
}

// ---------------------------------------------------------------------------
// dump_module -- the top-level S-expression for one module (§6 layout).
// ---------------------------------------------------------------------------
void Vsim::dump_module(std::ostream& o, int m) const {
    o << "(ND_MODULE \"" << nd_name[static_cast<std::size_t>(m)] << "\"";
    // ports
    o << "\n"; sp(o, 2); o << "(ports";
    if (nd_a[static_cast<std::size_t>(m)] == 0) o << ")";
    else {
        int p = nd_a[static_cast<std::size_t>(m)];
        while (p != 0) { o << "\n"; dport(o, p, 4); p = nd_next[static_cast<std::size_t>(p)]; }
        o << ")";
    }
    // items
    o << "\n"; sp(o, 2); o << "(items";
    if (nd_b[static_cast<std::size_t>(m)] == 0) o << ")";
    else {
        int it = nd_b[static_cast<std::size_t>(m)];
        while (it != 0) { o << "\n"; dstmt(o, it, 4); it = nd_next[static_cast<std::size_t>(it)]; }
        o << ")";
    }
    o << ")\n";
}

void Vsim::dump_ast(std::ostream& o, int root) const {
    int m = root;
    while (m != 0) { dump_module(o, m); m = nd_next[static_cast<std::size_t>(m)]; }
}

// ---------------------------------------------------------------------------
// dump_tokens -- "LINE:COL KIND lexeme" per token (§8).
// ---------------------------------------------------------------------------
void Vsim::dump_tokens(std::ostream& o) const {
    for (int i = 0; i < n_tok; ++i) {
        Kind k = tok_kind[static_cast<std::size_t>(i)];
        o << tok_line[static_cast<std::size_t>(i)] << ":" << tok_col[static_cast<std::size_t>(i)] << " "
          << tok_kind_name(k) << " ";
        if (k == T_NUMBER) {
            dump_val(o, tok_value[static_cast<std::size_t>(i)], tok_width[static_cast<std::size_t>(i)]);
        } else if (k == T_IDENT || k >= 64) {      // ident or keyword
            o << tok_text[static_cast<std::size_t>(i)];
        }
        o << "\n";
    }
}

} // namespace vsim
