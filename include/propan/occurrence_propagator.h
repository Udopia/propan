// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file occurrence_propagator.h
 * @brief Reference propagator using full occurrence lists.
 */

#pragma once

#include <algorithm>
#include <span>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/propagator.h"
#include "propan/solver_types.h"
#include "propan/trail.h"

namespace propan {

/**
 * @brief Propagation with full occurrence lists: every clause is visited
 * whenever one of its literals becomes false.
 *
 * Simple and read-only on the arena; serves as the reference for other
 * propagators.
 */
class OccurrencePropagator : public Propagator {
 public:
    OccurrencePropagator(const ClauseArena& clauses, Trail& trail)
        : clauses_(clauses), trail_(trail), occ_(trail.nVars()) { }

    bool attach(ClauseRef ref) override {
        auto clause = clauses_[ref];
        Lit unit = UNDEF_LIT;
        unsigned open = 0;
        for (Lit lit : clause) {
            occ_[lit].push_back(ref);
            if (trail_.value(lit) != VAL_FALSE) {
                ++open;
                unit = lit;
            }
        }
        if (open == 1 && trail_.value(unit) == VAL_UNASSIGNED) trail_.assign(unit, ref);
        return open > 0;
    }

    void detach(ClauseRef ref) override {
        for (Lit lit : clauses_[ref]) std::erase(occ_[lit], ref);
    }

    void relocate(const ClauseArena::Relocation& relocation) override {
        for (auto& list : occ_) {
            for (ClauseRef& ref : list) ref = relocation(ref);
        }
    }

    void resize(unsigned n_vars) override { occ_.resize(n_vars); }

    ClauseRef propagate() override {
        while (trail_.head < trail_.size()) {
            const Lit falsified = -trail_[trail_.head++];
            PROPAN_STAT(stats_.propagations);
            for (ClauseRef ref : occ_[falsified]) {
                PROPAN_STAT(stats_.visits);
                PROPAN_STAT(stats_.clauses);
                Lit unit = UNDEF_LIT;
                for (Lit lit : clauses_[ref]) {
                    const Val v = trail_.value(lit);
                    if (v == VAL_TRUE) goto next_clause;
                    if (v == VAL_UNASSIGNED) {
                        if (unit != UNDEF_LIT) goto next_clause;  // two unassigned: not unit
                        unit = lit;
                    }
                }
                if (unit == UNDEF_LIT) {
                    PROPAN_STAT(stats_.conflicts);
                    return ref;  // all literals false
                }
                PROPAN_STAT(stats_.assigned);
                trail_.assign(unit, ref);
            next_clause:;
            }
        }
        return ClauseRef();
    }

 private:
    const ClauseArena& clauses_;
    Trail& trail_;
    LitMap<std::vector<ClauseRef>> occ_;  ///< clauses containing each literal
};

}  // namespace propan
