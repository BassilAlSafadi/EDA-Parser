//============================================================================
// main.cpp -- ported from vsim_top.v: plusarg handling and orchestration.
//----------------------------------------------------------------------------
// Wires the phases together for Phase 1: load -> lex -> parse -> dump. The
// original's plusarg syntax is kept verbatim (+src=, +dump_tokens, ...) so
// the run script needs no argument-style changes, just a different binary.
//
// Invocation (see run/run_cpp.sh):
//   +src=<file.v>                 target file to parse             (required)
//   +dump_tokens [+tokout=<f>]    write the token stream (§8)
//   +dump_ast    [+astout=<f>]    write the AST S-expression (§6)
//   +nodecap=<N>                  artificially cap the node arena (§9.10)
//============================================================================
#include "vsim.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

// count modules in the parsed chain (for the summary line)
int count_modules(const vsim::Vsim& v, int h) {
    int c = 0, m = h;
    while (m != 0) { c = c + 1; m = v.nd_next[static_cast<std::size_t>(m)]; }
    return c;
}

bool has_flag(const std::vector<std::string>& args, const std::string& name) {
    for (const auto& a : args) if (a == "+" + name) return true;
    return false;
}

std::optional<std::string> value_arg(const std::vector<std::string>& args, const std::string& name) {
    const std::string prefix = "+" + name + "=";
    for (const auto& a : args)
        if (a.rfind(prefix, 0) == 0) return a.substr(prefix.size());
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    auto src = value_arg(args, "src");
    if (!src) {
        std::cout << "usage: +src=<file.v> [+dump_tokens] [+dump_ast] [+tokout=f] [+astout=f]\n";
        return 0;
    }

    vsim::Vsim v;
    v.g_fname = *src;

    v.load_src(*src);                             // Phase 1a: read into src arena
    if (auto cap = value_arg(args, "nodecap"))     // optional overflow demo (§9.10)
        v.node_cap = std::stoi(*cap);
    v.lex();                                       // Phase 1b: scan tokens
    int root = v.parse_source();                   // Phase 1c: build the AST

    if (has_flag(args, "dump_tokens")) {
        if (auto out = value_arg(args, "tokout")) {
            std::ofstream f(*out);
            v.dump_tokens(f);
        } else {
            v.dump_tokens(std::cout);
        }
    }

    if (has_flag(args, "dump_ast")) {
        if (auto out = value_arg(args, "astout")) {
            std::ofstream f(*out);
            v.dump_ast(f, root);
        } else {
            v.dump_ast(std::cout, root);
        }
    }

    v.print_diags(std::cout);

    if (v.had_error || v.arena_err)
        std::cout << "PARSE FAILED: " << v.n_diag << " diagnostic(s)\n";
    else
        std::cout << "PARSE OK: " << count_modules(v, root) << " module(s), "
                   << v.n_tok << " tokens, " << (v.n_node - 1) << " nodes\n";

    return 0;
}
