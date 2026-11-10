// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file watch_propagator.h
 * @brief Two-watched-literal propagation with blocking literals and circular replacement search.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/propagator.h"
#include "propan/solver_types.h"
#include "propan/trail.h"

namespace propan {

/**
 * @brief Two-watched-literal propagation.
 *
 * The first two literals of every clause of size >= 2 are watched: a clause is
 * only visited when one of them becomes false. Each watch carries a blocking
 * literal of the clause; if it is true the clause is not visited at all.
 * Binary clauses are never visited: the blocking literal is the other literal
 * and the watch is tagged. The search for a replacement watch resumes where
 * the previous one stopped (the clause's pos()), wrapping around.
 *
 * The propagator reorders literals within clauses and hence needs a mutable
 * arena. Clauses that are unit under the current assignment are assigned on
 * the current level when attached; for propagation to stay complete after
 * backtracking, attach such clauses at the level of their highest false
 * literal (as CDCL does by backjumping before learning).
 */
class WatchPropagator : public Propagator {
 public:
    WatchPropagator(ClauseArena& clauses, Trail& trail)
        : clauses_(clauses), trail_(trail), watches_(trail.nVars()) { }

    bool attach(ClauseRef ref) override {
        ClauseView c = clauses_[ref];
        if (c.empty()) return false;
        if (c.size() == 1) {
            const Val v = trail_.value(c[0]);
            if (v == VAL_UNASSIGNED) trail_.assign(c[0], ref);
            return v != VAL_FALSE;
        }
        // Watch the two best literals: non-false first, then false ones by descending level.
        for (uint32_t i = 0; i < 2; ++i) {
            uint32_t best = i;
            for (uint32_t k = i + 1; k < c.size(); ++k) {
                if (rank(c[k]) > rank(c[best])) best = k;
            }
            std::swap(c[i], c[best]);
        }
        const Val v0 = trail_.value(c[0]);
        if (v0 == VAL_FALSE) return false;  // all literals false
        if (v0 == VAL_UNASSIGNED && trail_.value(c[1]) == VAL_FALSE) trail_.assign(c[0], ref);
        const bool binary = c.size() == 2;
        watches_[c[0]].emplace_back(c[1], ref, binary);
        watches_[c[1]].emplace_back(c[0], ref, binary);
        return true;
    }

    void detach(ClauseRef ref) override {
        ConstClauseView c = clauses_[ref];
        if (c.size() < 2) return;
        for (Lit lit : { c[0], c[1] }) {
            std::erase_if(watches_[lit], [ref](const Watch& w) { return w.ref() == ref; });
        }
    }

    void relocate(std::span<const ClauseArena::Relocation> relocation) override {
        for (auto& list : watches_) {
            for (Watch& w : list) w = Watch(w.blocker, relocated(relocation, w.ref()), w.binary());
        }
    }

    ClauseRef propagate() override {
        while (trail_.head < trail_.size()) {
            const ClauseRef conflict = propagateList(-trail_[trail_.head++]);
            if (conflict != ClauseRef()) return conflict;
        }
        return ClauseRef();
    }

 protected:
    /// A watched clause with a blocking literal; binary clauses are tagged in the reference word.
    struct Watch {
        static constexpr uint32_t BINARY = uint32_t(1) << 31;

        Watch(Lit blocker, ClauseRef ref, bool binary) noexcept
            : blocker(blocker), data(ref.offset() | (binary ? BINARY : 0)) {
            assert(!(ref.offset() & BINARY));
        }

        ClauseRef ref() const noexcept { return ClauseRef(data & ~BINARY); }
        bool binary() const noexcept { return data & BINARY; }

        Lit blocker;
        uint32_t data;
    };
    static_assert(sizeof(Watch) == 8);

    /// Visits the watches of a literal that became false. @return the conflicting clause or UNDEF
    ClauseRef propagateList(Lit falsified) {
        std::vector<Watch>& ws = watches_[falsified];
        Watch* const begin = ws.data();
        Watch* const end = begin + ws.size();
        Watch* i = begin;
        Watch* j = begin;
        ClauseRef conflict;
        while (i != end) {
            const Watch w = *i++;
            const Val blocker_value = trail_.value(w.blocker);
            if (blocker_value == VAL_TRUE) {
                *j++ = w;  // satisfied, clause not visited
            } else if (w.binary()) {
                *j++ = w;
                if (blocker_value == VAL_FALSE) {
                    conflict = w.ref();
                    break;
                }
                trail_.assign(w.blocker, w.ref());
            } else {
                ClauseView c = clauses_[w.ref()];
                Lit* const lits = c.data();
                if (lits[0] == falsified) std::swap(lits[0], lits[1]);
                assert(lits[1] == falsified);
                const Lit other = lits[0];
                const Val other_value = other == w.blocker ? blocker_value : trail_.value(other);
                if (other_value == VAL_TRUE) {
                    *j++ = Watch(other, w.ref(), false);  // satisfied by the other watch: remember it
                    continue;
                }
                // Circular search for a non-false replacement, resuming at pos.
                const uint32_t size = c.size();
                const uint32_t pos = c.pos();
                assert(2 <= pos && pos <= size);
                uint32_t k = pos;
                Val v = VAL_FALSE;
                while (k < size && (v = trail_.value(lits[k])) == VAL_FALSE) ++k;
                if (v == VAL_FALSE) {
                    k = 2;
                    while (k < pos && (v = trail_.value(lits[k])) == VAL_FALSE) ++k;
                }
                c.setPos(k == size ? 2 : k);
                if (v == VAL_TRUE) {
                    *j++ = Watch(lits[k], w.ref(), false);  // satisfied: only update the blocker
                } else if (v == VAL_UNASSIGNED) {
                    std::swap(lits[1], lits[k]);  // move the watch, dropping it from this list
                    watches_[lits[1]].emplace_back(other, w.ref(), false);
                } else {
                    *j++ = w;
                    if (other_value == VAL_FALSE) {
                        conflict = w.ref();
                        break;
                    }
                    trail_.assign(other, w.ref());
                }
            }
        }
        if (conflict != ClauseRef()) j = std::copy(i, end, j);
        ws.erase(ws.begin() + (j - begin), ws.end());
        return conflict;
    }

    /// Preference for watching: non-false literals, then false ones by descending level.
    uint64_t rank(Lit lit) const {
        return trail_.value(lit) == VAL_FALSE ? trail_.level(lit.var()) : UINT64_MAX;
    }

    ClauseArena& clauses_;
    Trail& trail_;
    LitMap<std::vector<Watch>> watches_;  ///< watches of clauses in which the literal is watched
};

}  // namespace propan
