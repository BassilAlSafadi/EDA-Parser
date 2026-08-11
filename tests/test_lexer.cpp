//============================================================================
// test_lexer.cpp -- ported from tb_lexer.v: scanner assertions for the §9
// acceptance criteria.
//   §9.5  "==="  is ONE token; "= = ="  is THREE (maximal munch).
//   §9.6  "a / b" is three tokens; "a // b" is one token + discarded comment.
//   §9.8  4'b1x0z stores width-4 reading back 1x0z; 8'hFF extends per §3.5.
//============================================================================
#include "vsim.hpp"

#include <iostream>
#include <string>

using namespace vsim;

namespace {
int fails = 0;

// lex a snippet, then require the token count (incl. EOF) to equal `want`.
void check_count(Vsim& v, const std::string& s, int want, const std::string& label) {
    v.load_str(s);
    v.lex();
    if (v.n_tok != want) {
        fails++;
        std::cout << "FAIL " << label << ": n_tok=" << v.n_tok << " want=" << want << "\n";
    } else {
        std::cout << "ok   " << label << ": " << v.n_tok << " tokens\n";
    }
}
} // namespace

int main() {
    Vsim v;

    // §9.5 maximal munch of the '=' family
    check_count(v, "===", 2, "=== is one token");                 // CASEEQ, EOF
    if (v.tok_kind[0] != T_CASE_EQ) {
        fails++; std::cout << "FAIL: === did not scan as T_CASE_EQ\n";
    }
    check_count(v, "= = =", 4, "= = = is three tokens");           // 3x ASSIGN, EOF
    if (v.tok_kind[0] != T_ASSIGN || v.tok_kind[1] != T_ASSIGN || v.tok_kind[2] != T_ASSIGN) {
        fails++; std::cout << "FAIL: spaced = = = not three ASSIGN\n";
    }

    // §9.6 divide vs. comment
    check_count(v, "a / b", 4, "a / b is three tokens");           // IDENT SLASH IDENT EOF
    if (v.tok_kind[1] != T_SLASH) {
        fails++; std::cout << "FAIL: a / b middle token not SLASH\n";
    }
    check_count(v, "a // b", 2, "a // b is one token");             // IDENT EOF

    // §9.8 four-state literal storage
    v.load_str("4'b1x0z"); v.lex();
    if (v.tok_kind[0] != T_NUMBER || v.tok_width[0] != 4 || v.tok_value[0].toBitString(4) != "1x0z") {
        fails++;
        std::cout << "FAIL: 4'b1x0z stored as w=" << v.tok_width[0] << " v=" << v.tok_value[0].toBitString(4) << "\n";
    } else {
        std::cout << "ok   4'b1x0z: w=4 v=" << v.tok_value[0].toBitString(4) << "\n";
    }

    v.load_str("8'hFF"); v.lex();
    if (v.tok_kind[0] != T_NUMBER || v.tok_width[0] != 8 || v.tok_value[0].toUint(8) != 0xFF) {
        fails++;
        std::cout << "FAIL: 8'hFF stored as w=" << v.tok_width[0] << " v=" << v.tok_value[0].toBitString(8) << "\n";
    } else {
        std::cout << "ok   8'hFF: w=8 v=" << v.tok_value[0].toBitString(8) << "\n";
    }

    // 8'bx must x-extend to all x across 8 bits (§3.5)
    v.load_str("8'bx"); v.lex();
    if (v.tok_value[0].toBitString(8) != "xxxxxxxx") {
        fails++;
        std::cout << "FAIL: 8'bx did not x-extend: v=" << v.tok_value[0].toBitString(8) << "\n";
    } else {
        std::cout << "ok   8'bx x-extends: v=" << v.tok_value[0].toBitString(8) << "\n";
    }

    if (fails == 0) std::cout << "test_lexer: PASS\n";
    else            std::cout << "test_lexer: FAIL (" << fails << ")\n";
    return fails == 0 ? 0 : 1;
}
