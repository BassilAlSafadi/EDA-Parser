//============================================================================
// vsim_arena.v  --  arenas + allocation + identifier/char helpers   (§2)
//----------------------------------------------------------------------------
// This is an INCLUDE FRAGMENT, not a standalone module.  Verilog functions
// may only touch variables in their own module scope, so the arenas and every
// function that walks them must live in one module.  Each top module
// (vsim_top, tb_lexer, tb_parser) `include`s this and the other fragments,
// giving that module its own private copy of the whole machine.  Handle 0 is
// reserved as NULL everywhere, so node allocation starts at 1 (§2.5).
//============================================================================

// --------------------------------------------------------------- source arena
reg  [7:0]      src   [0:`MAX_SRC-1];       // raw target bytes (§3.1)
integer         src_len;

// ---------------------------------------------------------------- token arena
reg  [7:0]      tok_kind   [0:`MAX_TOK-1];  // §2.4
reg  [`IDW-1:0] tok_text   [0:`MAX_TOK-1];  // identifiers & keywords, packed
reg  [`VALW-1:0]tok_value  [0:`MAX_TOK-1];  // NUMBER only, four-state (§2.2)
reg  [15:0]     tok_width  [0:`MAX_TOK-1];  // NUMBER only
reg             tok_signed [0:`MAX_TOK-1];
reg  [15:0]     tok_line   [0:`MAX_TOK-1];
reg  [15:0]     tok_col    [0:`MAX_TOK-1];
integer         n_tok;

// ------------------------------------------------------------------ AST arena
// A struct of parallel arrays (Verilog-2001 has no structs, §2.5).  Lists are
// sibling chains through nd_next terminating at 0 -- never arrays.
reg  [7:0]      nd_kind  [0:`MAX_NODES-1];
reg  [15:0]     nd_a     [0:`MAX_NODES-1];  // child / operand 1
reg  [15:0]     nd_b     [0:`MAX_NODES-1];  // child / operand 2
reg  [15:0]     nd_c     [0:`MAX_NODES-1];  // child / operand 3
reg  [15:0]     nd_d     [0:`MAX_NODES-1];  // 4th slot: ND_LOOP body (§5.1 note)
reg  [15:0]     nd_next  [0:`MAX_NODES-1];  // sibling link, 0 = end of list
reg  [`IDW-1:0] nd_name  [0:`MAX_NODES-1];
reg  [`VALW-1:0]nd_value [0:`MAX_NODES-1];
reg  [15:0]     nd_width [0:`MAX_NODES-1];
reg             nd_signed[0:`MAX_NODES-1];
reg  [7:0]      nd_op    [0:`MAX_NODES-1];  // operator / direction / kind enum
reg  [15:0]     nd_line  [0:`MAX_NODES-1];
reg  [15:0]     nd_col   [0:`MAX_NODES-1];
integer         n_node;

// Set when any arena overflows.  Checked after the run so overflow is a loud
// failure (§9.10), never silent corruption.
reg             arena_err;

// Effective node-arena capacity.  Defaults to MAX_NODES; a run may lower it
// (via +nodecap) to exercise the overflow path without a giant input (§9.10).
integer         node_cap;

// ---------------------------------------------------------------------------
// new_node -- allocate one AST node of the given kind, zeroing every slot.
// Returns the integer handle, or 0 (NULL) once the arena is exhausted (§2.5).
// ---------------------------------------------------------------------------
function automatic integer new_node;
    input [7:0] kind;
    begin
        if (n_node >= node_cap) begin
            if (!arena_err)
                $display("FATAL: AST arena exhausted (capacity=%0d)", node_cap);
            arena_err = 1'b1;
            new_node  = 0;
        end else begin
            new_node        = n_node;
            nd_kind [n_node] = kind;
            nd_a    [n_node] = 0;  nd_b   [n_node] = 0;  nd_c   [n_node] = 0;
            nd_d    [n_node] = 0;  nd_next[n_node] = 0;
            nd_name [n_node] = 0;  nd_value[n_node] = 0; nd_width[n_node] = 0;
            nd_signed[n_node]= 0;  nd_op  [n_node] = 0;
            nd_line [n_node] = 0;  nd_col [n_node] = 0;
            n_node = n_node + 1;
        end
    end
endfunction

// ---------------------------------------------------------------------------
// Character classifiers.  Plain combinational lookups over an 8-bit code.
// ---------------------------------------------------------------------------
function automatic is_alpha;                 // [A-Za-z_]
    input [7:0] c;
    is_alpha = ((c >= "A" && c <= "Z") ||
                (c >= "a" && c <= "z") || c == "_");
endfunction

function automatic is_digit;                 // [0-9]
    input [7:0] c;
    is_digit = (c >= "0" && c <= "9");
endfunction

function automatic is_alnum;                 // [A-Za-z0-9_]
    input [7:0] c;
    is_alnum = (is_alpha(c) || is_digit(c));
endfunction

function automatic is_space;                 // space, tab, CR, LF, FF
    input [7:0] c;
    is_space = (c == " " || c == 8'h09 || c == 8'h0A ||
                c == 8'h0D || c == 8'h0C);
endfunction

function automatic is_hex;                    // [0-9A-Fa-f]
    input [7:0] c;
    is_hex = (is_digit(c) ||
              (c >= "a" && c <= "f") || (c >= "A" && c <= "F"));
endfunction

// lower-case an ASCII letter (used to normalise base/value chars)
function automatic [7:0] lc;
    input [7:0] c;
    lc = (c >= "A" && c <= "Z") ? (c + 8'd32) : c;
endfunction

// ---------------------------------------------------------------------------
// pad -- normalise an identifier vector to IDW bits.  Building an identifier
// by  id = (id<<8)|ch  leaves it right-aligned and zero-extended on the left;
// a string literal ("module") zero-extends the same way, so pad() is the
// identity and comparison is a single wide `==` (§2.3).  Kept as a named
// function so intent is explicit everywhere identifiers are compared.
// ---------------------------------------------------------------------------
function automatic [`IDW-1:0] pad;
    input [`IDW-1:0] s;
    pad = s;
endfunction

// ---------------------------------------------------------------------------
// mk_ident -- pack src[start .. start+len-1] into a right-aligned IDW vector.
// Identifiers longer than MAX_IDENT are flagged by the caller (a diagnostic,
// never a silent truncation, §2.3); here we keep the low MAX_IDENT bytes.
// ---------------------------------------------------------------------------
function automatic [`IDW-1:0] mk_ident;
    input integer start;
    input integer len;
    integer k;
    reg [`IDW-1:0] acc;
    begin
        acc = 0;
        for (k = 0; k < len; k = k + 1)
            acc = (acc << 8) | src[start + k];
        mk_ident = acc;
    end
endfunction

// ---------------------------------------------------------------------------
// fput_ident -- print a packed identifier to file descriptor fdo, skipping the
// leading zero bytes.  Identifiers never contain interior NUL bytes, so
// "skip leading zeros, print the rest" reproduces the text exactly and keeps
// golden dumps deterministic (safer than relying on %s NUL handling).
// ---------------------------------------------------------------------------
task fput_ident;
    input [`IDW-1:0] s;
    input integer    fdo;
    integer k;
    reg     started;
    reg [7:0] b;
    begin
        started = 1'b0;
        for (k = `MAX_IDENT-1; k >= 0; k = k - 1) begin
            b = s[k*8 +: 8];
            if (b != 8'd0) started = 1'b1;
            if (started)  $fwrite(fdo, "%c", b);
        end
    end
endtask
