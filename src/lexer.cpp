//============================================================================
// lexer.cpp -- ported from vsim_lexer.v (§3): host scanner over the target
// byte arena. Loads the whole target file into `src` first, then scans in
// memory. The scanner is a DFA family: identifiers/keywords (§3.3),
// whitespace & comments (§3.4), four-state numeric literals (§3.5),
// maximal-munch operators (§3.6).
//============================================================================
#include "vsim.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace vsim {

// ---------------------------------------------------------------------------
// load_src -- read the entire target file into the byte arena (§3.1).
// ---------------------------------------------------------------------------
void Vsim::load_src(const std::string& filename) {
    arena_err = false;
    had_error = false;
    n_tok    = 0;
    n_node   = 1;           // handle 0 reserved as NULL (§2.5)
    n_diag   = 0;
    node_cap = MAX_NODES;
    src_len  = 0;

    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        std::cerr << "FATAL: cannot open " << filename << "\n";
        std::exit(1);
    }
    auto ch = f.get();
    while (ch != std::char_traits<char>::eof() && src_len < MAX_SRC) {
        src[static_cast<std::size_t>(src_len)] = static_cast<std::uint8_t>(ch);
        src_len = src_len + 1;
        ch = f.get();
    }
    if (ch != std::char_traits<char>::eof()) {
        add_diag(SEV_ERROR, 0, 0, "source exceeds MAX_SRC bytes");
        arena_err = true;
    }
}

