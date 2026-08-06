//============================================================================
// vsim_defs.vh  --  capacities, token kinds, keyword codes, node kinds
//----------------------------------------------------------------------------
// Everything the *host* tool needs as compile-time constants.  Verilog-2001
// has no enums, so these are `define macros (§2.1, §3.2, §5.1 of the spec).
// Using macros (not module-scope parameters) means every included fragment
// AND every separately-compiled testbench sees identical codes.
//
// Two Verilogs live here (see spec "Terminology"):  the HOST is this tool;
// the TARGET is the DUT being parsed.  These macros describe target tokens
// and the host AST that represents them.
//============================================================================
`ifndef VSIM_DEFS_VH
`define VSIM_DEFS_VH

//---------------------------------------------------------------- capacities
// Each guarded so a testbench can shrink one (e.g. `define MAX_NODES 4 before
// including this header) to exercise the overflow path (§9.10).
`ifndef MAX_SRC
  `define MAX_SRC   65536           // target source bytes           (§2.1)
`endif
`ifndef MAX_TOK
  `define MAX_TOK   8192            // tokens
`endif
`ifndef MAX_NODES
  `define MAX_NODES 8192            // AST nodes
`endif
`ifndef MAX_DIAG
  `define MAX_DIAG  64              // diagnostics
`endif
// Elaboration capacities (Phase 2, Task 5.1).  Guarded like the arenas above
// so a testbench can shrink one and exercise the overflow path.
`ifndef MAX_MODS
  `define MAX_MODS  64              // distinct module definitions
`endif
`ifndef MAX_INST
  `define MAX_INST  1024            // elaborated instances in the hierarchy
`endif

`define MAX_IDENT  32               // identifier chars
`define IDW        256              // identifier bit width = MAX_IDENT*8
`define VALW       64               // max literal width

//------------------------------------------------------------ generic tokens
`define T_EOF        8'd0
`define T_IDENT      8'd1
`define T_NUMBER     8'd2

//-------------------------------------------------------------- punctuation
`define T_LBRACK     8'd3           // [
`define T_RBRACK     8'd4           // ]
`define T_LPAREN     8'd5           // (
`define T_RPAREN     8'd6           // )
`define T_LBRACE     8'd7           // {
`define T_RBRACE     8'd8           // }
`define T_SEMI       8'd9           // ;
`define T_COMMA      8'd10          // ,
`define T_COLON      8'd11          // :
`define T_HASH       8'd12          // #
`define T_AT         8'd13          // @
`define T_DOT        8'd14          // .

//----------------------------------------------------------------- operators
`define T_ASSIGN     8'd15          // =    (blocking assign)
`define T_LE         8'd16          // <=   (nonblocking / less-equal, §4.4)
`define T_EQ         8'd17          // ==
`define T_CASE_EQ    8'd18          // ===
`define T_NEQ        8'd19          // !=
`define T_CASE_NEQ   8'd20          // !==
`define T_LT         8'd21          // <
`define T_GT         8'd22          // >
`define T_GE         8'd23          // >=
`define T_SHL        8'd24          // <<
`define T_SHR        8'd25          // >>
`define T_LAND       8'd26          // &&
`define T_LOR        8'd27          // ||
`define T_NOT        8'd28          // !
`define T_BNOT       8'd29          // ~
`define T_AMP        8'd30          // &   (bitwise AND / reduction AND)
`define T_PIPE       8'd31          // |   (bitwise OR  / reduction OR)
`define T_CARET      8'd32          // ^   (bitwise XOR / reduction XOR)
`define T_XNORT      8'd33          // ~^  or  ^~  (XNOR, both spellings)
`define T_NANDT      8'd34          // ~&  (reduction NAND)
`define T_NORT       8'd35          // ~|  (reduction NOR)
`define T_PLUS       8'd36          // +
`define T_MINUS      8'd37          // -
`define T_STAR       8'd38          // *
`define T_SLASH      8'd39          // /
`define T_PCT        8'd40          // %
`define T_QUES       8'd41          // ?

//------------------------------------------------------------ target keywords
// kw_lookup() returns these in place of T_IDENT (§3.3).  Codes >= 64 so the
// keyword range never collides with the punctuation/operator range above.
`define K_MODULE      8'd64
`define K_ENDMODULE   8'd65
`define K_INPUT       8'd66
`define K_OUTPUT      8'd67
`define K_INOUT       8'd68
`define K_WIRE        8'd69
`define K_REG         8'd70
`define K_PARAMETER   8'd71
`define K_ALWAYS      8'd72
`define K_INITIAL     8'd73
`define K_ASSIGN      8'd74
`define K_BEGIN       8'd75
`define K_END         8'd76
`define K_IF          8'd77
`define K_ELSE        8'd78
`define K_CASE        8'd79
`define K_CASEX       8'd80
`define K_CASEZ       8'd81
`define K_ENDCASE     8'd82
`define K_DEFAULT     8'd83
`define K_FOR         8'd84
`define K_WHILE       8'd85
`define K_REPEAT      8'd86
`define K_POSEDGE     8'd87
`define K_NEGEDGE     8'd88
`define K_OR          8'd89          // event separator AND gate primitive (§3.2)
`define K_SIGNED      8'd90
`define K_INTEGER     8'd91
`define K_GENERATE    8'd92
`define K_ENDGENERATE 8'd93

//-------------------------------------------------------- gate primitives
// "or" reuses K_OR above.  These are the remaining gate words (§3.2).
`define K_AND         8'd94
`define K_NOT         8'd95
`define K_NAND        8'd96
`define K_NOR         8'd97
`define K_XOR         8'd98
`define K_XNOR        8'd99
`define K_BUF         8'd100

