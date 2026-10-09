// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file wcnf_formula.h
 * @brief Weighted CNF formula (MaxSAT): hard clauses and weighted soft clauses.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "propan/cnf_formula.h"
#include "propan/solver_types.h"
#include "propan/stream_buffer.h"

namespace propan {

/**
 * @brief A weighted CNF formula: hard clauses and soft clauses with weights.
 *
 * Hard and soft clauses are kept in two CNFFormula objects, which share the
 * variable range `1..nVars()`. The i-th soft clause has weight `weights()[i]`.
 * Clauses are normalized as in CNFFormula; dropped tautologies get no weight.
 * The formula is a value type: copies are deep, moves leave the source empty.
 */
class WCNFFormula {
 public:
    WCNFFormula() = default;

    /**
     * @brief Reads a WCNF file, possibly compressed.
     * @throw ParserException on I/O or format errors
     */
    explicit WCNFFormula(const char* filename) { readDimacsFromFile(filename); }

    WCNFFormula(const WCNFFormula&) = default;
    WCNFFormula& operator=(const WCNFFormula&) = default;

    /// Leaves `other` empty.
    WCNFFormula(WCNFFormula&& other) noexcept
        : hard_(std::move(other.hard_)), soft_(std::move(other.soft_)),
          weights_(std::exchange(other.weights_, {})) { }

    /// Leaves `other` empty.
    WCNFFormula& operator=(WCNFFormula&& other) noexcept {
        if (this != &other) {
            hard_ = std::move(other.hard_);
            soft_ = std::move(other.soft_);
            weights_ = std::exchange(other.weights_, {});
        }
        return *this;
    }

    /// The hard clauses.
    const CNFFormula& hard() const noexcept { return hard_; }

    /// The hard clauses; e.g. `CNFFormula f = std::move(w.hard());`.
    CNFFormula& hard() noexcept { return hard_; }

    /// The soft clauses.
    const CNFFormula& soft() const noexcept { return soft_; }

    /// The soft clauses.
    CNFFormula& soft() noexcept { return soft_; }

    /// Weights of the soft clauses.
    const std::vector<uint64_t>& weights() const noexcept { return weights_; }

    /// Number of variables (maximum variable id).
    unsigned nVars() const noexcept { return std::max(hard_.nVars(), soft_.nVars()); }

    /// Total number of clauses.
    std::size_t nClauses() const noexcept { return hard_.nClauses() + soft_.nClauses(); }

    /// Number of hard clauses.
    std::size_t nHardClauses() const noexcept { return hard_.nClauses(); }

    /// Number of soft clauses.
    std::size_t nSoftClauses() const noexcept { return soft_.nClauses(); }

    /// Adds a fresh variable and returns its id.
    unsigned newVar() noexcept {
        unsigned v = hard_.newVar();
        soft_.ensureVars(v);
        return v;
    }

    /// Ensures that variables `1..n` exist.
    void ensureVars(unsigned n) noexcept {
        hard_.ensureVars(n);
        soft_.ensureVars(n);
    }

    /// Removes all clauses, weights and variables.
    void clear() noexcept {
        hard_.clear();
        soft_.clear();
        weights_.clear();
    }

    /// Normalizes and adds a hard clause. @return false if it was dropped as a tautology
    bool addHardClause(std::span<const Lit> clause) {
        bool added = hard_.addClause(clause);
        ensureVars(std::max(hard_.nVars(), soft_.nVars()));
        return added;
    }

    /// Normalizes and adds a soft clause. @return false if it was dropped as a tautology
    bool addSoftClause(std::span<const Lit> clause, uint64_t weight) {
        bool added = soft_.addClause(clause);
        if (added) weights_.push_back(weight);
        ensureVars(std::max(hard_.nVars(), soft_.nVars()));
        return added;
    }

    /**
     * @brief Reads clauses from a WCNF file, possibly compressed, and adds them.
     *
     * Supports both the pre-2022 format (header `p wcnf <vars> <clauses> [<top>]`,
     * clauses with weight >= top are hard; without top all clauses are soft)
     * and the 2022 MaxSAT Evaluation format (no header, hard clauses start
     * with 'h').
     * @throw ParserException on I/O or format errors
     */
    void readDimacsFromFile(const char* filename) {
        StreamBuffer in(filename);
        Cl clause;
        bool has_top = false;
        uint64_t top = 0;
        while (in.skipWhitespace()) {
            if (*in == 'p' || *in == 'c') {
                if (*in == 'p') {
                    in.skipString("p wcnf");
                    int vars, n_clauses;
                    if (in.readInteger(&vars) && vars > 0) {
                        ensureVars(static_cast<unsigned>(vars));
                    }
                    in.readInteger(&n_clauses);
                    has_top = in.skipBlanks() && in.readUInt64(&top);
                }
                if (!in.skipLine()) break;
            } else {
                bool hard = false;
                uint64_t weight = 1;
                if (*in == 'h') {
                    hard = true;
                    in.skip();
                } else {
                    in.readUInt64(&weight);
                    hard = has_top && weight >= top;
                }
                int plit;
                while (in.readInteger(&plit)) {
                    if (plit == 0) break;
                    clause.push_back(Lit::fromDimacs(plit));
                }
                if (hard) {
                    addHardClause(clause);
                } else {
                    addSoftClause(clause, weight);
                }
                clause.clear();
            }
        }
    }

 private:
    CNFFormula hard_;
    CNFFormula soft_;
    std::vector<uint64_t> weights_;
};

}  // namespace propan
