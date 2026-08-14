//============================================================================
// elab.h -- Task 5.1 (module hierarchy resolution) member declarations.
//----------------------------------------------------------------------------
// Implemented by elab.cpp. Declares the method group ported 1:1 from
// vsim_elab.v, plus the module table / instance table state described in
// Elaboration.md §2.
//
// NOT a standalone header: vsim.hpp #include's this file from a point
// INSIDE the body of `class Vsim { ... };` (see vsim.hpp), the same way
// the original Verilog `include-d vsim_elab.v's declarations straight into
// its enclosing module. One Vsim instance is still one independent copy of
// the whole machine (arenas + lexer + parser + elaborator, all one class);
// this file only exists to give Task 5.1 its own named file on disk,
// matching the project's file-per-task convention, without turning the
// single-class design into a multi-class one. elaborate() must run (and
// succeed enough to produce top_inst >= 0) before sig.cpp's
// resolve_signals() is called -- it reads mt_*/in_*/n_inst built here.
//============================================================================

// ===================================================== elab.cpp (Task 5.1)
public:
    int chain_len(int h) const;
    int count_items(int h, Kind k) const;
    int find_module(const std::string& nm) const;
    int find_port_named(int mi, const std::string& nm) const;

    int build_module_table(int root);
    int check_conns(int inst, int tgt);
    int resolve_items(int items);
    int resolve_instances();
    // want_top == "" means "decide automatically" (mirrors want_top == 0 in
    // the Verilog, where 0 was the packed-identifier NULL).
    int pick_top(const std::string& want_top);
    int new_inst(const std::string& nm, int mi, int parent, int node);
    int add_child(int parent, int kid);
    bool mod_on_path(int i, int mi) const;
    int elab_body(int self, int items);
    int elaborate(int root, const std::string& want_top = std::string());

    void fput_path(std::ostream& o, int i) const;
    void dump_inst(std::ostream& o, int i, int ind) const;
    void dump_hier(std::ostream& o) const;

    // ------------------------------------------------------------- state
    // Module table -- one row per module DEFINITION, in declaration order.
    std::vector<std::string> mt_name;
    std::vector<int> mt_node, mt_nports, mt_nparam, mt_ninst, mt_ngate, mt_refs;
    int n_mod = 0;

    // Instance table -- one row per ELABORATED instance. First-child /
    // next-sibling tree, exactly as in vsim_elab.v §2.2: in_child[i] is i's
    // first child, in_sib[c] the next child of the same parent, in_parent[c]
    // the way back up. -1 is the null handle here (unlike the AST, where 0
    // is NULL): instance 0 is the root of the design, module 0 a real module.
    std::vector<std::string> in_name;
    std::vector<int> in_mod, in_parent, in_node, in_child, in_sib, in_depth, in_nconn;
    int n_inst = 0;

    int  top_mod  = -1;
    int  top_inst = -1;
    bool elab_err = false;

    // Effective instance-table capacity, elaboration's counterpart of
    // node_cap. elaborate() does NOT reset it -- a caller may lower it
    // before calling elaborate() to exercise the overflow path (mirrors
    // vsim_top's +instcap=<N>), same as tb_elab.v criterion #12.
    int inst_cap = MAX_INST;
