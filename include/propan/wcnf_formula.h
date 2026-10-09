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

class WCNFFormula {
 public:
    For hard_formula;
    For soft_formula;
    std::vector<int> soft_weights;
    unsigned variables;

    WCNFFormula() : hard_formula(), soft_formula(), variables(0) { }

    WCNFFormula(const char* filename) : WCNFFormula() {
        readDimacsFromFile(filename);
    }

    ~WCNFFormula() {
        // for (Cl* clause : hard_formula) {
        //     delete clause;
        // }
        for (Cl* clause : soft_formula) {
            delete clause;
        }
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
        hard_formula.clear();
        soft_formula.clear();
    }

    void readDimacsFromFile(const char* filename) {
        StreamBuffer in(filename);
        Cl clause;
        uint64_t maxweight = 0;
        while (in.skipWhitespace()) {
            if (*in == 'p' || *in == 'c') {
                if (*in == 'p') {
                    in.skipString("p wcnf");                    
                    int dummy;
                    in.readInteger(&dummy);  // skip number of variables
                    in.readInteger(&dummy);  // skip number of clauses
                    in.readUInt64(&maxweight);  // keep maxweight
                }
                if (!in.skipLine()) break;
            } else {
                uint64_t weight = 1;
                in.readUInt64(&weight);
                int plit;
                while (in.readInteger(&plit)) {
                    if (plit == 0) break;
                    clause.push_back(Lit(abs(plit), plit < 0));
                }
                if (weight >= maxweight) {
                    readClause(clause.begin(), clause.end(), hard_formula);
                } else {
                    readClause(clause.begin(), clause.end(), soft_formula);
                    soft_weights.push_back(weight);
                }
                clause.clear();
            }
        }
    }

    template <typename Iterator>
    void readClause(Iterator begin, Iterator end, For& formula) {
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
