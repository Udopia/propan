// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file binary_first_propagator.h
 * @brief Two-watched-literal propagation with separate, flat binary clause lists visited first.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/solver_types.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"

namespace propan {

/**
 * @brief WatchPropagator variant that keeps binary clauses out of the watch
 * lists and propagates them first.
 *
 * Binary implications are stored in one flat array in CSR layout: the
 * implications of a literal are a contiguous range, so visiting them is a
 * linear scan without tags or indirection. The layout is rebuilt lazily
 * before the next propagation after binary clauses were attached or
 * detached (O(binary clauses)); it is meant for formulas whose binary part is
 * static. Longer clauses are handled exactly as in WatchPropagator.
 */
class BinaryFirstPropagator : public WatchPropagator {
 public:
    using WatchPropagator::WatchPropagator;

    bool attach(ClauseRef ref) override {
        ConstClauseView c = clauses_[ref];
        if (c.size() != 2) return WatchPropagator::attach(ref);
        const Val v0 = trail_.value(c[0]);
        const Val v1 = trail_.value(c[1]);
        if (v0 == VAL_FALSE && v1 == VAL_UNASSIGNED) trail_.assign(c[1], ref);
        if (v1 == VAL_FALSE && v0 == VAL_UNASSIGNED) trail_.assign(c[0], ref);
        entries_.push_back({ c[0], { c[1], ref } });
        entries_.push_back({ c[1], { c[0], ref } });
        dirty_ = true;
        return v0 != VAL_FALSE || v1 != VAL_FALSE;
    }

    void detach(ClauseRef ref) override {
        if (clauses_[ref].size() != 2) return WatchPropagator::detach(ref);
        std::erase_if(entries_, [ref](const Entry& e) { return e.second.ref == ref; });
        dirty_ = true;
    }

    void relocate(std::span<const ClauseArena::Relocation> relocation) override {
        WatchPropagator::relocate(relocation);
        for (Entry& e : entries_) e.second.ref = relocated(relocation, e.second.ref);
        for (Implication& b : binaries_) b.ref = relocated(relocation, b.ref);
    }

    ClauseRef propagate() override {
        if (dirty_) rebuild();
        while (trail_.head < trail_.size()) {
            const Lit falsified = -trail_[trail_.head++];
            const uint32_t index = falsified.index();
            for (uint32_t i = start_[index]; i < start_[index + 1]; ++i) {
                const Implication& b = binaries_[i];
                const Val v = trail_.value(b.other);
                if (v == VAL_TRUE) continue;
                if (v == VAL_FALSE) return b.ref;
                trail_.assign(b.other, b.ref);
            }
            const ClauseRef conflict = propagateList(falsified);
            if (conflict != ClauseRef()) return conflict;
        }
        return ClauseRef();
    }

 private:
    /// Binary clause as seen from one of its literals: the other literal and the clause.
    struct Implication {
        Lit other;
        ClauseRef ref;
    };
    using Entry = std::pair<Lit, Implication>;  ///< implication filed under the literal that triggers it

    /// Builds the CSR layout from entries_ by counting sort on the triggering literal.
    void rebuild() {
        start_.assign(2 * trail_.nVars() + 3, 0);
        for (const Entry& e : entries_) ++start_[e.first.index() + 1];
        for (std::size_t i = 1; i < start_.size(); ++i) start_[i] += start_[i - 1];
        binaries_.resize(entries_.size());
        std::vector<uint32_t> next(start_.begin(), start_.end() - 1);
        for (const Entry& e : entries_) binaries_[next[e.first.index()]++] = e.second;
        dirty_ = false;
    }

    std::vector<Entry> entries_;          ///< all binary implications, unsorted
    std::vector<Implication> binaries_;   ///< implications grouped by triggering literal
    std::vector<uint32_t> start_;         ///< binaries_ range of literal index i is [start_[i], start_[i + 1])
    bool dirty_ = true;
};

}  // namespace propan
