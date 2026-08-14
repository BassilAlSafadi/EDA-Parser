//============================================================================
// verify.cpp -- see verify.h for the design note.
//============================================================================
#include "verify.h"

#include <fstream>
#include <ostream>

namespace vsim {

namespace {

char bit_char_local(Bit b) {
    switch (b) {
        case Bit::Zero: return '0';
        case Bit::One:  return '1';
        case Bit::X:    return 'x';
        case Bit::Z:    return 'z';
    }
    return '?';
}

} // namespace

VerifyReport run_verification_vectors(Vsim& v,
                                       const std::vector<std::vector<StimulusEntry>>& vectors,
                                       std::ostream& out) {
    VerifyReport report;

    if (v.top_inst < 0) {
        out << "VERIFY: no top instance elaborated -- cannot run vectors\n";
        return report;
    }

    auto drivers = collect_assign_drivers(v);
    auto gates   = collect_gate_instances(v);

    for (int t = 0; t < static_cast<int>(vectors.size()); ++t) {
        // Split this line's tokens into stimulus (drive) vs expected (check)
        // using the top instance's own declared port direction. inout ports
        // are treated as drivable stimulus -- this subset has no
        // bidirectional/tri-state resolution.
        std::vector<StimulusEntry> stim;
        std::vector<StimulusEntry> expect;
        for (const StimulusEntry& e : vectors[static_cast<std::size_t>(t)]) {
            int sg = v.find_sig_local(v.top_inst, e.name);
            if (sg < 0) {
                v.add_diag(SEV_WARNING, 0, 0,
                           "vector file cycle " + std::to_string(t) + ": '" + e.name +
                           "' is not a port of the top instance, ignored");
                continue;
            }
            if (v.sg_dir[static_cast<std::size_t>(sg)] == DIR_OUTPUT) expect.push_back(e);
            else                                                     stim.push_back(e);
        }

        SignalValues vals;
        vals.init(v.n_sig);
        apply_stimulus(v, stim, vals);
        int passes = delta_cycle(v, drivers, gates, vals);

        CycleResult cr;
        cr.cycle        = t;
        cr.delta_passes = passes;
        cr.pass         = (passes >= 0);

        for (const StimulusEntry& e : expect) {
            int sg = v.find_sig_local(v.top_inst, e.name);
            VectorCheck vc;
            vc.name      = e.name;
            vc.expected  = e.value;
            vc.actual    = vals.get(sg);
            vc.dont_care = (e.value == Bit::X);
            vc.match     = vc.dont_care || (vc.actual == vc.expected);
            if (!vc.match) cr.pass = false;
            cr.checks.push_back(vc);
        }

        out << "cycle " << t << ":";
        for (const VectorCheck& vc : cr.checks) {
            out << ' ' << vc.name << '=' << bit_char_local(vc.actual);
            if (!vc.dont_care && !vc.match)
                out << "(expected " << bit_char_local(vc.expected) << ')';
        }
        if (passes < 0) out << " [did not converge]";
        out << "  " << (cr.pass ? "PASS" : "FAIL") << '\n';

        report.cycles.push_back(cr);
        report.total_cycles += 1;
        if (passes < 0) report.nonconvergent += 1;
        if (cr.pass) report.passed += 1; else report.failed += 1;
    }

    out << "----\n" << report.passed << '/' << report.total_cycles << " cycles passed";
    if (report.nonconvergent > 0) out << " (" << report.nonconvergent << " did not converge)";
    out << '\n';

    return report;
}

VerifyReport run_verification(Vsim& v, const std::string& vector_path, std::ostream& out) {
    auto vectors = parse_vector_file(vector_path);
    return run_verification_vectors(v, vectors, out);
}

} // namespace vsim
