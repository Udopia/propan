#include <cassert>
#include <iostream>
#include <memory>
#include <random>

#include "propan/cnf_formula.h"
#include "propan/wcnf_formula.h"
#include "propan/occurrence_propagator.h"
#include "propan/propagator.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"

#include <numeric>
#include <algorithm>
#include <cmath>

using namespace propan;

double calculate_mean(const std::vector<int>& data) {
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}

double calculate_variance(const std::vector<int>& data, double mean) {
    double variance = 0.0;
    for (int value : data) {
        variance += (value - mean) * (value - mean);
    }
    return variance / data.size();
}

double calculate_median(std::vector<int>& data) {
    std::sort(data.begin(), data.end());
    return data[data.size() / 2];
}

std::vector<Lit> get_permutation(size_t nVars, size_t seed) {
    std::vector<Lit> perm(nVars);
    for (size_t i = 0; i < nVars; i++) {
        perm[i] = Lit(static_cast<int>(i + 1));
    }

    std::mt19937 g(seed);
    std::shuffle(perm.begin(), perm.end(), g);
    return perm;
}

// SLUR(k) with immediate propagation: every decision gets its own level, a
// conflict backtracks the whole block and tries the next polarity vector.
bool slurpk(std::vector<Lit> lits, Trail& trail, Propagator& prop, unsigned k) {
    for (unsigned i = 0; i < lits.size(); i += k) {
        const unsigned base = trail.level();
        bool conflict = true;
        for (uint64_t bv = 0; bv < (1ULL << k) && conflict; ++bv) {
            trail.backtrackTo(base);
            conflict = false;
            unsigned c = 0, j = 0;
            while (c < k && i + j < lits.size()) {
                while (i + j < lits.size() && trail.value(lits[i + j].var()) != VAL_UNASSIGNED) {
                    ++j;
                }
                if (i + j >= lits.size()) break;
                trail.newLevel();
                trail.push((bv & (1ULL << c)) ? lits[i + j] : -lits[i + j]);
                c++;
                if (prop.propagate() != ClauseRef()) {
                    conflict = true;
                    break;
                }
            }
        }
        if (conflict) return false;
    }
    return true;
}

// SLUR(k): a block of up to k decisions forms one level and is propagated at once.
bool slurk(std::vector<Lit> lits, Trail& trail, Propagator& prop, unsigned k) {
    for (unsigned i = 0; i < lits.size(); i += k) {
        const unsigned base = trail.level();
        bool conflict = true;
        for (uint64_t bv = 0; bv < (1ULL << k) && conflict; ++bv) {
            trail.backtrackTo(base);
            trail.newLevel();
            unsigned c = 0, j = 0;
            while (c < k && i + j < lits.size()) {
                while (i + j < lits.size() && trail.value(lits[i + j].var()) != VAL_UNASSIGNED) {
                    ++j;
                }
                if (i + j >= lits.size()) break;
                trail.push((bv & (1ULL << c)) ? lits[i + j] : -lits[i + j]);
                c++;
            }
            conflict = prop.propagate() != ClauseRef();
        }
        if (conflict) return false;
    }
    return true;
}

// SLUR: one decision per level, each polarity tried once.
bool slursat(std::vector<Lit> lits, Trail& trail, Propagator& prop) {
    for (Lit lit : lits) {
        if (trail.value(lit.var()) == VAL_UNASSIGNED) {
            trail.newLevel();
            trail.push(lit);
            if (prop.propagate() != ClauseRef()) {
                trail.backtrack();
                trail.newLevel();
                trail.push(-lit);
                if (prop.propagate() != ClauseRef()) {
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cout << "USAGE: ./slur <path/to/dimacs/cnf/formula> [seed] [k] [watch|occ]" << std::endl;
		return 0;
	}

    CNFFormula formula;
    if (std::string(argv[1]).find(".wcnf") != std::string::npos) {
        WCNFFormula wformula(argv[1]);
        formula = std::move(wformula.hard());
    } else {
        formula = CNFFormula(argv[1]);
    }

    int seed = 0;
    if (argc > 2) {
        seed = std::stoi(argv[2]);
    }

    int k = 1;
    if (argc > 3) {
        k = std::stoi(argv[3]);
    }

	std::cout << "c Testing if formula " << argv[1] << " can be solved by SLUR algorithm." << std::endl;

    Trail trail(formula.nVars());
    std::unique_ptr<Propagator> propagator;
    if (argc > 4 && std::string(argv[4]) == "occ") {
        propagator = std::make_unique<OccurrencePropagator>(formula.clauses(), trail);
    } else {
        propagator = std::make_unique<WatchPropagator>(formula.clauses(), trail);
    }
    Propagator& prop = *propagator;
    if (!prop.attachAll(formula.clauses()) || prop.propagate() != ClauseRef()) {
        std::cout << "s UNSATISFIABLE" << std::endl;
        return 0;
    }

    std::vector<Lit> perm = get_permutation(formula.nVars(), seed);

    bool result = false;
    if (abs(k) <= 1) {
        result = slursat(perm, trail, prop);
    } else if (k > 1) {
        result = slurk(perm, trail, prop, k);
    } else {
        result = slurpk(perm, trail, prop, -k);
    }

    if (result) {
        std::cout << "s SATISFIABLE" << std::endl;
        std::cout << trail << std::endl;
        if (!satisfies(trail, formula.clauses())) {
            std::cout << "c ERROR: Trail does not satisfy the formula." << std::endl;
        }
    } else {
        std::cout << "s UNKNOWN" << std::endl;
        std::cout << "c GAVE UP" << std::endl;
    }

    std::vector<int> differences;
    for (unsigned i = 0; i < trail.level(); ++i) {
        differences.push_back(static_cast<int>(trail.literals(i).size()));
    }
    std::sort(differences.begin(), differences.end());
    if (differences.empty()) {
        return 0;
    }
    
    double mean = std::accumulate(differences.begin(), differences.end(), 0.0) / differences.size();
    double median = differences[differences.size() / 2];

    double variance = 0.0;
    for (int diff : differences) {
        variance += (diff - mean) * (diff - mean);
    }
    variance /= differences.size();

    std::cout << "c mean: " << mean << std::endl;
    std::cout << "c median: " << median << std::endl;
    std::cout << "c variance: " << variance << std::endl;

	return 0;
}