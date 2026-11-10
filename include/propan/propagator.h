// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file propagator.h
 * @brief Interface of unit propagation over a Trail.
 */

#pragma once

#include <cstdint>

#include "propan/clause_arena.h"

namespace propan {

/**
 * @brief Propagation counters, maintained only if compiled with `PROPAN_STATS`
 * (CMake option of the same name); otherwise all increments compile to nothing
 * and the counters stay zero.
 */
struct PropagationStats {
    uint64_t propagations = 0;  ///< literals taken from the queue
    uint64_t visits = 0;        ///< watches or occurrences inspected
    uint64_t clauses = 0;       ///< clauses whose literals were inspected
    uint64_t assigned = 0;      ///< literals assigned by propagation
    uint64_t conflicts = 0;     ///< conflicts found
};

#ifdef PROPAN_STATS
#define PROPAN_STAT(counter) (++(counter))
#else
#define PROPAN_STAT(counter) ((void)0)
#endif

/**
 * @brief Unit propagation: maintains clause indexing structures and assigns
 * implied literals on a Trail.
 *
 * A propagator is bound to one ClauseArena and one Trail. It owns the order of
 * literals within clauses; size and identity changes of the arena must be
 * announced via attach(), detach() and relocate(). The virtual interface is
 * only crossed once per call, so implementations inline the Trail's accessors
 * in their loops.
 */
class Propagator {
 public:
    virtual ~Propagator() = default;

    /**
     * @brief Starts tracking a clause of the arena.
     *
     * If all but one literal are false, the remaining one is assigned with the
     * clause as reason (a unit clause is assigned on the current level).
     * @return false if the clause is empty or all its literals are false
     */
    virtual bool attach(ClauseRef ref) = 0;

    /// Stops tracking a clause; call before erasing it from the arena.
    virtual void detach(ClauseRef ref) = 0;

    /// Updates references after ClauseArena::compact().
    virtual void relocate(const ClauseArena::Relocation& relocation) = 0;

    /// Extends the per-literal structures to cover variables `1..n_vars` (after Trail::resize()).
    virtual void resize(unsigned n_vars) = 0;

    /**
     * @brief Propagates the trail's queue to fixpoint.
     * @return the conflicting clause, or UNDEF if there is none
     */
    virtual ClauseRef propagate() = 0;

    /// Counters (all zero unless compiled with PROPAN_STATS).
    const PropagationStats& stats() const noexcept { return stats_; }

 protected:
    PropagationStats stats_;
};

}  // namespace propan
