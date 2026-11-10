// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file wcnf_formula.h
 * @brief Weighted CNF formula: hard clauses and weighted soft clauses. See dimacs.h for file input.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "propan/cnf_formula.h"
#include "propan/solver_types.h"

namespace propan {

/// A soft clause: normalized literals and a weight.
struct SoftClause {
    Cl lits;
    uint64_t weight;
};

/**
 * @brief A weighted CNF formula: hard clauses in a CNFFormula and soft clauses
 * with weights.
 *
 * Soft clauses are normalized like hard ones (sorted, duplicates removed,
 * tautologies dropped) and kept in order of addition. The variable range
 * `1..nVars()` covers both parts. What soft clauses mean (relaxation,
 * bounds, ...) is up to the application.
 */
class WCNFFormula {
 public:
    /// The hard clauses.
    const CNFFormula& hard() const noexcept { return hard_; }

    /// The hard clauses; e.g. `CNFFormula f = std::move(w.hard());`.
    CNFFormula& hard() noexcept { return hard_; }

    /// The soft clauses in order of addition.
    std::span<const SoftClause> soft() const noexcept { return soft_; }

    /// Number of variables (maximum variable id over both parts).
    unsigned nVars() const noexcept { return std::max(hard_.nVars(), soft_vars_); }

    /// Number of hard clauses.
    std::size_t nHardClauses() const noexcept { return hard_.nClauses(); }

    /// Number of soft clauses.
    std::size_t nSoftClauses() const noexcept { return soft_.size(); }

    /// Ensures that variables `1..n` exist (e.g. when declared but unused).
    void ensureVars(unsigned n) noexcept { hard_.ensureVars(n); }

    /// Normalizes and adds a hard clause. @return false if it was dropped as a tautology
    bool addHardClause(std::span<const Lit> clause) { return hard_.addClause(clause); }

    /// Normalizes and adds a soft clause. @return false if it was dropped as a tautology
    bool addSoftClause(std::span<const Lit> clause, uint64_t weight) {
        SoftClause soft { Cl(clause.begin(), clause.end()), weight };
        if (!CNFFormula::normalize(soft.lits)) return false;
        if (!soft.lits.empty()) soft_vars_ = std::max(soft_vars_, soft.lits.back().var().id());
        soft_.push_back(std::move(soft));
        return true;
    }

    /// Removes all clauses and variables.
    void clear() noexcept {
        hard_.clear();
        soft_.clear();
        soft_vars_ = 0;
    }

 private:
    CNFFormula hard_;
    std::vector<SoftClause> soft_;
    unsigned soft_vars_ = 0;  ///< largest variable in a soft clause
};

}  // namespace propan
