//============================================================================
// diag.cpp -- ported from vsim_diag.v (§7): diagnostic collection, sorting,
// printing. Policy (decision 0.7): collect every diagnostic in an array,
// print sorted by source position, and set a failure flag.
//
// add_diag was a FUNCTION (not a task) in the host Verilog specifically so
// the recursive-descent parser -- built from functions, which may not call
// tasks -- could report errors; callers wrote `ignore = add_diag(...);`. In
// C++ every method can call every other method, so that workaround is gone
// and add_diag is simply void.
//============================================================================
#include "vsim.hpp"

#include <algorithm>
#include <ostream>

namespace vsim {

// ---------------------------------------------------------------------------
// add_diag -- record one diagnostic. Silently drops everything past
// MAX_DIAG (the parser also stops after MAX_DIAG errors, §7) but still
// marks failure.
// ---------------------------------------------------------------------------
void Vsim::add_diag(Kind sev, int line, int col, const std::string& msg) {
    if (sev == SEV_ERROR) had_error = true;
    if (n_diag < MAX_DIAG) {
        diag_sev[n_diag]  = sev;
        diag_line[n_diag] = line;
        diag_col[n_diag]  = col;
        diag_msg[n_diag]  = msg;
        n_diag = n_diag + 1;
    }
}

std::string Vsim::sev_name(Kind sev) {
    switch (sev) {
        case SEV_ERROR:   return "error";
        case SEV_WARNING: return "warning";
        default:          return "note";
    }
}

// ---------------------------------------------------------------------------
// print_diags -- selection-sort the collected diagnostics by (line, col)
// then emit them as  file.v:LINE:COL: sev: message . n_diag <= MAX_DIAG (64)
// so an O(n^2) sort is trivially fine and keeps output deterministic (§7).
// ---------------------------------------------------------------------------
void Vsim::print_diags(std::ostream& out) const {
    std::vector<int> order(static_cast<std::size_t>(n_diag));
    for (int i = 0; i < n_diag; ++i) order[static_cast<std::size_t>(i)] = i;

    for (int i = 0; i < n_diag; ++i) {
        int best = i;
        for (int j = i + 1; j < n_diag; ++j) {
            int oj = order[static_cast<std::size_t>(j)], ob = order[static_cast<std::size_t>(best)];
            if ((diag_line[static_cast<std::size_t>(oj)] < diag_line[static_cast<std::size_t>(ob)]) ||
                (diag_line[static_cast<std::size_t>(oj)] == diag_line[static_cast<std::size_t>(ob)] &&
                 diag_col[static_cast<std::size_t>(oj)]  < diag_col[static_cast<std::size_t>(ob)]))
                best = j;
        }
        std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(best)]);
    }

    for (int i = 0; i < n_diag; ++i) {
        int k = order[static_cast<std::size_t>(i)];
        out << g_fname << ":" << diag_line[static_cast<std::size_t>(k)] << ":"
            << diag_col[static_cast<std::size_t>(k)] << ": " << sev_name(diag_sev[static_cast<std::size_t>(k)])
            << ": " << diag_msg[static_cast<std::size_t>(k)] << "\n";
    }
}

} // namespace vsim
