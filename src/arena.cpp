//============================================================================
// arena.cpp -- ported from vsim_arena.v (§2): arenas, new_node, identifier
// and character helpers.
//============================================================================
#include "vsim.hpp"

#include <iostream>

namespace vsim {

Vsim::Vsim() {
    src.resize(MAX_SRC);

    tok_kind.resize(MAX_TOK);
    tok_text.resize(MAX_TOK);
    tok_value.resize(MAX_TOK);
    tok_width.resize(MAX_TOK);
    tok_signed.resize(MAX_TOK);
    tok_line.resize(MAX_TOK);
    tok_col.resize(MAX_TOK);

    nd_kind.resize(MAX_NODES);
    nd_a.resize(MAX_NODES);
    nd_b.resize(MAX_NODES);
    nd_c.resize(MAX_NODES);
    nd_d.resize(MAX_NODES);
    nd_next.resize(MAX_NODES);
    nd_name.resize(MAX_NODES);
    nd_value.resize(MAX_NODES);
    nd_width.resize(MAX_NODES);
    nd_signed.resize(MAX_NODES);
    nd_op.resize(MAX_NODES);
    nd_line.resize(MAX_NODES);
    nd_col.resize(MAX_NODES);

    diag_sev.resize(MAX_DIAG);
    diag_line.resize(MAX_DIAG);
    diag_col.resize(MAX_DIAG);
    diag_msg.resize(MAX_DIAG);
}

// ---------------------------------------------------------------------------
// new_node -- allocate one AST node of the given kind, zeroing every slot.
// Returns the handle, or 0 (NULL) once the arena is exhausted (§2.5).
// ---------------------------------------------------------------------------
int Vsim::new_node(Kind kind) {
    if (n_node >= node_cap) {
        if (!arena_err)
            std::cerr << "FATAL: AST arena exhausted (capacity=" << node_cap << ")\n";
        arena_err = true;
        return 0;
    }
    int h = n_node;
    nd_kind[h] = kind;
    nd_a[h] = 0; nd_b[h] = 0; nd_c[h] = 0; nd_d[h] = 0; nd_next[h] = 0;
    nd_name[h].clear();
    nd_value[h] = FourState();
    nd_width[h] = 0;
    nd_signed[h] = false;
    nd_op[h] = 0;
    nd_line[h] = 0; nd_col[h] = 0;
    n_node = n_node + 1;
    return h;
}

// ---------------------------------------------------------------------------
// Character classifiers. Plain combinational lookups over an 8-bit code.
// ---------------------------------------------------------------------------
bool Vsim::is_alpha(std::uint8_t c) {                 // [A-Za-z_]
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool Vsim::is_digit(std::uint8_t c) {                 // [0-9]
    return c >= '0' && c <= '9';
}
bool Vsim::is_alnum(std::uint8_t c) {                 // [A-Za-z0-9_]
    return is_alpha(c) || is_digit(c);
}
bool Vsim::is_space(std::uint8_t c) {                 // space, tab, CR, LF, FF
    return c == ' ' || c == 0x09 || c == 0x0A || c == 0x0D || c == 0x0C;
}
bool Vsim::is_hex(std::uint8_t c) {                   // [0-9A-Fa-f]
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
std::uint8_t Vsim::lc(std::uint8_t c) {               // lower-case an ASCII letter
    return (c >= 'A' && c <= 'Z') ? static_cast<std::uint8_t>(c + 32) : c;
}

// ---------------------------------------------------------------------------
// mk_ident -- pack src[start .. start+len-1] into an identifier string.
// Identifiers longer than MAX_IDENT are flagged by the caller (a diagnostic,
// never a silent truncation of the caller's INTENT, §2.3); the stored text
// itself keeps only the last MAX_IDENT characters, mirroring the original's
// fixed-width `acc = (acc << 8) | ch` packing, which drops the earliest
// bytes once the accumulator saturates.
// ---------------------------------------------------------------------------
std::string Vsim::mk_ident(int start, int len) const {
    std::string s;
    s.reserve(static_cast<std::size_t>(len));
    for (int k = 0; k < len; ++k) s.push_back(static_cast<char>(src[static_cast<std::size_t>(start + k)]));
    if (static_cast<int>(s.size()) > MAX_IDENT)
        return s.substr(s.size() - static_cast<std::size_t>(MAX_IDENT));
    return s;
}

} // namespace vsim