//--------------------------------------------------------------- node kinds
// nd_kind values (§5.1).  Codes >= 128 keep AST kinds disjoint from tokens
// so a mis-typed field is caught by range, not silently reinterpreted.
`define ND_MODULE       8'd128
`define ND_PORT         8'd129
`define ND_NET          8'd130
`define ND_REG          8'd131
`define ND_PARAM        8'd132
`define ND_CONT_ASSIGN  8'd133
`define ND_ALWAYS       8'd134
`define ND_INITIAL      8'd135
`define ND_GATE_INST    8'd136
`define ND_MOD_INST     8'd137
`define ND_GENERATE     8'd138
`define ND_EVENT_EXPR   8'd139
`define ND_EVENT_TERM   8'd140
`define ND_BLK_ASSIGN   8'd141
`define ND_NB_ASSIGN    8'd142
`define ND_IF           8'd143
`define ND_CASE         8'd144
`define ND_CASE_ITEM    8'd145
`define ND_SEQ_BLOCK    8'd146
`define ND_LOOP         8'd147
`define ND_NULL_STMT    8'd148
`define ND_BINARY       8'd149
`define ND_UNARY        8'd150
`define ND_TERNARY      8'd151
`define ND_CONCAT       8'd152
`define ND_REPLICATION  8'd153
`define ND_LITERAL      8'd154
`define ND_IDENT        8'd155
`define ND_BIT_SELECT   8'd156
`define ND_PART_SELECT  8'd157
`define ND_RANGE        8'd158
`define ND_CONN         8'd159       // named connection  .port(expr)
`define ND_PARAM_ASSIGN 8'd160       // name = value inside #( )

//----------------------------------------------------------- port directions
// stored in nd_op of an ND_PORT / ND_PORT-decl (§5.1)
`define DIR_NONE    8'd0             // non-ANSI header port, direction TBD
`define DIR_INPUT   8'd1
`define DIR_OUTPUT  8'd2
`define DIR_INOUT   8'd3

//-------------------------------------------------------------- net-type flag
// stored in nd_b of ND_PORT: 0 = default wire, 1 = explicit wire, 2 = reg
`define NT_NONE     16'd0
`define NT_WIRE     16'd1
`define NT_REG      16'd2

//--------------------------------------------------------------- edge kinds
// stored in nd_op of ND_EVENT_TERM (§5.1)
`define EDGE_NONE   8'd0
`define EDGE_POS    8'd1
`define EDGE_NEG    8'd2

//------------------------------------------------------------ diag severities
`define SEV_NOTE     8'd0
`define SEV_WARNING  8'd1
`define SEV_ERROR    8'd2

`endif // VSIM_DEFS_VH
