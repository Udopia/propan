// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file trail.h
 * @brief The trail: assignment stack with decision levels, reasons and the propagation queue.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/solver_types.h"

namespace propan {

/**
 * @brief Assignment stack ("trail") with decision levels and reasons.
 *
 * The trail holds the assigned literals in assignment order and knows nothing
 * about clauses: decisions are pushed by the client, implications are
 * assigned by a Propagator, which also consumes the propagation queue
 * `[head, size())`.
 *
 * The trail is divided into decision levels. Level 0 (the root level) holds
 * units and their consequences and is never undone. newLevel() opens a level;
 * a level consists of a contiguous block of decisions followed by their
 * consequences. This shape is asserted: a level may only be opened once the
 * queue is empty (the previous level is at fixpoint or was backtracked), and
 * a decision may only be pushed before the level was propagated. After a
 * conflict, backtrack() or backtrackTo() must be called before anything else
 * is pushed.
 *
 * A decision is a literal without reason (#ClauseRef UNDEF); the decisions of
 * a level are the leading literals of literals(level) with isDecision() true.
 *
 * Assigning a literal at a level below the current one (out-of-order
 * assignment, as in chronological backtracking) is supported: such literals
 * are kept by backtrackToRetaining() and re-propagated, whereas backtrack()
 * and backtrackTo() assume there are none. Hence literals(level) is the trail
 * segment opened at that level; it may contain literals of lower levels,
 * never of higher ones. level(Var) is authoritative.
 */
class Trail {
 public:
    explicit Trail(unsigned n_vars = 0) { resize(n_vars); }

    /// Covers variables `1..n_vars`; new variables are unassigned, existing assignments are kept.
    void resize(unsigned n_vars) {
        assert(n_vars >= nVars());
        val_.resize(n_vars);
        vars_.resize(n_vars + 1);
    }

    /// Forgets all assignments and levels, keeping the variables.
    void clear() {
        trail_.clear();
        lim_.assign(1, 0);
        val_.assign(VAL_UNASSIGNED);
        head = 0;
    }

    unsigned nVars() const noexcept { return val_.nVars(); }

    /// Value of the literal (a single load): #VAL_TRUE, #VAL_FALSE or #VAL_UNASSIGNED.
    Val value(Lit lit) const { return val_[lit]; }

    /// Value of the variable.
    Val value(Var var) const { return val_[Lit(var)]; }

    /// Reason of an assigned variable; UNDEF for decisions and root facts pushed by the client.
    ClauseRef reason(Var var) const { return vars_[var].reason; }

    /// Level at which an assigned variable was assigned.
    unsigned level(Var var) const { return vars_[var].level; }

    /// True if the assigned literal is a decision (has no reason).
    bool isDecision(Lit lit) const { return reason(lit.var()) == ClauseRef(); }

    /// Current decision level (0 = root level).
    unsigned level() const noexcept { return static_cast<unsigned>(lim_.size() - 1); }

    /// Opens a new decision level. @pre the queue is empty
    void newLevel() {
        assert(head == trail_.size());
        lim_.push_back(trail_.size());
    }

    /**
     * @brief Undoes all assignments of the current decision level.
     * @return false (and does nothing) on the root level
     */
    bool backtrack() {
        if (level() == 0) return false;
        backtrackTo(level() - 1);
        return true;
    }

    /**
     * @brief Undoes all levels above the given one.
     * @pre no literal above was assigned out of order at a level `<= level`
     *      (use backtrackToRetaining() in that case; asserted)
     */
    void backtrackTo(unsigned level) {
        if (level >= this->level()) return;
        const std::size_t keep = lim_[level + 1];
        for (std::size_t i = keep; i < trail_.size(); ++i) {
            assert(vars_[trail_[i].var()].level > level);
            unassign(trail_[i]);
        }
        trail_.resize(keep);
        lim_.resize(level + 1);
        head = keep;
    }

