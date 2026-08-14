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
    //============================================================================
// vsim_hpp_ADDITIONS.hpp -- APPEND these into class Vsim in vsim.hpp
//----------------------------------------------------------------------------
// Two new method groups, ported 1:1 from and commented with their source:
//   vsim_elab.v -> elab.cpp   (Task 5.1: module hierarchy resolution)
//   vsim_sig.v  -> sig.cpp    (Task 5.2: signal & port resolution)
//
// Same convention as the rest of vsim.hpp: one Vsim instance is one
// independent copy of the whole machine. elaborate() must run (and succeed
// enough to produce top_inst >= 0) before resolve_signals() is called --
// sig.cpp reads mt_*/in_*/n_inst built by elab.cpp.
//
// Task 5.1's declarations live in their own src/elab.h, #include'd right
// here -- i.e. still textually inside this class body, just given a
// separate file on disk (see elab.h's header comment for why).
//============================================================================
#include "elab.h"

// ====================================================== sig.cpp (Task 5.2)
public:
    int find_sig_local(int inst, const std::string& nm) const;
    int port_local_at(int inst, int pos) const;
    int find_param_local(int inst, const std::string& nm) const;

    int eval_const(int inst, int e);

    int scan_decls(int inst, int items);
    int build_scope(int inst);

    int new_param(int inst, const std::string& nm, int val);
    int scan_params(int inst, int items);
    int apply_overrides(int inst);
    int build_params(int inst);

    int resolve_widths(int inst);

    int resolve_conn_expr(int scope_inst, int e);
    int new_conn(int inst, int portsig, int netsig);
    int flatten_connections(int inst);

    int resolve_signals();   // driver -- call after elaborate() succeeds

    void dump_sig(std::ostream& o) const;

    // ------------------------------------------------------------- state
    // Signal table -- one row per port or per locally declared wire/reg,
    // scoped to one instance. Ports of instance i always occupy sg_*
    // indices [in_sig0[i] .. in_sig0[i]+mt_nports[in_mod[i]]-1), in header
    // order (build_scope adds them first), so positional connections can
    // index straight into this range via port_local_at.
    std::vector<std::string> sg_name;
    std::vector<int>  sg_inst, sg_node, sg_dir, sg_width;
    std::vector<bool> sg_isport;
    int n_sig = 0;

    // Parameter table -- one row per parameter, scoped to one instance:
    // defaults copied from the module (header #( ) list, then body
    // `parameter` decls, declaration order), then overridden by that
    // instance's own #( ) list.
    std::vector<std::string> pm_name;
    std::vector<int> pm_inst, pm_value;
    int n_param = 0;

    // Connection table -- one row per resolved port<->net binding. cn_port
    // is a sg_* index in the CHILD's own scope, cn_net a sg_* index in the
    // PARENT's scope, or -1 for an unconnected port or an unresolved
    // expression.
    std::vector<int> cn_inst, cn_port, cn_net;
    int n_conn = 0;

    std::vector<int> in_sig0;   // first sg_* index for this instance
    std::vector<int> in_nsig;   // count of signals in its scope
    std::vector<int> in_par0;   // first pm_* index for this instance

    bool sig_err = false;
};

} // namespace vsim
