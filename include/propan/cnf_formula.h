// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file cnf_formula.h
 * @brief Formula in conjunctive normal form, stored in a ClauseArena.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <utility>

#include "propan/clause_arena.h"
#include "propan/solver_types.h"
#include "propan/stream_buffer.h"

namespace propan {

/**
 * @brief A CNF formula: a list of clauses over variables `1..nVars()`.
 *
 * Clauses are normalized when added: literals are sorted by index, duplicate
 * literals are removed and tautologies are dropped. The empty clause is kept.
 *
 * Iteration yields the clauses as `std::span<const Lit>` in insertion order;
 * clauses() gives access to the underlying ClauseArena and its references.
 * The formula is a value type: copies are deep, moves leave the source empty.
 */
class CNFFormula {
 public:
    using const_iterator = ClauseArena::const_iterator;

    CNFFormula() = default;

    /**
     * @brief Reads a DIMACS CNF file, possibly compressed.
     * @throw ParserException on I/O or format errors
     */
    explicit CNFFormula(const char* filename) { readDimacsFromFile(filename); }

    CNFFormula(const CNFFormula&) = default;
    CNFFormula& operator=(const CNFFormula&) = default;

    /// Leaves `other` empty.
    CNFFormula(CNFFormula&& other) noexcept
        : clauses_(std::move(other.clauses_)), variables_(std::exchange(other.variables_, 0)) { }

    /// Leaves `other` empty.
    CNFFormula& operator=(CNFFormula&& other) noexcept {
        if (this != &other) {
            clauses_ = std::move(other.clauses_);
            variables_ = std::exchange(other.variables_, 0);
        }
        return *this;
    }

    const_iterator begin() const { return clauses_.begin(); }
    const_iterator end() const { return clauses_.end(); }

    /// Literals of the i-th clause.
    std::span<const Lit> operator[](std::size_t i) const { return clauses_[i]; }

    /// Literals of the referenced clause.
    std::span<const Lit> operator[](ClauseRef ref) const { return clauses_[ref]; }

    /// The underlying clause storage.
    const ClauseArena& clauses() const noexcept { return clauses_; }

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

    /**
     * @brief Reads clauses from a DIMACS CNF file, possibly compressed, and adds them.
     *
     * Variables declared in the header count in nVars() even if unused.
     * @throw ParserException on I/O or format errors
     */
    void readDimacsFromFile(const char* filename) {
        StreamBuffer in(filename);
        Cl clause;
        while (in.skipWhitespace()) {
            if (*in == 'p' || *in == 'c') {
                if (*in == 'p') {
                    in.skipString("p cnf");
                    int vars;
                    if (in.readInteger(&vars) && vars > 0) {
                        ensureVars(static_cast<unsigned>(vars));
                    }
                }
                if (!in.skipLine()) break;
            } else {
                int plit;
                while (in.readInteger(&plit)) {
                    if (plit == 0) break;
                    clause.push_back(Lit::fromDimacs(plit));
                }
                addClause(clause);
                clause.clear();
            }
        }
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
