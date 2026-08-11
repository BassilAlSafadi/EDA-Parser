//============================================================================
// vsim.hpp -- the whole tool: arenas + lexer + parser + dump, one class
//----------------------------------------------------------------------------
// The original host Verilog kept every arena and every function that walks
// it inside a single module, because Verilog functions may only touch
// variables in their own module scope (vsim_arena.v's header comment). C++
// has no such restriction, but the Vsim class still holds everything as
// members -- one instance is one independent copy of the whole machine,
// exactly like one `include-d module was in the original (spec §2, §4, §6,
// §7). Method groups below are ported 1:1 from, and commented with, their
// source file:
//   vsim_arena.v  -> arenas, new_node, char classifiers, identifiers
//   vsim_diag.v   -> diagnostics
//   vsim_lexer.v  -> scanner
//   vsim_parser.v -> recursive-descent + precedence-climbing parser
//   vsim_dump.v   -> token/AST dumps
//============================================================================
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "defs.hpp"
#include "four_state.hpp"

namespace vsim {

class Vsim {
public:
    Vsim();

    // ===================================================== vsim_arena.v (§2)
    static bool is_alpha(std::uint8_t c);
    static bool is_digit(std::uint8_t c);
    static bool is_alnum(std::uint8_t c);
    static bool is_space(std::uint8_t c);
    static bool is_hex(std::uint8_t c);
    static std::uint8_t lc(std::uint8_t c);

    int new_node(Kind kind);

    // pack src[start .. start+len-1] into an identifier. Identifiers longer
    // than MAX_IDENT keep their LAST MAX_IDENT characters (mirrors the
    // original's `acc = (acc << 8) | ch` packing into a fixed-width vector,
    // which drops the earliest bytes once the vector saturates); the caller
    // still raises the "identifier exceeds MAX_IDENT chars" diagnostic.
    std::string mk_ident(int start, int len) const;

    // ====================================================== vsim_diag.v (§7)
    void add_diag(Kind sev, int line, int col, const std::string& msg);
    static std::string sev_name(Kind sev);
    void print_diags(std::ostream& out) const;

    // ===================================================== vsim_lexer.v (§3)
    void load_src(const std::string& filename);   // read a target file
    void load_str(const std::string& s);           // load an in-memory snippet (tests)

    std::uint8_t peekc(int off) const;
    void adv();
    void push_tok(Kind k);
    static Kind kw_lookup(const std::string& s);
    static bool valid_base_digit(std::uint8_t c, std::uint8_t base);
    void skip_ws_comments();
    void scan_ident();
    void scan_number();
    void scan_op();
    void lex();

    // ==================================================== vsim_parser.v (§4)
    Kind cur_kind() const;
    Kind pk1() const;
    int eat_tok();
    bool accept(Kind k);
    bool expect(Kind k, const std::string& msg);
    void recover();
    int chain_tail(int h) const;

    static int binop_prec(Kind k);
    static bool is_prefix_unop(Kind k);

    int parse_primary();
    int parse_concat_or_repl();
    int parse_concat_braces();
    int parse_postfix();
    int parse_unary();
    int parse_expr(int min_prec);

    int parse_range();
    int opt_range();
    bool opt_signed();
    int parse_net_decl();
    int parse_reg_decl();
    int parse_integer_decl();
    int parse_param_decl();

    int find_port(const std::string& nm) const;
    static Kind dir_code(Kind k);
    int parse_port_decl();

    int opt_delay();
    int parse_lvalue();
    int parse_cont_assign();
    static bool is_gate_kw(Kind k);
    int parse_gate_inst();
    int parse_conn();
    int parse_mod_inst();

    int parse_event_expr();
    int parse_always();
    int parse_initial();
    int parse_assign_stmt();
    int parse_assign_nosemi();
    int parse_if();
    int parse_case();
    int parse_seq_block();
    int parse_loop();
    int parse_statement();

    int parse_gen_block();
    int parse_generate();

    int parse_ansi_port();
    int parse_ports();
    int parse_param_port_list();
    int parse_module_item();
    int parse_module();
    int parse_source();

    // ====================================================== vsim_dump.v (§6, §8)
    static std::string tok_kind_name(Kind k);
    static std::string opsym(Kind op);
    static std::string gate_name(Kind g);
    static std::string dir_name(Kind d);
    static std::string case_name(Kind c);
    static std::string loop_name(Kind c);
    static std::string edge_name(Kind e);

    static void dump_val(std::ostream& o, const FourState& val, int width);
    void dexpr(std::ostream& o, int h) const;
    void devent(std::ostream& o, int h) const;
    void dstmt(std::ostream& o, int h, int ind) const;
    void dexpr_stmt(std::ostream& o, int h) const;
    void dport(std::ostream& o, int p, int ind) const;
    void dump_module(std::ostream& o, int m) const;
    void dump_ast(std::ostream& o, int root) const;
    void dump_tokens(std::ostream& o) const;

    // ========================================================= vsim_top.v
    int count_modules(int h) const;

    // -------------------------------------------------------------- state
    // Source arena (§3.1)
    std::vector<std::uint8_t> src;
    int src_len = 0;

    // Token arena (§2.4)
    std::vector<Kind>        tok_kind;
    std::vector<std::string> tok_text;
    std::vector<FourState>   tok_value;   // NUMBER only
    std::vector<int>         tok_width;   // NUMBER only
    std::vector<bool>        tok_signed;
    std::vector<int>         tok_line;
    std::vector<int>         tok_col;
    int n_tok = 0;

    // AST arena (§2.5, §5.1): struct-of-arrays; sibling lists thread through
    // nd_next, terminated at handle 0 (reserved NULL).
    std::vector<Kind>        nd_kind;
    std::vector<int>         nd_a, nd_b, nd_c, nd_d;   // child/operand slots
    std::vector<int>         nd_next;                  // sibling link
    std::vector<std::string> nd_name;
    std::vector<FourState>   nd_value;
    std::vector<int>         nd_width;
    std::vector<bool>        nd_signed;
    std::vector<Kind>        nd_op;
    std::vector<int>         nd_line, nd_col;
    int n_node = 1;   // handle 0 reserved as NULL

    bool arena_err = false;   // set once any arena overflows
    int  node_cap  = MAX_NODES;

    // Diagnostics (§7)
    std::vector<Kind>        diag_sev;
    std::vector<int>         diag_line, diag_col;
    std::vector<std::string> diag_msg;
    int  n_diag = 0;
    bool had_error = false;
    std::string g_fname;

    // Scanner cursor (§3)
    int lp = 0, lln = 1, lcol = 1;   // cursor into src / current line,col
    int tln = 0, tcol = 0;           // start line/col of token being built
    std::string t_text;
    FourState   t_value;
    int         t_width = 0;
    bool        t_signed = false;

    // Parser cursor (§2.6)
    int cur = 0;              // token cursor
    int cur_mod_ports = 0;    // head of current module's port list
};

} // namespace vsim
