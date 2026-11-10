// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file dimacs.h
 * @brief Readers for DIMACS CNF and WCNF files, possibly compressed.
 */

#pragma once

#include <cstdint>

#include "propan/cnf_formula.h"
#include "propan/solver_types.h"
#include "propan/stream_buffer.h"
#include "propan/wcnf_formula.h"

namespace propan {

namespace detail {

/// Reads literals up to the terminating 0 (or the end of the file) into `clause`.
inline void readClauseBody(StreamBuffer& in, Cl& clause) {
    clause.clear();
    for (int lit; in.read(lit) && lit != 0;) clause.push_back(Lit(lit));
}

}  // namespace detail

/**
 * @brief Reads a DIMACS CNF file and adds its clauses to `formula`.
 *
 * Variables declared in the header count in nVars() even if unused.
 * @throw ParserException on I/O or format errors
 */
inline void readCnf(const char* filename, CNFFormula& formula) {
    StreamBuffer in(filename);
    Cl clause;
    while (in.skipWhitespace()) {
        if (*in == 'p' || *in == 'c') {
            if (*in == 'p') {
                in.skipString("p cnf");
                int vars;
                if (in.read(vars) && vars > 0) formula.ensureVars(static_cast<unsigned>(vars));
            }
            if (!in.skipLine()) break;
        } else {
            detail::readClauseBody(in, clause);
            formula.addClause(clause);
        }
    }
}

/// Reads a DIMACS CNF file into a new formula. @throw ParserException on I/O or format errors
inline CNFFormula readCnf(const char* filename) {
    CNFFormula formula;
    readCnf(filename, formula);
    return formula;
}

/**
 * @brief Reads a WCNF file and adds its clauses to `formula`.
 *
 * Supports both the pre-2022 format (header `p wcnf <vars> <clauses> [<top>]`,
 * clauses with weight >= top are hard; without top all clauses are soft) and
 * the 2022 MaxSAT Evaluation format (no header, hard clauses start with 'h').
 * @throw ParserException on I/O or format errors
 */
inline void readWcnf(const char* filename, WCNFFormula& formula) {
    StreamBuffer in(filename);
    Cl clause;
    bool has_top = false;
    uint64_t top = 0;
    while (in.skipWhitespace()) {
        if (*in == 'p' || *in == 'c') {
            if (*in == 'p') {
                in.skipString("p wcnf");
                int vars, n_clauses;
                if (in.read(vars) && vars > 0) formula.ensureVars(static_cast<unsigned>(vars));
                in.read(n_clauses);
                has_top = in.skipBlanks() && in.read(top);
            }
            if (!in.skipLine()) break;
        } else if (*in == 'h') {
            in.skip();
            detail::readClauseBody(in, clause);
            formula.addHardClause(clause);
        } else {
            uint64_t weight = 1;
            in.read(weight);
            detail::readClauseBody(in, clause);
            if (has_top && weight >= top) formula.addHardClause(clause);
            else formula.addSoftClause(clause, weight);
        }
    }
}

/// Reads a WCNF file into a new formula. @throw ParserException on I/O or format errors
inline WCNFFormula readWcnf(const char* filename) {
    WCNFFormula formula;
    readWcnf(filename, formula);
    return formula;
}

}  // namespace propan
