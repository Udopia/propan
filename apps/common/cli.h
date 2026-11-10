// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file cli.h
 * @brief Helpers shared by the applications: command line options and formula loading.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <CLI11/CLI11.hpp>

#include "propan/cnf_formula.h"
#include "propan/dimacs.h"
#include "propan/propagators.h"
#include "propan/wcnf_formula.h"

namespace propan::cli {

/// Command line application: positional formula file and the library's options.
class App : public CLI::App {
 public:
    explicit App(std::string description) : CLI::App(std::move(description)) {
        add_option("formula", file, "CNF or WCNF file, possibly compressed")->required()->check(CLI::ExistingFile);
        add_option("-p,--propagator", propagator_, "propagation algorithm")
            ->transform(CLI::IsMember(std::vector<std::string>(std::begin(PROPAGATOR_NAMES), std::end(PROPAGATOR_NAMES))))
            ->default_str(std::string(PROPAGATOR_NAMES[0]));
    }

    /// Library options as configured on the command line.
    Options options() const { return { .propagator = propagator_ }; }

    std::string file;  ///< the formula file

 private:
    std::string propagator_ { PROPAGATOR_NAMES[0] };
};

/// Parses the command line; on failure or `--help`, prints and returns an exit code.
/// Usage: `if (auto code = cli::parse(app, argc, argv)) return *code;`
inline std::optional<int> parse(CLI::App& app, int argc, char** argv) {
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    return std::nullopt;
}

/// True if the file name denotes a WCNF file (possibly compressed).
inline bool isWcnf(std::string_view path) { return path.find(".wcnf") != std::string_view::npos; }

/// Loads a CNF formula; for a WCNF file its hard part.
inline CNFFormula loadCnf(const std::string& path) {
    if (isWcnf(path)) return std::move(readWcnf(path.c_str()).hard());
    return readCnf(path.c_str());
}

}  // namespace propan::cli
