/*************************************************************************************************
WCNFFormula -- Copyright (c) 2021, Ashlin Iser, KIT - Karlsruhe Institute of Technology

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **************************************************************************************************/

#pragma once

#include <algorithm>
#include "propan/solver_types.h"
#include "propan/stream_buffer.h"

class WCNFFormula {
 public:
    For hard_formula;
    For soft_formula;
    std::vector<uint64_t> soft_weights;
    unsigned variables;

    WCNFFormula() : hard_formula(), soft_formula(), variables(0) { }

    WCNFFormula(const char* filename) : WCNFFormula() {
        readDimacsFromFile(filename);
    }

    WCNFFormula(const WCNFFormula&) = delete;
    WCNFFormula& operator=(const WCNFFormula&) = delete;

    WCNFFormula(WCNFFormula&& other) noexcept
        : hard_formula(std::move(other.hard_formula)), soft_formula(std::move(other.soft_formula)),
          soft_weights(std::move(other.soft_weights)), variables(other.variables) {
        other.hard_formula.clear();
        other.soft_formula.clear();
        other.soft_weights.clear();
        other.variables = 0;
    }

    WCNFFormula& operator=(WCNFFormula&& other) noexcept {
        if (this != &other) {
            clear();
            hard_formula = std::move(other.hard_formula);
            soft_formula = std::move(other.soft_formula);
            soft_weights = std::move(other.soft_weights);
            variables = other.variables;
            other.hard_formula.clear();
            other.soft_formula.clear();
            other.soft_weights.clear();
            other.variables = 0;
        }
        return *this;
    }

    ~WCNFFormula() {
        clear();
    }

    typedef For::const_iterator const_iterator;

    inline const_iterator hard_begin() const {
        return hard_formula.begin();
    }

    inline const_iterator hard_end() const {
        return hard_formula.end();
    }

    inline const_iterator soft_begin() const {
        return soft_formula.begin();
    }

    inline const_iterator soft_end() const {
        return soft_formula.end();
    }

    inline const Cl* operator[] (int i) const {
        if (i < hard_formula.size()) return hard_formula[i];
        i -= hard_formula.size();
        return soft_formula[i];
    }

    inline size_t nVars() const {
        return variables;
    }

    inline size_t nClauses() const {
        return hard_formula.size() + soft_formula.size();
    }

    inline size_t nHardClauses() const {
        return hard_formula.size();
    }

    inline size_t nSoftClauses() const {
        return soft_formula.size();
    }

    inline int newVar() {
        return ++variables;
    }

    inline void clear() {
        for (Cl* clause : hard_formula) delete clause;
        for (Cl* clause : soft_formula) delete clause;
        hard_formula.clear();
        soft_formula.clear();
        soft_weights.clear();
        variables = 0;
    }

    /**
     * Reads both the pre-2022 format (header "p wcnf <vars> <clauses> <top>", clauses with
     * weight >= top are hard) and the 2022 MaxSAT Evaluation format (no header, hard clauses
     * start with 'h').
     */
    void readDimacsFromFile(const char* filename) {
        StreamBuffer in(filename);
        Cl clause;
        bool has_top = false;
        uint64_t top = 0;
        while (in.skipWhitespace()) {
            if (*in == 'p' || *in == 'c') {
                if (*in == 'p') {
                    in.skipString("p wcnf");
                    int vars, dummy;
                    if (in.readInteger(&vars) && vars > 0) {
                        variables = std::max(variables, static_cast<unsigned>(vars));
                    }
                    in.readInteger(&dummy);  // skip number of clauses
                    has_top = in.readUInt64(&top);
                }
                if (!in.skipLine()) break;
            } else {
                bool hard = false;
                uint64_t weight = 1;
                if (*in == 'h') {
                    hard = true;
                    in.skip();
                } else {
                    in.readUInt64(&weight);
                    hard = has_top && weight >= top;
                }
                int plit;
                while (in.readInteger(&plit)) {
                    if (plit == 0) break;
                    clause.push_back(Lit(abs(plit), plit < 0));
                }
                if (hard) {
                    readClause(clause.begin(), clause.end(), hard_formula);
                } else if (readClause(clause.begin(), clause.end(), soft_formula)) {
                    soft_weights.push_back(weight);
                }
                clause.clear();
            }
        }
    }

    /// Normalizes and adds a clause; returns false if it was dropped as a tautology.
    template <typename Iterator>
    bool readClause(Iterator begin, Iterator end, For& formula) {
        Cl* clause = new Cl { begin, end };
        if (clause->size() > 0) {
            // remove redundant literals
            std::sort(clause->begin(), clause->end());
            unsigned dup = 0;
            for (auto it = clause->begin(), jt = clause->begin()+1; jt != clause->end(); ++jt) {
                if (*it != *jt) {  // unique
                    if (it->var() == jt->var()) {
                        delete clause;
                        return false;  // no tautologies
                    }
                    ++it;
                    *it = *jt;
                } else {
                    ++dup;
                }
            }
            clause->resize(clause->size() - dup);
            clause->shrink_to_fit();
            variables = std::max(variables, (unsigned int)clause->back().var());
        }
        formula.push_back(clause);
        return true;
    }
};
