// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file trail.h
 * @brief The trail: assignment stack with decision levels and unit propagation.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/cnf_formula.h"
#include "propan/solver_types.h"

namespace propan {

/**
 * @brief Assignment stack ("trail") with decision levels and unit propagation.
 *
 * The trail holds the assigned literals in assignment order. It is divided
 * into decision levels: level 0 (the root level) holds the units of the
 * formula and their consequences and is never undone; every call to
 * newLevel() opens a new level, to which any number of literals may be pushed
 * (decisions) before propagate() adds their consequences. backtrack() undoes
 * the most recent level.
 *
 * Values are of type Val (#VAL_TRUE, #VAL_FALSE, #VAL_UNASSIGNED), so that the
 * value of a literal is the value of its variable, negated if the literal is
 * negative.
 *
 * Propagation uses occurrence lists, i.e. every clause is visited whenever
 * one of its literals becomes false.
 */
class Trail {
 public:
    Trail() = default;

    /// Resets to an empty trail over zero variables (forgets the formula).
    void clear() {
        trail_.clear();
        trail_lim_.assign(1, 0);
        val_.clear();
        occ_.clear();
        clauses_ = nullptr;
        head_ = 0;
    }

    /**
     * @brief Initializes the trail for the given formula.
     *
     * Builds the occurrence lists, assigns the unit clauses on level 0 and
     * propagates them. The formula must outlive the trail (or the next call
     * to init()).
     * @return false if the formula is refuted by unit propagation (or contains the empty clause)
     */
    bool init(const CNFFormula& formula) {
        clear();
        clauses_ = &formula.clauses();
        occ_.resize(2 * formula.nVars() + 2);
        val_.resize(formula.nVars() + 1);
        bool ok = true;
        for (ClauseRef ref : clauses_->refs()) {
            std::span<const Lit> clause = (*clauses_)[ref];
            if (clause.empty()) ok = false;
            for (Lit lit : clause) {
                occ_[lit].push_back(ref);
            }
        }
        for (ClauseRef ref : clauses_->refs()) {
            std::span<const Lit> clause = (*clauses_)[ref];
            if (ok && clause.size() == 1) {
                ok = push(clause[0]);
            }
        }
        return ok && propagate();
    }

    /**
     * @brief Propagates all literals pushed since the last propagation to fixpoint.
     * @return false on conflict (the trail is then partially propagated; backtrack)
     */
    bool propagate() {
        const ClauseArena& clauses = *clauses_;
        while (head_ < trail_.size()) {
            Lit falsified = ~trail_[head_++];
            for (ClauseRef ref : occ_[falsified]) {
                Lit unit = UNDEF_LIT;
                for (Lit lit : clauses[ref]) {
                    Val v = value(lit);
                    if (v == VAL_TRUE) goto next_clause;  // satisfied
                    if (v == VAL_UNASSIGNED) {
                        if (unit != UNDEF_LIT) goto next_clause;  // two unassigned: not unit
                        unit = lit;
                    }
                }
                if (unit == UNDEF_LIT) return false;  // all literals false: conflict
                assign(unit);
            next_clause:;
            }
        }
        return true;
    }

    /**
     * @brief Assigns the literal true on the current level (without propagation).
     * @return false if the literal is already false; true if it is (now) true
     */
    bool push(Lit lit) {
        Val v = value(lit);
        if (v != VAL_UNASSIGNED) return v == VAL_TRUE;
        assign(lit);
        return true;
    }

    /// Opens a new decision level.
    void newLevel() { trail_lim_.push_back(static_cast<unsigned>(trail_.size())); }

    /**
     * @brief Undoes all assignments of the current decision level.
     * @return false (and does nothing) on the root level
     */
    bool backtrack() {
        if (trail_lim_.size() <= 1) return false;
        for (std::size_t i = trail_lim_.back(); i < trail_.size(); ++i) {
            val_[trail_[i].var()] = VAL_UNASSIGNED;
        }
        trail_.resize(trail_lim_.back());
        trail_lim_.pop_back();
        head_ = trail_.size();
        return true;
    }

    /// Value of the variable: #VAL_TRUE, #VAL_FALSE or #VAL_UNASSIGNED.
    Val value(Var var) const { return val_[var]; }

    /// Value of the literal: #VAL_TRUE, #VAL_FALSE or #VAL_UNASSIGNED.
    Val value(Lit lit) const {
        Val v = val_[lit.var()];
        return lit.sign() ? -v : v;
    }

    /// The i-th assigned literal.
    Lit operator[](std::size_t i) const { return trail_[i]; }

    /// Number of assigned literals.
    std::size_t size() const noexcept { return trail_.size(); }

    /// All assigned literals in assignment order.
    std::span<const Lit> literals() const noexcept { return trail_; }

    /// Number of variables of the formula given to init().
    unsigned nVars() const noexcept { return val_.empty() ? 0 : static_cast<unsigned>(val_.size() - 1); }

    /// Current decision level (0 = root level).
    unsigned level() const noexcept { return static_cast<unsigned>(trail_lim_.size() - 1); }

    /// Trail position of the first literal of the given level (`level <= level()`).
    std::size_t levelStart(unsigned level) const { return trail_lim_[level]; }

    /// Prints the assigned literals in DIMACS notation on one line.
    void print(std::ostream& out = std::cout) const {
        for (Lit lit : trail_) {
            out << lit << " ";
        }
        out << std::endl;
    }

    /// True if every clause of the formula has a true literal.
    bool satisfies(const CNFFormula& formula) const {
        for (auto clause : formula) {
            bool sat = false;
            for (Lit lit : clause) {
                if (value(lit) == VAL_TRUE) {
                    sat = true;
                    break;
                }
            }
            if (!sat) return false;
        }
        return true;
    }

 private:
    /// Assigns the unassigned literal true on the current level.
    void assign(Lit lit) {
        val_[lit.var()] = lit.sign() ? VAL_FALSE : VAL_TRUE;
        trail_.push_back(lit);
    }

    std::vector<Lit> trail_;                      ///< assigned literals in assignment order
    std::vector<unsigned> trail_lim_ { 0 };       ///< trail position where each level starts
    std::vector<Val> val_;                        ///< value per variable
    std::vector<std::vector<ClauseRef>> occ_;     ///< occurrence list per literal
    const ClauseArena* clauses_ = nullptr;        ///< clauses of the formula given to init()
    std::size_t head_ = 0;                        ///< first trail position not yet propagated
};

}  // namespace propan
