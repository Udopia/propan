// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file solver.h
 * @brief Solver state: formula, trail and propagator kept consistent.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "propan/clause_arena.h"
#include "propan/cnf_formula.h"
#include "propan/propagator.h"
#include "propan/propagators.h"
#include "propan/solver_types.h"
#include "propan/trail.h"

namespace propan {

/**
 * @brief Owns a formula, a trail over its variables and a propagator attached
 * to its clauses, and keeps the three consistent.
 *
 * This is the state of a search procedure, not a search procedure itself:
 * decisions, backtracking and conflict handling are up to the caller (see
 * the applications). Construction attaches all clauses and propagates the
 * root level; clauses added through add() are attached; compaction relocates
 * the references held by trail and propagator.
 *
 * Typical loop:
 * @code
 * Solver s(readCnf(path));
 * while (s.consistent() && !s.complete()) {
 *     s.decide(pickLiteral(s));
 *     if (ClauseRef conflict = s.propagate(); conflict != ClauseRef()) {
 *         // analyze s.clauses()[conflict] using s.trail().reason()/level(),
 *         // s.trail().backtrackTo(level), s.learn(clause)
 *     }
 * }
 * @endcode
 */
class Solver {
 public:
    /// Takes over the formula, attaches all its clauses and propagates the root level. Check consistent() afterwards.
    explicit Solver(CNFFormula formula, const Options& options = {})
        : formula_(std::move(formula)), trail_(formula_.nVars()),
          propagator_(makePropagator(options, formula_.clauses(), trail_)) {
        for (auto clause : formula_.clauses()) consistent_ = propagator_->attach(clause.ref()) && consistent_;
        if (consistent_) propagate();
    }

    /// False once the formula was refuted: a clause was falsified or a conflict found at the root level.
    bool consistent() const noexcept { return consistent_; }

    const CNFFormula& formula() const noexcept { return formula_; }
    const ClauseArena& clauses() const noexcept { return formula_.clauses(); }
    Trail& trail() noexcept { return trail_; }
    const Trail& trail() const noexcept { return trail_; }
    Propagator& propagator() noexcept { return *propagator_; }
    unsigned nVars() const noexcept { return trail_.nVars(); }

    /// Adds a fresh, unassigned variable to formula, trail and propagator. @return its id
    Var newVar() {
        const unsigned v = formula_.newVar();
        trail_.resize(v);
        propagator_->resize(v);
        return Var(v);
    }

    /**
     * @brief Normalizes, adds and attaches a clause.
     *
     * A clause that is unit under the current assignment is propagated at the
     * current level; the caller is responsible for being at the right level
     * (e.g. backjumping before adding a learned clause).
     * @return false if the clause was a tautology (nothing added) or is falsified
     */
    bool add(std::span<const Lit> clause, ClauseFlags flags = {}) {
        if (!formula_.addClause(clause)) return false;
        const ClauseRef ref = formula_.clauses().ref(ClauseId(static_cast<uint32_t>(formula_.nClauses() - 1)));
        if (flags != ClauseFlags()) formula_.clauses().setFlags(ref, flags);
        if (!propagator_->attach(ref)) {
            if (trail_.level() == 0) consistent_ = false;
            return false;
        }
        return true;
    }

    /**
     * @brief Adds and attaches a learned clause.
     * @pre the clause is asserting: all literals but at most one are false
     *      (after backjumping to the right level); asserted
     */
    bool learn(std::span<const Lit> clause) {
        assert(std::count_if(clause.begin(), clause.end(), [&](Lit l) { return value(l) != VAL_FALSE; }) <= 1);
        ClauseFlags flags;
        flags.setLearned();
        return add(clause, flags);
    }

    /// Detaches and erases a clause. @pre the clause is not the reason of an assigned variable
    void erase(ClauseRef ref) {
        propagator_->detach(ref);
        formula_.clauses().erase(ref);
    }

    /// Removes dead storage from the arena and relocates all references held by trail and propagator.
    void compact() {
        const auto relocation = formula_.clauses().compact();
        trail_.relocate(relocation);
        propagator_->relocate(relocation);
    }

    /// Propagates to fixpoint. @return the conflicting clause or UNDEF
    ClauseRef propagate() {
        const ClauseRef conflict = propagator_->propagate();
        if (conflict != ClauseRef() && trail_.level() == 0) consistent_ = false;
        return conflict;
    }

    /// Opens a level and pushes decisions. @return false if a decision was already false
    bool decide(std::span<const Lit> decisions) {
        trail_.newLevel();
        return trail_.push(decisions);
    }

    /// Opens a level and pushes one decision. @return false if it was already false
    bool decide(Lit decision) { return decide(std::span<const Lit>(&decision, 1)); }

    /// Value of a literal.
    Val value(Lit lit) const { return trail_.value(lit); }

    /// Value of a variable.
    Val value(Var var) const { return trail_.value(var); }

    /// True if every variable is assigned.
    bool complete() const noexcept { return trail_.size() == trail_.nVars(); }

    /// True if the current assignment satisfies every clause.
    bool satisfied() const { return satisfies(trail_, formula_.clauses()); }

 private:
    CNFFormula formula_;
    Trail trail_;
    std::unique_ptr<Propagator> propagator_;
    bool consistent_ = true;
};

}  // namespace propan
