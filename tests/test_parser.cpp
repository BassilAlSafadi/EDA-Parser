//============================================================================
// test_parser.cpp -- ported from tb_parser.v: structural AST assertions for
// the §9 acceptance criteria.
//   §9.3  COUT = (A&B)|(A&CIN)|(B&CIN) builds exactly the §5.2 tree
//         (| left-associative on top, every & one level deeper).
//   §9.7  if(a) if(b) x=1; else y=1;  binds else to the inner if.
// Also confirms both snippets parse with zero diagnostics (§9.1).
//============================================================================
#include "vsim.hpp"

#include <iostream>
#include <string>

using namespace vsim;

namespace {
int fails = 0;

void ck(bool cond, const std::string& msg) {
    if (!cond) { fails++; std::cout << "FAIL: " << msg << "\n"; }
    else       { std::cout << "ok   " << msg << "\n"; }
}

// true iff h is an ND_IDENT named nm
bool is_id(const Vsim& v, int h, const std::string& nm) {
    return h != 0 && v.nd_kind[static_cast<std::size_t>(h)] == ND_IDENT && v.nd_name[static_cast<std::size_t>(h)] == nm;
}

// true iff h is an ND_BINARY with the given operator
bool is_bin(const Vsim& v, int h, Kind op) {
    return h != 0 && v.nd_kind[static_cast<std::size_t>(h)] == ND_BINARY && v.nd_op[static_cast<std::size_t>(h)] == op;
}
} // namespace

int main() {
    Vsim v;

    //---------------------------------------------------- §9.3 COUT tree
    v.load_str("module fa(A,B,CIN,COUT); input A,B,CIN; output COUT; "
               "assign COUT=(A&B)|(A&CIN)|(B&CIN); endmodule");
    v.lex();
    int root = v.parse_source();
    ck(v.n_diag == 0, "COUT parses with zero diagnostics");
    int m = root;
    int casgn = v.nd_b[static_cast<std::size_t>(m)];                    // first (only) item
    ck(v.nd_kind[static_cast<std::size_t>(casgn)] == ND_CONT_ASSIGN, "top item is a continuous assign");
    int top = v.nd_b[static_cast<std::size_t>(casgn)];                  // RHS expression
    ck(is_bin(v, top, T_PIPE),        "root is (| ...)");
    int L = v.nd_a[static_cast<std::size_t>(top)], R = v.nd_b[static_cast<std::size_t>(top)];
    ck(is_bin(v, L, T_PIPE),          "left child is (| ...) -- left-assoc");
    ck(is_bin(v, R, T_AMP),           "right child is (& B CIN)");
    ck(is_id(v, v.nd_a[static_cast<std::size_t>(R)], "B") && is_id(v, v.nd_b[static_cast<std::size_t>(R)], "CIN"), "  right & has B,CIN");
    int LL = v.nd_a[static_cast<std::size_t>(L)], LR = v.nd_b[static_cast<std::size_t>(L)];
    ck(is_bin(v, LL, T_AMP),          "L.left is (& A B)");
    ck(is_id(v, v.nd_a[static_cast<std::size_t>(LL)], "A") && is_id(v, v.nd_b[static_cast<std::size_t>(LL)], "B"),   "  it has A,B");
    ck(is_bin(v, LR, T_AMP),          "L.right is (& A CIN)");
    ck(is_id(v, v.nd_a[static_cast<std::size_t>(LR)], "A") && is_id(v, v.nd_b[static_cast<std::size_t>(LR)], "CIN"), "  it has A,CIN");

    //------------------------------------------------ §9.7 dangling else
    v.load_str("module m(input a,input b,output reg x,output reg y); "
               "always @(*) if(a) if(b) x=1; else y=1; endmodule");
    v.lex();
    root = v.parse_source();
    ck(v.n_diag == 0, "dangling-else snippet parses with zero diagnostics");
    m = root;
    int alwys = v.nd_b[static_cast<std::size_t>(m)];                    // the always block
    ck(v.nd_kind[static_cast<std::size_t>(alwys)] == ND_ALWAYS, "item is an always block");
    int outer = v.nd_b[static_cast<std::size_t>(alwys)];                // outer if
    ck(v.nd_kind[static_cast<std::size_t>(outer)] == ND_IF, "outer statement is an if");
    ck(v.nd_c[static_cast<std::size_t>(outer)] == 0,          "outer if has NO else");
    int inner = v.nd_b[static_cast<std::size_t>(outer)];                // then-branch = inner if
    ck(v.nd_kind[static_cast<std::size_t>(inner)] == ND_IF, "inner statement is an if");
    ck(v.nd_c[static_cast<std::size_t>(inner)] != 0,          "inner if HAS the else (binds inner)");

    if (fails == 0) std::cout << "test_parser: PASS\n";
    else            std::cout << "test_parser: FAIL (" << fails << ")\n";
    return fails == 0 ? 0 : 1;
}
