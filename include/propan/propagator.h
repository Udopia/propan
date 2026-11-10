// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file propagator.h
 * @brief Interface of unit propagation over a Trail.
 */

#pragma once

#include <span>

#include "propan/clause_arena.h"

namespace propan {

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
    virtual void relocate(std::span<const ClauseArena::Relocation> relocation) = 0;

    /**
     * @brief Propagates the trail's queue to fixpoint.
     * @return the conflicting clause, or UNDEF if there is none
     */
    virtual ClauseRef propagate() = 0;

    /// Attaches all clauses of the arena. @return false if any attach() failed
    bool attachAll(const ClauseArena& clauses) {
        bool ok = true;
        for (auto clause : clauses) ok = attach(clause.ref()) && ok;
        return ok;
    }
};

}  // namespace propan
