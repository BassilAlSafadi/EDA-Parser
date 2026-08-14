//============================================================================
// main.cpp -- ported from vsim_top.v: plusarg handling and orchestration.
//----------------------------------------------------------------------------
// Wires the phases together: load -> lex -> parse -> [elaborate] ->
// [resolve_signals] -> [simulate] -> dump.
// The original's plusarg syntax is kept verbatim (+src=, +dump_tokens, ...)
// so the run script needs no argument-style changes, just a different
// binary. Elaboration (Task 5.1, Elaboration.md §4) runs automatically
// whenever the parse succeeds -- it is a required stage of the flow, not an
// opt-in dump, mirroring how PARSE OK/FAILED is unconditional today.
//
// Invocation (see run/run_cpp.sh):
//   +src=<file.v>                 target file to parse             (required)
//   +dump_tokens [+tokout=<f>]    write the token stream (§8)
//   +dump_ast    [+astout=<f>]    write the AST S-expression (§6)
//   +nodecap=<N>                  artificially cap the node arena (§9.10)
//   +top=<name>                   override the deduced top module (Elaboration.md §3.1)
//   +instcap=<N>                  artificially cap the instance table (Elaboration.md §5 #12)
//   +dump_hier   [+hierout=<f>]   write the hierarchy S-expression (Elaboration.md §4)
//   +dump_sig    [+sigout=<f>]    write the signal/connection table (Signal_Resolution.md §4)
//   +sim                          run Phase 3 simulation (requires +vec=)
//   +vec=<file>                   plain-text stimulus file (one vector per line: name=bit ...)
//   +simout=<file>                redirect simulation output (default: stdout)
//   +port=<name>[,<name>...]      signals to print (default: all top-instance signals)
//============================================================================
#include "vsim.hpp"
#include "sim.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
        std::cout << "usage: +src=<file.v> [+dump_tokens] [+dump_ast] [+dump_hier]\n"
                     "       [+dump_sig] [+top=<name>] [+instcap=<N>]\n"
                     "       [+tokout=f] [+astout=f] [+hierout=f] [+sigout=f]\n"
                     "       [+sim] [+vec=<file>] [+simout=<file>] [+port=<name,name,...>]\n";
        return 0;
    }

    vsim::Vsim v;
    v.g_fname = *src;

    v.load_src(*src);                             // Phase 1a: read into src arena
    if (auto cap = value_arg(args, "nodecap"))     // optional overflow demo (§9.10)
        v.node_cap = std::stoi(*cap);
    v.lex();                                       // Phase 1b: scan tokens
    int root = v.parse_source();                   // Phase 1c: build the AST

    // snapshot parse success before elaboration can add its own errors onto
    // the same had_error flag (diag collector is shared, Elaboration.md §4)
    bool parse_ok = !v.had_error && !v.arena_err;

    bool did_elab = false;
    if (parse_ok) {
        if (auto cap = value_arg(args, "instcap"))     // overflow demo (Elaboration.md §5 #12)
            v.inst_cap = std::stoi(*cap);
        std::string want_top = value_arg(args, "top").value_or(std::string());
        v.elaborate(root, want_top);                    // Phase 2, Task 5.1
        did_elab = true;
    }

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

    if (did_elab && has_flag(args, "dump_hier")) {
        if (auto out = value_arg(args, "hierout")) {
            std::ofstream f(*out);
            v.dump_hier(f);
        } else {
            v.dump_hier(std::cout);
        }
    }

    // ----------------------------------------------------------------- Phase 2b: signal resolution
    bool did_sig = false;
    int  nconn   = 0;
    if (did_elab && !v.elab_err && !v.had_error && v.top_mod >= 0) {
        nconn   = v.resolve_signals();   // Task 5.2
        did_sig = true;
    }

    if (did_sig && has_flag(args, "dump_sig")) {
        if (auto out = value_arg(args, "sigout")) {
            std::ofstream f(*out);
            v.dump_sig(f);
        } else {
            v.dump_sig(std::cout);
        }
    }

    // ----------------------------------------------------------------- Phase 3: simulation
    bool did_sim = false;
    int  sim_ret = 0;
    if (did_sig && !v.sig_err && !v.had_error && has_flag(args, "sim")) {
        auto vec_path = value_arg(args, "vec");
        if (!vec_path) {
            std::cerr << "sim: +sim requires +vec=<file>\n";
        } else {
            // Parse the port list (+port=a,b,sum,carry or empty => all).
            std::vector<std::string> port_names;
            if (auto port_arg = value_arg(args, "port")) {
                std::istringstream ps(*port_arg);
                std::string nm;
                while (std::getline(ps, nm, ','))
                    if (!nm.empty()) port_names.push_back(nm);
            }

            auto vectors = vsim::parse_vector_file(*vec_path);

            if (auto simout = value_arg(args, "simout")) {
                std::ofstream f(*simout);
                sim_ret = vsim::simulate(v, vectors, port_names, f);
            } else {
                sim_ret = vsim::simulate(v, vectors, port_names, std::cout);
            }
            did_sim = true;
        }
    }

    v.print_diags(std::cout);   // parse + elaboration + resolution + sim diagnostics

    if (!parse_ok)
        std::cout << "PARSE FAILED: " << v.n_diag << " diagnostic(s)\n";
    else
        std::cout << "PARSE OK: " << count_modules(v, root) << " module(s), "
                   << v.n_tok << " tokens, " << (v.n_node - 1) << " nodes\n";

    if (did_elab) {
        // had_error/elab_err are unconditionally false here unless
        // elaborate() itself raised them, since parse_ok was already true
        if (v.elab_err || v.had_error || v.top_mod < 0)
            std::cout << "ELABORATE FAILED: " << v.n_diag << " diagnostic(s)\n";
        else
            std::cout << "ELABORATE OK: top='" << v.mt_name[static_cast<std::size_t>(v.top_mod)]
                       << "', " << v.n_mod << " module(s), " << v.n_inst << " instance(s)\n";
    }

    if (did_sig) {
        if (v.sig_err || v.had_error)
            std::cout << "RESOLVE FAILED: " << v.n_diag << " diagnostic(s)\n";
        else
            std::cout << "RESOLVE OK: " << v.n_sig << " signal(s), "
                       << nconn << " connection(s)\n";
    }

    if (did_sim) {
        if (sim_ret < 0)
            std::cout << "SIMULATE WARNING: some delta-cycles did not converge\n";
        else
            std::cout << "SIMULATE OK\n";
    }

    return 0;
}