    /**
     * @brief Undoes all levels above the given one, retaining literals that
     * were assigned out of order at a level `<= level`.
     *
     * The retained literals stay on the trail in order and are queued for
     * propagation again (chronological backtracking).
     */
    void backtrackToRetaining(unsigned level) {
        if (level >= this->level()) return;
        const std::size_t keep = lim_[level + 1];
        std::size_t j = keep;
        for (std::size_t i = keep; i < trail_.size(); ++i) {
            Lit lit = trail_[i];
            if (vars_[lit.var()].level > level) unassign(lit);
            else trail_[j++] = lit;
        }
        trail_.resize(j);
        lim_.resize(level + 1);
        head = keep;
    }

    /**
     * @brief Pushes a decision (or a root fact on level 0).
     * @pre on a non-root level, the level has not been propagated yet
     * @return false if the literal is already false; true if it is (now) true
     */
    bool push(Lit lit) {
        assert(level() == 0 || head == lim_.back());
        Val v = value(lit);
        if (v != VAL_UNASSIGNED) return v == VAL_TRUE;
        assign(lit, ClauseRef());
        return true;
    }

    /// Pushes decisions until one is already false. @return false in that case
    bool push(std::span<const Lit> lits) {
        for (Lit lit : lits) {
            if (!push(lit)) return false;
        }
        return true;
    }

    /// Assigns the unassigned literal true on the current level (propagator interface).
    void assign(Lit lit, ClauseRef reason) { assign(lit, reason, level()); }

    /// Assigns the unassigned literal true at the given level, which may be below the current one.
    void assign(Lit lit, ClauseRef reason, unsigned level) {
        assert(value(lit) == VAL_UNASSIGNED && level <= this->level());
        val_[lit] = VAL_TRUE;
        val_[-lit] = VAL_FALSE;
        vars_[lit.var()] = { reason, level };
        trail_.push_back(lit);
    }

    const Lit* begin() const noexcept { return trail_.data(); }
    const Lit* end() const noexcept { return trail_.data() + trail_.size(); }
    std::size_t size() const noexcept { return trail_.size(); }
    Lit operator[](std::size_t i) const { return trail_[i]; }

    /// All assigned literals in assignment order.
    std::span<const Lit> literals() const noexcept { return trail_; }

    /// The trail segment opened at the given level (`level <= level()`).
    std::span<const Lit> literals(unsigned level) const {
        const std::size_t first = lim_[level];
        const std::size_t last = level + 1 < lim_.size() ? lim_[level + 1] : trail_.size();
        return { trail_.data() + first, last - first };
    }

    /// Replaces the reasons of assigned variables after ClauseArena::compact().
    void relocate(const ClauseArena::Relocation& relocation) {
        for (Lit lit : trail_) {
            ClauseRef& reason = vars_[lit.var()].reason;
            if (reason != ClauseRef()) reason = relocation(reason);
        }
    }

    /// First trail position not yet propagated; the propagation queue is `[head, size())`.
    std::size_t head = 0;

 private:
    void unassign(Lit lit) { val_[lit] = val_[-lit] = VAL_UNASSIGNED; }

    struct VarInfo {
        ClauseRef reason;
        uint32_t level = 0;
    };

    std::vector<Lit> trail_;               ///< assigned literals in assignment order
    std::vector<std::size_t> lim_ { 0 };   ///< trail position where each level starts
    LitMap<Val> val_;                      ///< value per literal
    std::vector<VarInfo> vars_;            ///< reason and level per variable
};

/// Prints the assigned literals in DIMACS notation, each followed by a space.
inline std::ostream& operator<<(std::ostream& stream, const Trail& trail) {
    return stream << trail.literals();
}

/// True if every clause of the arena has a true literal.
inline bool satisfies(const Trail& trail, const ClauseArena& clauses) {
    for (auto clause : clauses) {
        if (std::none_of(clause.begin(), clause.end(), [&](Lit lit) { return trail.value(lit) == VAL_TRUE; })) {
            return false;
        }
    }
    return true;
}

}  // namespace propan
