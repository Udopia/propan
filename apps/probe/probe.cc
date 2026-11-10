// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

// Probing with unit propagation: failed literals and lifted implications on
// CNF formulas; failed soft literals and joint satisfiability of the soft
// literals on WCNF formulas. Serves as integration test and benchmark of the
// propagators: all counters depend only on propagation fixpoints and must be
// identical for every propagator.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "propan/binary_first_propagator.h"
#include "propan/cnf_formula.h"
#include "propan/occurrence_propagator.h"
#include "propan/propagator.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"
#include "propan/wcnf_formula.h"

using namespace propan;

namespace {

struct Stats {
    std::uint64_t probes = 0;    ///< decision levels opened for probing
    std::uint64_t implied = 0;   ///< literals implied by probes without conflict
    std::uint64_t failed = 0;    ///< probes ending in a conflict
    std::uint64_t lifted = 0;    ///< literals implied by both polarities of a variable
};

std::unique_ptr<Propagator> makePropagator(const std::string& name, ClauseArena& clauses, Trail& trail) {
    if (name == "occ") return std::make_unique<OccurrencePropagator>(clauses, trail);
    if (name == "bin") return std::make_unique<BinaryFirstPropagator>(clauses, trail);
    return std::make_unique<WatchPropagator>(clauses, trail);
}

/// Probes `lit` on a new level. @return false on conflict; the level is left open otherwise
bool probe(Trail& trail, Propagator& prop, Lit lit, Stats& stats) {
    ++stats.probes;
    trail.newLevel();
    trail.push(lit);
    if (prop.propagate() != ClauseRef()) {
        ++stats.failed;
        trail.backtrack();
        return false;
    }
    stats.implied += trail.literals(trail.level()).size() - 1;
    return true;
}

/// Assigns a root fact and propagates. @return false if the formula is refuted
bool fixRoot(Trail& trail, Propagator& prop, Lit lit) {
    return trail.push(lit) && prop.propagate() == ClauseRef();
}

/// Failed literal probing with lifting of common implications, over all unassigned variables.
/// @return false if the formula is refuted
bool probeVariables(Trail& trail, Propagator& prop, Stats& stats, unsigned rounds) {
    std::vector<unsigned char> mark(2 * trail.nVars() + 2, 0);
    std::vector<Lit> first, common;
    for (unsigned round = 0; round < rounds; ++round) {
        for (unsigned v = 1; v <= trail.nVars(); ++v) {
            const Lit pos(static_cast<int>(v));
            if (trail.value(pos) != VAL_UNASSIGNED) continue;
            if (!probe(trail, prop, pos, stats)) {
                if (!fixRoot(trail, prop, -pos)) return false;
                continue;
            }
            const auto implied = trail.literals(trail.level()).subspan(1);
            first.assign(implied.begin(), implied.end());
            trail.backtrack();
            for (Lit l : first) mark[l.index()] = 1;
            const bool other_ok = probe(trail, prop, -pos, stats);
            common.clear();
            if (other_ok) {
                for (Lit l : trail.literals(trail.level()).subspan(1)) {
                    if (mark[l.index()]) common.push_back(l);
                }
                trail.backtrack();
            }
            for (Lit l : first) mark[l.index()] = 0;
            if (!other_ok) {
                if (!fixRoot(trail, prop, pos)) return false;
                continue;
            }
            stats.lifted += common.size();
            for (Lit l : common) {
                if (!fixRoot(trail, prop, l)) return false;
            }
        }
    }
    return true;
}

int runCnf(CNFFormula& f, const std::string& prop_name, unsigned rounds) {
    const auto t0 = std::chrono::steady_clock::now();
    Trail trail(f.nVars());
    auto prop = makePropagator(prop_name, f.clauses(), trail);
    Stats stats;
    bool ok = prop->attachAll(f.clauses()) && prop->propagate() == ClauseRef();
    const std::size_t root_units = trail.size();
    if (ok) ok = probeVariables(trail, *prop, stats, rounds);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "c variables " << f.nVars() << " clauses " << f.nClauses() << "\n"
              << "c root-units " << root_units << "\n"
              << "c probes " << stats.probes << " failed " << stats.failed << " lifted " << stats.lifted
              << " implied " << stats.implied << "\n"
              << "c root-assigned " << (ok ? trail.literals(0).size() : 0) << "\n"
              << "c propagator " << prop_name << " time " << secs << "\n"
              << (ok ? "s UNKNOWN" : "s UNSATISFIABLE") << std::endl;
    return 0;
}

int runWcnf(WCNFFormula& f, const std::string& prop_name) {
    const auto t0 = std::chrono::steady_clock::now();
    Trail trail(f.nVars());
    auto prop = makePropagator(prop_name, f.hard().clauses(), trail);
    Stats stats;
    bool ok = prop->attachAll(f.hard().clauses()) && prop->propagate() == ClauseRef();
    if (ok) ok = probeVariables(trail, *prop, stats, 1);  // preprocess the hard part
    // Soft literals: unit soft clauses.
    std::vector<Lit> softs;
    std::vector<std::uint64_t> weights;
    for (auto clause : f.soft()) {
        if (clause.size() == 1) {
            softs.push_back(clause[0]);
            weights.push_back(f.weights()[clause.id().index()]);
        }
    }
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
        } else if (probe(trail, *prop, l, stats)) {
            trail.backtrack();
            open.push_back(l);
        } else {
            lower_bound += weights[i];
            ok = fixRoot(trail, *prop, -l);
        }
    }
    // Push all open soft literals as one block and see whether propagation accepts them.
    bool block_consistent = false;
    std::uint64_t block_implied = 0;
    if (ok) {
        trail.newLevel();
        block_consistent = trail.push(open) && prop->propagate() == ClauseRef();
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
            if (trail.value(l) == VAL_FALSE || !probe(trail, *prop, l, stats)) {
                ++greedy_skipped;
                greedy_weight += weights[i];
            }
        }
        trail.backtrackTo(0);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "c variables " << f.nVars() << " hard " << f.nHardClauses() << " soft " << f.nSoftClauses()
              << " soft-units " << softs.size() << "\n"
              << "c probes " << stats.probes << " failed " << stats.failed << " lifted " << stats.lifted
              << " implied " << stats.implied << "\n"
              << "c soft-true " << already_true << " lower-bound " << lower_bound << "\n"
              << "c block " << (block_consistent ? "consistent" : "conflicting") << " implied " << block_implied << "\n"
              << "c greedy-skipped " << greedy_skipped << " weight " << greedy_weight << "\n"
              << "c propagator " << prop_name << " time " << secs << "\n"
              << (ok ? "s UNKNOWN" : "s UNSATISFIABLE") << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "USAGE: probe <formula.cnf|formula.wcnf> [watch|occ|bin] [rounds]" << std::endl;
        return 0;
    }
    const std::string file = argv[1];
    const std::string prop_name = argc > 2 ? argv[2] : "watch";
    const unsigned rounds = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 1;
    if (file.find(".wcnf") != std::string::npos) {
        WCNFFormula f(file.c_str());
        return runWcnf(f, prop_name);
    }
    CNFFormula f(file.c_str());
    return runCnf(f, prop_name, rounds);
}
