/*************************************************************************************************
CNFFormula -- Copyright (c) 2021, Ashlin Iser, KIT - Karlsruhe Institute of Technology

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

class CNFFormula {
 public:
    For formula;
    unsigned variables;

    CNFFormula() : formula(), variables(0) { }

    CNFFormula(For f, unsigned vars) : formula(f), variables(vars) { }

    CNFFormula(const char* filename) : CNFFormula() {
        readDimacsFromFile(filename);
    }

    // Copy constructor
    CNFFormula(const CNFFormula& other) : formula(), variables(other.variables) {
        for (Cl* clause : other.formula) {
            formula.push_back(new Cl(*clause)); // Deep copy each clause
        }
    }

    // Copy assignment operator
    CNFFormula& operator=(const CNFFormula& other) {
        if (this != &other) { // Protect against self-assignment
            clear(); // Clean up existing resources
            variables = other.variables;
            for (Cl* clause : other.formula) {
                formula.push_back(new Cl(*clause)); // Deep copy each clause
            }
        }
        return *this;
    }

    // Move constructor
    CNFFormula(CNFFormula&& other) noexcept
        : formula(std::move(other.formula)), variables(other.variables) {
        other.variables = 0; // Leave `other` in a valid state
    }

    ~CNFFormula() {
        for (Cl* clause : formula) {
            delete clause;
        }
    }

    typedef For::const_iterator const_iterator;

    inline const_iterator begin() const {
        return formula.begin();
    }

    inline const_iterator end() const {
        return formula.end();
    }

    inline const Cl* operator[] (int i) const {
        return formula[i];
    }

    inline size_t nVars() const {
        return variables;
    }

    inline size_t nClauses() const {
        return formula.size();
    }

    inline int newVar() {
        return ++variables;
    }

    inline void clear() {
        formula.clear();
    }

    void readDimacsFromFile(const char* filename) {
        StreamBuffer in(filename);
        Cl clause;
        while (in.skipWhitespace()) {
            if (*in == 'p' || *in == 'c') {
                if (!in.skipLine()) break;
            } else {
                int plit;
                while (in.readInteger(&plit)) {
                    if (plit == 0) break;
                    clause.push_back(Lit(abs(plit), plit < 0));
                }
                readClause(clause.begin(), clause.end());
                clause.clear();
            }
        }
    }

    void readClause(std::initializer_list<Lit> list) {
        readClause(list.begin(), list.end());
    }

    template <typename Iterator>
    void readClause(Iterator begin, Iterator end) {
        Cl* clause = new Cl { begin, end };
        if (clause->size() > 0) {
            // remove redundant literals
            std::sort(clause->begin(), clause->end());
            unsigned dup = 0;
            for (auto it = clause->begin(), jt = clause->begin()+1; jt != clause->end(); ++jt) {
                if (*it != *jt) {  // unique
                    if (it->var() == jt->var()) {
                        delete clause;
                        return;  // no tautologies
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
    }
};

