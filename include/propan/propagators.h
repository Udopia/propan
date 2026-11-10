// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file propagators.h
 * @brief All propagator implementations and their selection by name.
 */

#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "propan/clause_arena.h"
#include "propan/occurrence_propagator.h"
#include "propan/propagator.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"

namespace propan {

/// Names accepted as Options::propagator; the first one is the default.
inline constexpr std::string_view PROPAGATOR_NAMES[] = { "watch", "occ" };

/// Configuration of propagation. Fields are added as variants and options appear.
struct Options {
    std::string_view propagator = PROPAGATOR_NAMES[0];
};

/**
 * @brief Creates the configured propagator over the given arena and trail.
 * @throw std::invalid_argument for an unknown propagator name
 */
inline std::unique_ptr<Propagator> makePropagator(const Options& options, ClauseArena& clauses, Trail& trail) {
    if (options.propagator == "watch") return std::make_unique<WatchPropagator>(clauses, trail);
    if (options.propagator == "occ") return std::make_unique<OccurrencePropagator>(clauses, trail);
    throw std::invalid_argument("propan: unknown propagator '" + std::string(options.propagator) + "'");
}

}  // namespace propan
