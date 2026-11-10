// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

// Probing with unit propagation: failed literals and lifted implications on
// CNF formulas; failed soft literals and joint satisfiability of the soft
// literals on WCNF formulas. Serves as integration test and benchmark of the
// propagators: all counters depend only on propagation fixpoints and must be
// identical for every propagator.
//
// Soft clause handling is application logic: only unit soft clauses are
// considered here, as soft literals.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "common/cli.h"
#include "propan/solver.h"
#include "propan/wcnf_formula.h"

using namespace propan;

namespace {

struct Stats {
    std::uint64_t probes = 0;    ///< decision levels opened for probing
    std::uint64_t implied = 0;   ///< literals implied by probes without conflict
    std::uint64_t failed = 0;    ///< probes ending in a conflict
    std::uint64_t lifted = 0;    ///< literals implied by both polarities of a variable
};

/// Probes `lit` on a new level. @return false on conflict; the level is left open otherwise
bool probe(Solver& s, Lit lit, Stats& stats) {
    ++stats.probes;
    s.decide(lit);
    if (s.propagate() != ClauseRef()) {
        ++stats.failed;
        s.trail().backtrack();
        return false;
    }
    stats.implied += s.trail().literals(s.trail().level()).size() - 1;
    return true;
}

/// Assigns a root fact and propagates. @return false if the formula is refuted
bool fixRoot(Solver& s, Lit lit) {
    return s.trail().push(lit) && s.propagate() == ClauseRef();
}

/// Failed literal probing with lifting of common implications, over all unassigned variables.
/// @return false if the formula is refuted
bool probeVariables(Solver& s, Stats& stats, unsigned rounds) {
    Trail& trail = s.trail();
    std::vector<unsigned char> mark(2 * trail.nVars() + 2, 0);
    std::vector<Lit> first, common;
    for (unsigned round = 0; round < rounds; ++round) {
        for (unsigned v = 1; v <= trail.nVars(); ++v) {
            const Lit pos(static_cast<int>(v));
            if (trail.value(pos) != VAL_UNASSIGNED) continue;
            if (!probe(s, pos, stats)) {
                if (!fixRoot(s, -pos)) return false;
                continue;
            }
            const auto implied = trail.literals(trail.level()).subspan(1);
            first.assign(implied.begin(), implied.end());
            trail.backtrack();
            for (Lit l : first) mark[l.index()] = 1;
            const bool other_ok = probe(s, -pos, stats);
            common.clear();
            if (other_ok) {
                for (Lit l : trail.literals(trail.level()).subspan(1)) {
                    if (mark[l.index()]) common.push_back(l);
                }
                trail.backtrack();
            }
            for (Lit l : first) mark[l.index()] = 0;
            if (!other_ok) {
                if (!fixRoot(s, pos)) return false;
                continue;
            }
            stats.lifted += common.size();
            for (Lit l : common) {
                if (!fixRoot(s, l)) return false;
            }
        }
    }
    return true;
}

int runCnf(CNFFormula f, const Options& options, unsigned rounds) {
    const auto t0 = std::chrono::steady_clock::now();
    Solver s(std::move(f), options);
    Stats stats;
    bool ok = s.consistent();
    const std::size_t root_units = s.trail().size();
    if (ok) ok = probeVariables(s, stats, rounds);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "c variables " << s.nVars() << " clauses " << s.formula().nClauses() << "\n"
              << "c root-units " << root_units << "\n"
              << "c probes " << stats.probes << " failed " << stats.failed << " lifted " << stats.lifted
              << " implied " << stats.implied << "\n"
              << "c root-assigned " << (ok ? s.trail().literals(0).size() : 0) << "\n"
              << "c propagator " << options.propagator << " time " << secs << "\n"
              << (ok ? "s UNKNOWN" : "s UNSATISFIABLE") << std::endl;
    return 0;
}

int runWcnf(WCNFFormula f, const Options& options) {
    const auto t0 = std::chrono::steady_clock::now();
    // Soft literals: unit soft clauses, taken before the hard part is moved into the solver.
    std::vector<Lit> softs;
    std::vector<std::uint64_t> weights;
    for (const SoftClause& clause : f.soft()) {
        if (clause.lits.size() == 1) {
            softs.push_back(clause.lits[0]);
            weights.push_back(clause.weight);
        }
    }
    const unsigned n_vars = f.nVars();
    const std::size_t n_hard = f.nHardClauses(), n_soft = f.nSoftClauses();
    f.hard().ensureVars(n_vars);  // soft-only variables must exist in the solver
    Solver s(std::move(f.hard()), options);
    Trail& trail = s.trail();
    Stats stats;
    bool ok = s.consistent();
    if (ok) ok = probeVariables(s, stats, 1);  // preprocess the hard part
    // Probe every soft literal; a failed one is falsified at the root and counts towards the lower bound.
    std::uint64_t lower_bound = 0;
    std::uint64_t already_true = 0;
    std::vector<Lit> open;
    for (std::size_t i = 0; ok && i < softs.size(); ++i) {
        const Lit l = softs[i];
        if (trail.value(l) == VAL_TRUE) {
            ++already_true;
        } else if (trail.value(l) == VAL_FALSE) {
            lower_bound += weights[i];
        } else if (probe(s, l, stats)) {
            trail.backtrack();
            open.push_back(l);
        } else {
            lower_bound += weights[i];
            ok = fixRoot(s, -l);
        }
    }
    // Push all open soft literals as one block and see whether propagation accepts them.
    bool block_consistent = false;
    std::uint64_t block_implied = 0;
    if (ok) {
        block_consistent = s.decide(open) && s.propagate() == ClauseRef();
        if (block_consistent) block_implied = trail.literals(trail.level()).size() - open.size();
        trail.backtrack();
    }
    // Greedy: one level per soft literal, skipping those that conflict.
    std::uint64_t greedy_skipped = 0;
    std::uint64_t greedy_weight = 0;
    if (ok) {
        for (std::size_t i = 0; i < softs.size(); ++i) {
            const Lit l = softs[i];
            if (trail.value(l) == VAL_TRUE) continue;
            if (trail.value(l) == VAL_FALSE || !probe(s, l, stats)) {
                ++greedy_skipped;
                greedy_weight += weights[i];
            }
        }
        trail.backtrackTo(0);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "c variables " << n_vars << " hard " << n_hard << " soft " << n_soft
              << " soft-units " << softs.size() << "\n"
              << "c probes " << stats.probes << " failed " << stats.failed << " lifted " << stats.lifted
              << " implied " << stats.implied << "\n"
              << "c soft-true " << already_true << " lower-bound " << lower_bound << "\n"
              << "c block " << (block_consistent ? "consistent" : "conflicting") << " implied " << block_implied << "\n"
              << "c greedy-skipped " << greedy_skipped << " weight " << greedy_weight << "\n"
              << "c propagator " << options.propagator << " time " << secs << "\n"
              << (ok ? "s UNKNOWN" : "s UNSATISFIABLE") << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cli::App app("Failed literal probing as integration test and benchmark of the propagators");
    unsigned rounds = 1;
    app.add_option("-r,--rounds", rounds, "probing rounds over all variables (CNF)")->default_val(1);
    if (auto code = cli::parse(app, argc, argv)) return *code;
    if (cli::isWcnf(app.file)) return runWcnf(readWcnf(app.file.c_str()), app.options());
    return runCnf(readCnf(app.file.c_str()), app.options(), rounds);
}
