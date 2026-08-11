//============================================================================
// defs.hpp -- capacities, token kinds, keyword codes, node kinds
//----------------------------------------------------------------------------
// Ported 1:1 from the host Verilog's vsim_defs.vh (`define macros, since
// Verilog-2001 has no enums). Values are kept numerically identical to the
// original so token/node kinds and diagnostics behave exactly the same.
//
// Two Verilogs are referenced throughout the codebase (see Specification.md
// "Terminology"): the HOST is this tool (now C++); the TARGET is the DUT
// (still Verilog) being parsed. These constants describe target tokens and
// the host AST that represents them.
//============================================================================
#pragma once
#include <cstdint>

namespace vsim {

using Kind = std::uint8_t;   // token kind / node kind / keyword code

//---------------------------------------------------------------- capacities
constexpr int MAX_SRC   = 65536;   // target source bytes
constexpr int MAX_TOK   = 8192;    // tokens
constexpr int MAX_NODES = 8192;    // AST nodes
constexpr int MAX_DIAG  = 64;      // diagnostics
constexpr int MAX_IDENT = 32;      // identifier chars
constexpr int VALW      = 64;      // max literal width (bits)

//------------------------------------------------------------ generic tokens
constexpr Kind T_EOF        = 0;
constexpr Kind T_IDENT      = 1;
constexpr Kind T_NUMBER     = 2;

//-------------------------------------------------------------- punctuation
constexpr Kind T_LBRACK     = 3;
constexpr Kind T_RBRACK     = 4;
constexpr Kind T_LPAREN     = 5;
constexpr Kind T_RPAREN     = 6;
constexpr Kind T_LBRACE     = 7;
constexpr Kind T_RBRACE     = 8;
constexpr Kind T_SEMI       = 9;
constexpr Kind T_COMMA      = 10;
constexpr Kind T_COLON      = 11;
constexpr Kind T_HASH       = 12;
constexpr Kind T_AT         = 13;
constexpr Kind T_DOT        = 14;

//----------------------------------------------------------------- operators
constexpr Kind T_ASSIGN     = 15;   // =    (blocking assign)
constexpr Kind T_LE         = 16;   // <=   (nonblocking / less-equal)
constexpr Kind T_EQ         = 17;   // ==
constexpr Kind T_CASE_EQ    = 18;   // ===
constexpr Kind T_NEQ        = 19;   // !=
constexpr Kind T_CASE_NEQ   = 20;   // !==
constexpr Kind T_LT         = 21;   // <
constexpr Kind T_GT         = 22;   // >
constexpr Kind T_GE         = 23;   // >=
constexpr Kind T_SHL        = 24;   // <<
constexpr Kind T_SHR        = 25;   // >>
constexpr Kind T_LAND       = 26;   // &&
constexpr Kind T_LOR        = 27;   // ||
constexpr Kind T_NOT        = 28;   // !
constexpr Kind T_BNOT       = 29;   // ~
constexpr Kind T_AMP        = 30;   // &   (bitwise AND / reduction AND)
constexpr Kind T_PIPE       = 31;   // |   (bitwise OR  / reduction OR)
constexpr Kind T_CARET      = 32;   // ^   (bitwise XOR / reduction XOR)
constexpr Kind T_XNORT      = 33;   // ~^  or  ^~  (XNOR, both spellings)
constexpr Kind T_NANDT      = 34;   // ~&  (reduction NAND)
constexpr Kind T_NORT       = 35;   // ~|  (reduction NOR)
constexpr Kind T_PLUS       = 36;   // +
constexpr Kind T_MINUS      = 37;   // -
constexpr Kind T_STAR       = 38;   // *
constexpr Kind T_SLASH      = 39;   // /
constexpr Kind T_PCT        = 40;   // %
constexpr Kind T_QUES       = 41;   // ?

//------------------------------------------------------------ target keywords
// kw_lookup() returns these in place of T_IDENT. Codes >= 64 so the keyword
// range never collides with the punctuation/operator range above.
constexpr Kind K_MODULE      = 64;
constexpr Kind K_ENDMODULE   = 65;
constexpr Kind K_INPUT       = 66;
constexpr Kind K_OUTPUT      = 67;
constexpr Kind K_INOUT       = 68;
constexpr Kind K_WIRE        = 69;
constexpr Kind K_REG         = 70;
constexpr Kind K_PARAMETER   = 71;
constexpr Kind K_ALWAYS      = 72;
constexpr Kind K_INITIAL     = 73;
constexpr Kind K_ASSIGN      = 74;
constexpr Kind K_BEGIN       = 75;
constexpr Kind K_END         = 76;
constexpr Kind K_IF          = 77;
constexpr Kind K_ELSE        = 78;
constexpr Kind K_CASE        = 79;
constexpr Kind K_CASEX       = 80;
constexpr Kind K_CASEZ       = 81;
constexpr Kind K_ENDCASE     = 82;
constexpr Kind K_DEFAULT     = 83;
constexpr Kind K_FOR         = 84;
constexpr Kind K_WHILE       = 85;
constexpr Kind K_REPEAT      = 86;
constexpr Kind K_POSEDGE     = 87;
constexpr Kind K_NEGEDGE     = 88;
constexpr Kind K_OR          = 89;   // event separator AND gate primitive
constexpr Kind K_SIGNED      = 90;
constexpr Kind K_INTEGER     = 91;
constexpr Kind K_GENERATE    = 92;
constexpr Kind K_ENDGENERATE = 93;

//-------------------------------------------------------- gate primitives
// "or" reuses K_OR above. These are the remaining gate words.
constexpr Kind K_AND         = 94;
constexpr Kind K_NOT         = 95;
constexpr Kind K_NAND        = 96;
constexpr Kind K_NOR         = 97;
constexpr Kind K_XOR         = 98;
constexpr Kind K_XNOR        = 99;
constexpr Kind K_BUF         = 100;

//--------------------------------------------------------------- node kinds
// nd_kind values. Codes >= 128 keep AST kinds disjoint from tokens so a
// mis-typed field is caught by range, not silently reinterpreted.
constexpr Kind ND_MODULE       = 128;
constexpr Kind ND_PORT         = 129;
constexpr Kind ND_NET          = 130;
constexpr Kind ND_REG          = 131;
constexpr Kind ND_PARAM        = 132;
constexpr Kind ND_CONT_ASSIGN  = 133;
constexpr Kind ND_ALWAYS       = 134;
constexpr Kind ND_INITIAL      = 135;
constexpr Kind ND_GATE_INST    = 136;
constexpr Kind ND_MOD_INST     = 137;
constexpr Kind ND_GENERATE     = 138;
constexpr Kind ND_EVENT_EXPR   = 139;
constexpr Kind ND_EVENT_TERM   = 140;
constexpr Kind ND_BLK_ASSIGN   = 141;
constexpr Kind ND_NB_ASSIGN    = 142;
constexpr Kind ND_IF           = 143;
constexpr Kind ND_CASE         = 144;
constexpr Kind ND_CASE_ITEM    = 145;
constexpr Kind ND_SEQ_BLOCK    = 146;
constexpr Kind ND_LOOP         = 147;
constexpr Kind ND_NULL_STMT    = 148;
constexpr Kind ND_BINARY       = 149;
constexpr Kind ND_UNARY        = 150;
constexpr Kind ND_TERNARY      = 151;
constexpr Kind ND_CONCAT       = 152;
constexpr Kind ND_REPLICATION  = 153;
constexpr Kind ND_LITERAL      = 154;
constexpr Kind ND_IDENT        = 155;
constexpr Kind ND_BIT_SELECT   = 156;
constexpr Kind ND_PART_SELECT  = 157;
constexpr Kind ND_RANGE        = 158;
constexpr Kind ND_CONN         = 159;   // named connection  .port(expr)
constexpr Kind ND_PARAM_ASSIGN = 160;   // name = value inside #( )

//----------------------------------------------------------- port directions
// stored in nd_op of an ND_PORT / ND_PORT-decl
constexpr Kind DIR_NONE    = 0;   // non-ANSI header port, direction TBD
constexpr Kind DIR_INPUT   = 1;
constexpr Kind DIR_OUTPUT  = 2;
constexpr Kind DIR_INOUT   = 3;

//-------------------------------------------------------------- net-type flag
// stored in nd_b of ND_PORT: 0 = default wire, 1 = explicit wire, 2 = reg
constexpr std::uint16_t NT_NONE = 0;
constexpr std::uint16_t NT_WIRE = 1;
constexpr std::uint16_t NT_REG  = 2;

//--------------------------------------------------------------- edge kinds
// stored in nd_op of ND_EVENT_TERM
constexpr Kind EDGE_NONE = 0;
constexpr Kind EDGE_POS  = 1;
constexpr Kind EDGE_NEG  = 2;

//------------------------------------------------------------ diag severities
constexpr Kind SEV_NOTE    = 0;
constexpr Kind SEV_WARNING = 1;
constexpr Kind SEV_ERROR   = 2;

} // namespace vsim
