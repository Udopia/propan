// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

// SLUR (single lookahead unit resolution) and its block variant SLUR(k): an
// incomplete decision procedure that never backtracks more than one block.
// Answers SATISFIABLE, UNSATISFIABLE (refuted by unit propagation at the
// root) or UNKNOWN (gave up).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include "common/cli.h"
#include "propan/solver.h"

using namespace propan;

namespace {

std::vector<Lit> permutation(unsigned n_vars, unsigned seed) {
    std::vector<Lit> perm;
    perm.reserve(n_vars);
    for (unsigned v = 1; v <= n_vars; ++v) perm.push_back(Lit(static_cast<int>(v)));
    std::mt19937 rng(seed);
    std::shuffle(perm.begin(), perm.end(), rng);
    return perm;
}

/// Next up to `k` unassigned literals of `order` from position `i` on, with polarities from bit vector `bv`.
std::vector<Lit> block(const Solver& s, const std::vector<Lit>& order, std::size_t i, unsigned k, uint64_t bv) {
    std::vector<Lit> lits;
    for (std::size_t j = i; j < order.size() && lits.size() < k; ++j) {
        if (s.value(order[j]) == VAL_UNASSIGNED) lits.push_back((bv >> lits.size()) & 1 ? order[j] : -order[j]);
    }
    return lits;
}

/// SLUR: one decision per level, each polarity tried once.
bool slur(Solver& s, const std::vector<Lit>& order) {
    for (Lit lit : order) {
        if (s.value(lit) != VAL_UNASSIGNED) continue;
        if (s.decide(lit) && s.propagate() == ClauseRef()) continue;
        s.trail().backtrack();
        if (!(s.decide(-lit) && s.propagate() == ClauseRef())) return false;
    }
    return true;
}

/// SLUR(k): a block of up to k decisions forms one level and is propagated at once.
bool slurk(Solver& s, const std::vector<Lit>& order, unsigned k) {
    for (std::size_t i = 0; i < order.size(); i += k) {
        const unsigned base = s.trail().level();
        bool conflict = true;
        for (uint64_t bv = 0; bv < (uint64_t(1) << k) && conflict; ++bv) {
            s.trail().backtrackTo(base);
            conflict = !(s.decide(block(s, order, i, k, bv)) && s.propagate() == ClauseRef());
        }
        if (conflict) return false;
    }
    return true;
}

/// SLUR(k) with immediate propagation: every decision of a block gets its own level,
/// a conflict backtracks the whole block and tries the next polarity vector.
bool slurpk(Solver& s, const std::vector<Lit>& order, unsigned k) {
    for (std::size_t i = 0; i < order.size(); i += k) {
        const unsigned base = s.trail().level();
        bool conflict = true;
        for (uint64_t bv = 0; bv < (uint64_t(1) << k) && conflict; ++bv) {
            s.trail().backtrackTo(base);
            conflict = false;
            for (Lit lit : block(s, order, i, k, bv)) {
                if (s.value(lit) != VAL_UNASSIGNED) continue;  // implied by an earlier decision of the block
                if (!(s.decide(lit) && s.propagate() == ClauseRef())) {
                    conflict = true;
                    break;
                }
            }
        }
        if (conflict) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    cli::App app("SLUR: single lookahead unit resolution, an incomplete decision procedure");
    unsigned seed = 0;
    int k = 1;
    app.add_option("-s,--seed", seed, "seed of the variable order")->default_val(0);
    app.add_option("-k,--block", k, "k > 1: SLUR(k) blocks of decisions, k < 0: SLUR(-k) with immediate propagation")
        ->default_val(1);
    if (auto code = cli::parse(app, argc, argv)) return *code;
    std::cout << "c Testing if formula " << app.file << " can be solved by SLUR algorithm." << std::endl;

    Solver s(cli::loadCnf(app.file), app.options());
    if (!s.consistent()) {
        std::cout << "s UNSATISFIABLE" << std::endl;
        return 0;
    }
    const std::vector<Lit> order = permutation(s.nVars(), seed);
    bool result;
    if (std::abs(k) <= 1) result = slur(s, order);
    else if (k > 1) result = slurk(s, order, static_cast<unsigned>(k));
    else result = slurpk(s, order, static_cast<unsigned>(-k));

    if (result) {
        std::cout << "s SATISFIABLE" << std::endl;
        std::cout << s.trail() << std::endl;
        if (!s.satisfied()) std::cout << "c ERROR: Trail does not satisfy the formula." << std::endl;
    } else {
        std::cout << "s UNKNOWN" << std::endl;
        std::cout << "c GAVE UP" << std::endl;
    }

    const Trail& trail = s.trail();
    std::vector<std::size_t> sizes;
    for (unsigned l = 0; l < trail.level(); ++l) sizes.push_back(trail.literals(l).size());
    if (sizes.empty()) return 0;
    std::sort(sizes.begin(), sizes.end());
    const double mean = std::accumulate(sizes.begin(), sizes.end(), 0.0) / sizes.size();
    double variance = 0.0;
    for (std::size_t n : sizes) variance += (n - mean) * (n - mean);
    std::cout << "c mean: " << mean << "\n"
              << "c median: " << sizes[sizes.size() / 2] << "\n"
              << "c variance: " << variance / sizes.size() << std::endl;
    return 0;
}
