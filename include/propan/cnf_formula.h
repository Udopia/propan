// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file cnf_formula.h
 * @brief Formula in conjunctive normal form, stored in a ClauseArena. See dimacs.h for file input.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <utility>

#include "propan/clause_arena.h"
#include "propan/solver_types.h"

namespace propan {

/**
 * @brief A CNF formula: a list of clauses over variables `1..nVars()`.
 *
 * Clauses are normalized when added: literals are sorted by index, duplicate
 * literals are removed and tautologies are dropped. The empty clause is kept.
 *
 * Iteration yields const clause views in physical arena order.
 * clauses() gives access to the underlying ClauseArena and its ID mapping.
 * The formula is a value type: copies are deep, moves leave the source empty.
 */
class CNFFormula {
 public:
    using const_iterator = ClauseArena::const_iterator;

    CNFFormula() = default;

    CNFFormula(const CNFFormula&) = default;
    CNFFormula& operator=(const CNFFormula&) = default;

    /// Leaves `other` empty.
    CNFFormula(CNFFormula&& other) noexcept : CNFFormula() { swap(other); }

    /// Leaves `other` empty.
    CNFFormula& operator=(CNFFormula&& other) noexcept {
        CNFFormula(std::move(other)).swap(*this);
        return *this;
    }

    void swap(CNFFormula& other) noexcept {
        clauses_.swap(other.clauses_);
        std::swap(variables_, other.variables_);
        buffer_.swap(other.buffer_);
    }

    const_iterator begin() const { return clauses_.begin(); }
    const_iterator end() const { return clauses_.end(); }

    /// Clause with the given stable ID (must be live).
    ConstClauseView operator[](ClauseId id) const { return clauses_[id]; }

    /// Clause at the given offset (must be live).
    ConstClauseView operator[](ClauseRef ref) const { return clauses_[ref]; }

    /// The underlying clause storage.
    const ClauseArena& clauses() const noexcept { return clauses_; }

    /// Mutable clause storage, e.g. for propagators that reorder literals.
    ClauseArena& clauses() noexcept { return clauses_; }

    /// Number of variables (maximum variable id).
    unsigned nVars() const noexcept { return variables_; }

    /// Number of clauses.
    std::size_t nClauses() const noexcept { return clauses_.size(); }

    /// Adds a fresh variable and returns its id.
    unsigned newVar() noexcept { return ++variables_; }

    /// Ensures that variables `1..n` exist (e.g. when declared but unused).
    void ensureVars(unsigned n) noexcept { variables_ = std::max(variables_, n); }

    /// Removes all clauses and variables.
    void clear() noexcept {
        clauses_.clear();
        variables_ = 0;
    }

    /// Normalizes and adds the clause. @return false if it was dropped as a tautology
    bool addClause(std::initializer_list<Lit> clause) {
        return addClause(std::span<const Lit>(clause.begin(), clause.size()));
    }

    /// Normalizes and adds the clause. @return false if it was dropped as a tautology
    bool addClause(std::span<const Lit> clause) {
        buffer_.assign(clause.begin(), clause.end());
        if (!normalize(buffer_)) return false;
        if (!buffer_.empty()) ensureVars(buffer_.back().var().id());
        clauses_.add(buffer_);
        return true;
    }

    /**
     * @brief Sorts the literals, removes duplicates.
     * @return false if the clause is a tautology
     */
    static bool normalize(Cl& clause) {
        std::sort(clause.begin(), clause.end());
        clause.erase(std::unique(clause.begin(), clause.end()), clause.end());
        for (std::size_t i = 1; i < clause.size(); ++i) {
            if (clause[i - 1].var() == clause[i].var()) return false;
        }
        return true;
    }

 private:
    ClauseArena clauses_;
    unsigned variables_ = 0;
    Cl buffer_;  ///< scratch space for normalization
};

}  // namespace propan