// ---------------------------------------------------------------------------
// load_str -- populate the source arena directly from an in-memory string
// (used by the tests for self-contained snippets). The original packed a
// fixed-width string literal for this; a std::string carries its own length
// so no packing/auto-length-detection is needed here.
// ---------------------------------------------------------------------------
void Vsim::load_str(const std::string& s) {
    arena_err = false;
    had_error = false;
    n_tok = 0; n_node = 1; n_diag = 0; node_cap = MAX_NODES;
    src_len = static_cast<int>(std::min<std::size_t>(s.size(), static_cast<std::size_t>(MAX_SRC)));
    for (int k = 0; k < src_len; ++k) src[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(s[static_cast<std::size_t>(k)]);
}

// current char (0 past EOF)
std::uint8_t Vsim::peekc(int off) const {
    return (lp + off < src_len) ? src[static_cast<std::size_t>(lp + off)] : 0;
}

// consume one src byte, tracking line/column (CR left column-neutral so
// CRLF files column-count like LF files, §3.4).
void Vsim::adv() {
    if (src[static_cast<std::size_t>(lp)] == 0x0A)      { lln = lln + 1; lcol = 1; }
    else if (src[static_cast<std::size_t>(lp)] == 0x0D) { /* no column change */ }
    else                                                  lcol = lcol + 1;
    lp = lp + 1;
}

// ---------------------------------------------------------------------------
// push_tok -- append the token currently described by the t_* scratch
// members. Overflowing the token arena is a loud failure, not corruption
// (§9.10).
// ---------------------------------------------------------------------------
void Vsim::push_tok(Kind k) {
    if (n_tok >= MAX_TOK) {
        if (!arena_err)
            std::cerr << "FATAL: token arena exhausted (MAX_TOK=" << MAX_TOK << ")\n";
        arena_err = true;
    } else {
        tok_kind[static_cast<std::size_t>(n_tok)]   = k;
        tok_text[static_cast<std::size_t>(n_tok)]   = t_text;
        tok_value[static_cast<std::size_t>(n_tok)]  = t_value;
        tok_width[static_cast<std::size_t>(n_tok)]  = t_width;
        tok_signed[static_cast<std::size_t>(n_tok)] = t_signed;
        tok_line[static_cast<std::size_t>(n_tok)]   = tln;
        tok_col[static_cast<std::size_t>(n_tok)]    = tcol;
        n_tok = n_tok + 1;
    }
    t_text.clear(); t_value = FourState(); t_width = 0; t_signed = false;
}

// ---------------------------------------------------------------------------
// kw_lookup -- reclassify a scanned identifier as a keyword or T_IDENT
// (§3.3). Keywords must NOT be scanner states -- they are a post-scan table
// lookup.
// ---------------------------------------------------------------------------
Kind Vsim::kw_lookup(const std::string& s) {
    static const std::unordered_map<std::string, Kind> table = {
        {"module", K_MODULE},     {"endmodule", K_ENDMODULE}, {"input", K_INPUT},
        {"output", K_OUTPUT},     {"inout", K_INOUT},         {"wire", K_WIRE},
        {"reg", K_REG},           {"parameter", K_PARAMETER}, {"always", K_ALWAYS},
        {"initial", K_INITIAL},   {"assign", K_ASSIGN},       {"begin", K_BEGIN},
        {"end", K_END},           {"if", K_IF},               {"else", K_ELSE},
        {"case", K_CASE},         {"casex", K_CASEX},         {"casez", K_CASEZ},
        {"endcase", K_ENDCASE},   {"default", K_DEFAULT},     {"for", K_FOR},
        {"while", K_WHILE},       {"repeat", K_REPEAT},       {"posedge", K_POSEDGE},
        {"negedge", K_NEGEDGE},   {"or", K_OR},                {"signed", K_SIGNED},
        {"integer", K_INTEGER},   {"generate", K_GENERATE},   {"endgenerate", K_ENDGENERATE},
        {"and", K_AND},           {"not", K_NOT},              {"nand", K_NAND},
        {"nor", K_NOR},           {"xor", K_XOR},              {"xnor", K_XNOR},
        {"buf", K_BUF},
    };
    auto it = table.find(s);
    return it != table.end() ? it->second : T_IDENT;
}

// valid value digit for a based literal (x/z/? always accepted, §3.5)
bool Vsim::valid_base_digit(std::uint8_t c, std::uint8_t base) {
    std::uint8_t lcc = lc(c);
    if (lcc == 'x' || lcc == 'z' || c == '?') return true;
    switch (base) {
        case 'b': return c == '0' || c == '1';
        case 'o': return c >= '0' && c <= '7';
        case 'd': return is_digit(c);
        case 'h': return is_hex(c);
        default:  return false;
    }
}

// ---------------------------------------------------------------------------
// skip_ws_comments -- consume whitespace, // line comments and /* */ block
// comments (§3.4). `/` alone is left for the operator scanner (divide), the
// classic divide-vs-comment case resolved by looking at the following char.
// ---------------------------------------------------------------------------
void Vsim::skip_ws_comments() {
    bool progressed = true;
    while (progressed) {
        progressed = false;
        while (lp < src_len && is_space(src[static_cast<std::size_t>(lp)])) { adv(); progressed = true; }
        if (lp + 1 < src_len && src[static_cast<std::size_t>(lp)] == '/' && src[static_cast<std::size_t>(lp + 1)] == '/') {
            while (lp < src_len && src[static_cast<std::size_t>(lp)] != 0x0A) adv();
            progressed = true;
        } else if (lp + 1 < src_len && src[static_cast<std::size_t>(lp)] == '/' && src[static_cast<std::size_t>(lp + 1)] == '*') {
            int cs_line = lln, cs_col = lcol;
            adv(); adv();                          // consume "/*"
            bool found = false;
            while (lp + 1 < src_len && !found) {
                if (src[static_cast<std::size_t>(lp)] == '*' && src[static_cast<std::size_t>(lp + 1)] == '/') { adv(); adv(); found = true; }
                else adv();
            }
            if (!found) {
                while (lp < src_len) adv();       // swallow to EOF
                add_diag(SEV_ERROR, cs_line, cs_col, "unterminated block comment");
            }
            progressed = true;
        }
    }
}

// ---------------------------------------------------------------------------
// scan_ident -- identifier/keyword (§3.3). $ is deliberately excluded from
// the continuation set (decision 0.6) so malformed names are not silently
// admitted.
// ---------------------------------------------------------------------------
void Vsim::scan_ident() {
    int s = lp;
    while (lp < src_len && is_alnum(src[static_cast<std::size_t>(lp)])) adv();
    int len = lp - s;
    if (len > MAX_IDENT)
        add_diag(SEV_ERROR, tln, tcol, "identifier exceeds MAX_IDENT chars");
    std::string idv = mk_ident(s, len);
    t_text = idv;
    push_tok(kw_lookup(idv));
}

// ---------------------------------------------------------------------------
// scan_number -- four-state numeric literal (§3.5). Handles unsized
// decimals, sized/based literals, x/z/? digits, and size extension/
// truncation. `val` plays the same role as the host reg in the original: a
// four-state accumulator built up by repeated shift-and-insert.
// ---------------------------------------------------------------------------
void Vsim::scan_number() {
    std::uint64_t decval = 0;
    bool haverun = false;
    FourState val;                 // four-state accumulator (based literal digits)
    std::uint64_t val_plain = 0;   // decimal digit-run accumulator (base 'd')
    bool is_based = false, is_signed = false, dec_special = false;
    int size = 0, nw = 0, W = 0;
    std::uint8_t base = 0;
    Bit fillbit = Bit::Zero;

    // ---- leading decimal run: either the size or an unsized decimal ----
    if (src[static_cast<std::size_t>(lp)] != '\'') {
        while (lp < src_len && (is_digit(src[static_cast<std::size_t>(lp)]) || src[static_cast<std::size_t>(lp)] == '_')) {
            if (src[static_cast<std::size_t>(lp)] != '_') {
                decval = decval * 10 + static_cast<std::uint64_t>(src[static_cast<std::size_t>(lp)] - '0');
                haverun = true;
            }
            adv();
        }
    }

    if (lp < src_len && src[static_cast<std::size_t>(lp)] == '\'') {
        // ------------------------------- based literal --------------------
        is_based = true;
        size = haverun ? static_cast<int>(decval) : 0;
        adv();                                        // consume '
        if (lp < src_len && (src[static_cast<std::size_t>(lp)] == 's' || src[static_cast<std::size_t>(lp)] == 'S')) {
            is_signed = true; adv();
        }
        base = (lp < src_len) ? lc(src[static_cast<std::size_t>(lp)]) : 'b';
        switch (base) {
            case 'b': case 'o': case 'h': case 'd': break;
            default:
                add_diag(SEV_ERROR, tln, tcol, "invalid base in numeric literal");
                base = 'b';
                break;
        }
        adv();                                        // consume base char

        if (base == 'd') {
            // whole-value x/z, else a decimal digit run
            if (lp < src_len && (lc(src[static_cast<std::size_t>(lp)]) == 'x' || lc(src[static_cast<std::size_t>(lp)]) == 'z' ||
                                  src[static_cast<std::size_t>(lp)] == '?')) {
                dec_special = true;
                fillbit = (lc(src[static_cast<std::size_t>(lp)]) == 'x') ? Bit::X : Bit::Z;
                adv();
                while (lp < src_len && src[static_cast<std::size_t>(lp)] == '_') adv();
            } else {
                while (lp < src_len && (is_digit(src[static_cast<std::size_t>(lp)]) || src[static_cast<std::size_t>(lp)] == '_')) {
                    if (src[static_cast<std::size_t>(lp)] != '_')
                        val_plain = val_plain * 10 + static_cast<std::uint64_t>(src[static_cast<std::size_t>(lp)] - '0');
                    adv();
                }
                nw = 32;
            }
        } else {
            bool saw_digit = false;
            while (lp < src_len && (valid_base_digit(src[static_cast<std::size_t>(lp)], base) || src[static_cast<std::size_t>(lp)] == '_')) {
                if (src[static_cast<std::size_t>(lp)] == '_') {
                    adv();
                } else {
                    std::uint8_t d = src[static_cast<std::size_t>(lp)], dch = lc(d);
                    saw_digit = true;
                    if (base == 'b') {
                        val.shiftLeft(1);
                        if      (dch == 'x')             val.set(0, Bit::X);
                        else if (dch == 'z' || d == '?') val.set(0, Bit::Z);
                        else                              val.set(0, (d - '0') ? Bit::One : Bit::Zero);
                        nw = nw + 1;
                    } else if (base == 'o') {
                        val.shiftLeft(3);
                        if      (dch == 'x')             val.setRangeUniform(0, 3, Bit::X);
                        else if (dch == 'z' || d == '?') val.setRangeUniform(0, 3, Bit::Z);
                        else                              val.setLowFromUint(3, static_cast<std::uint64_t>(d - '0'));
                        nw = nw + 3;
                    } else {                              // hex
                        val.shiftLeft(4);
                        if      (dch == 'x')             val.setRangeUniform(0, 4, Bit::X);
                        else if (dch == 'z' || d == '?') val.setRangeUniform(0, 4, Bit::Z);
                        else if (is_digit(d))            val.setLowFromUint(4, static_cast<std::uint64_t>(d - '0'));
                        else                              val.setLowFromUint(4, static_cast<std::uint64_t>(dch - 'a' + 10));
                        nw = nw + 4;
                    }
                    adv();
                }
            }
            if (!saw_digit)
                add_diag(SEV_ERROR, tln, tcol, "based literal has no value digits");
        }

        // decimal digit run used the plain accumulator; fold it into `val`
        // exactly where the original reused the same four-state reg for it.
        if (base == 'd' && !dec_special) val.setLowFromUint(32, val_plain);

        // ------------------------- size extend / truncate (§3.5) ----------
        W = haverun ? size : 32;
        if (W > VALW) {
            add_diag(SEV_ERROR, tln, tcol, "literal size exceeds VALW bits");
            W = VALW;
        }
        if (W == 0) W = 32;
        if (nw > VALW) nw = VALW;

        if (dec_special) {
            val.fillRange(0, W, fillbit);
        } else {
            fillbit = Bit::Zero;
            if (nw > 0) {
                if      (val.isX(nw - 1)) fillbit = Bit::X;
                else if (val.isZ(nw - 1)) fillbit = Bit::Z;
            }
            for (int i = nw; i < W; ++i) val.set(i, fillbit);     // left-extend
        }
        for (int i = W; i < VALW; ++i) val.set(i, Bit::Zero);     // clear above W

        (void)is_based;
        t_value  = val;
        t_width  = W;
        t_signed = is_signed;
        push_tok(T_NUMBER);
    } else {
        // ------------------------------ unsized decimal -------------------
        t_value  = FourState::fromUint(decval, 32);
        t_width  = 32;
        t_signed = true;
        push_tok(T_NUMBER);
    }
}

// ---------------------------------------------------------------------------
// scan_op -- punctuation and operators via maximal munch (§3.6). Every
// prefix of a multi-char operator is itself accepting, so we must take the
// LONGEST match: "===" is one token, not "==" then "=".
// ---------------------------------------------------------------------------
void Vsim::scan_op() {
    std::uint8_t c = src[static_cast<std::size_t>(lp)];
    switch (c) {
        case '(': adv(); push_tok(T_LPAREN); break;
        case ')': adv(); push_tok(T_RPAREN); break;
        case '[': adv(); push_tok(T_LBRACK); break;
        case ']': adv(); push_tok(T_RBRACK); break;
        case '{': adv(); push_tok(T_LBRACE); break;
        case '}': adv(); push_tok(T_RBRACE); break;
        case ';': adv(); push_tok(T_SEMI);   break;
        case ',': adv(); push_tok(T_COMMA);  break;
        case ':': adv(); push_tok(T_COLON);  break;
        case '#': adv(); push_tok(T_HASH);   break;
        case '@': adv(); push_tok(T_AT);     break;
        case '.': adv(); push_tok(T_DOT);    break;
        case '?': adv(); push_tok(T_QUES);   break;
        case '+': adv(); push_tok(T_PLUS);   break;
        case '-': adv(); push_tok(T_MINUS);  break;
        case '*': adv(); push_tok(T_STAR);   break;
        case '/': adv(); push_tok(T_SLASH);  break;
        case '%': adv(); push_tok(T_PCT);    break;
        case '=':
            adv();
            if (peekc(0) == '=') {
                adv();
                if (peekc(0) == '=') { adv(); push_tok(T_CASE_EQ); }
                else                        push_tok(T_EQ);
            } else push_tok(T_ASSIGN);
            break;
        case '!':
            adv();
            if (peekc(0) == '=') {
                adv();
                if (peekc(0) == '=') { adv(); push_tok(T_CASE_NEQ); }
                else                        push_tok(T_NEQ);
            } else push_tok(T_NOT);
            break;
        case '<':
            adv();
            if      (peekc(0) == '=') { adv(); push_tok(T_LE);  }
            else if (peekc(0) == '<') { adv(); push_tok(T_SHL); }
            else                             push_tok(T_LT);
            break;
        case '>':
            adv();
            if      (peekc(0) == '=') { adv(); push_tok(T_GE);  }
            else if (peekc(0) == '>') { adv(); push_tok(T_SHR); }
            else                             push_tok(T_GT);
            break;
        case '&':
            adv();
            if (peekc(0) == '&') { adv(); push_tok(T_LAND); }
            else                        push_tok(T_AMP);
            break;
        case '|':
            adv();
            if (peekc(0) == '|') { adv(); push_tok(T_LOR); }
            else                        push_tok(T_PIPE);
            break;
        case '^':
            adv();
            if (peekc(0) == '~') { adv(); push_tok(T_XNORT); }
            else                        push_tok(T_CARET);
            break;
        case '~':
            adv();
            if      (peekc(0) == '^') { adv(); push_tok(T_XNORT); }
            else if (peekc(0) == '&') { adv(); push_tok(T_NANDT); }
            else if (peekc(0) == '|') { adv(); push_tok(T_NORT);  }
            else                             push_tok(T_BNOT);
            break;
        default:
            add_diag(SEV_ERROR, tln, tcol, "unexpected character");
            adv();
            break;
    }
}

// ---------------------------------------------------------------------------
// lex -- scan the entire source arena into the token arena, ending with
// T_EOF.
// ---------------------------------------------------------------------------
void Vsim::lex() {
    lp = 0; lln = 1; lcol = 1; n_tok = 0;
    bool done = false;
    while (!done && !arena_err) {
        skip_ws_comments();
        tln = lln; tcol = lcol;
        if (lp >= src_len) {
            push_tok(T_EOF);
            done = true;
        } else {
            std::uint8_t c = src[static_cast<std::size_t>(lp)];
            if      (is_alpha(c))                scan_ident();
            else if (is_digit(c) || c == '\'')   scan_number();
            else                                  scan_op();
        }
    }
}

} // namespace vsim
