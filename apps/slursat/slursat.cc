#include <cassert>
#include <random>

#include "propan/cnf_formula.h"
#include "propan/wcnf_formula.h"
#include "propan/trail.h"

#include <numeric>
#include <algorithm>
#include <cmath>

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
    for (int i = 0; i < nVars; i++) {
        perm[i] = Lit(i+1, false);
    }

    std::mt19937 g(seed);
    std::shuffle(perm.begin(), perm.end(), g);
    return perm;
}

// SLUR(k) algorithm, with immediate propagation
bool slurpk(std::vector<Lit> lits, Trail& trail, unsigned k) {
    bool conflict = false;
    for (unsigned i = 0; i < lits.size(); i += k) {
        for (uint64_t bv = 0; bv < (1ULL << k); ++bv) {
            if (conflict) {
                trail.backtrack();
                conflict = false;
            }
            trail.newLevel();
            unsigned c = 0, j = 0;
            while (c < k && i + j < lits.size()) {
                while (i + j < lits.size() && trail.value(lits[i + j].var()) != trail.UNASSIGNED) {
                    ++j;
                }
                if (i + j >= lits.size()) break;
                Lit lit = (bv & (1ULL << c)) ? lits[i + j] : ~lits[i + j];
                trail.push(lit);
                c++;
                conflict = !trail.propagate();
                if (conflict) break;
            }
            if (!conflict) break;
        }
        if (conflict) return false;
    }
    return true;
}

// SLUR(k) algorithm
bool slurk(std::vector<Lit> lits, Trail& trail, unsigned k) {
    bool conflict = false;
    for (unsigned i = 0; i < lits.size(); i += k) {
        for (uint64_t bv = 0; bv < (1ULL << k); ++bv) {
            if (conflict) {
                trail.backtrack();
                conflict = false;
            }
            trail.newLevel();
            unsigned c = 0, j = 0;
            while (c < k && i + j < lits.size()) {
                while (i + j < lits.size() && trail.value(lits[i + j].var()) != trail.UNASSIGNED) {
                    ++j;
                }
                if (i + j >= lits.size()) break;
                Lit lit = (bv & (1ULL << c)) ? lits[i + j] : ~lits[i + j];
                trail.push(lit);
                c++;
            }
            conflict = !trail.propagate();
            if (!conflict) break;
        }
        if (conflict) return false;
    }
    return true;
}

// SLUR algorithm
bool slursat(std::vector<Lit> lits, Trail& trail) {
    for (Lit lit : lits) {
        if (trail.value(lit.var()) == trail.UNASSIGNED) {
            trail.newLevel();
            trail.push(lit);
            if (!trail.propagate()) {
                trail.backtrack();
                trail.newLevel();
                trail.push(~lit);
                if (!trail.propagate()) {
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cout << "USAGE: ./slur <path/to/dimacs/cnf/formula> [seed] [k]" << std::endl;
		return 0;
	}

    CNFFormula formula;
    if (std::string(argv[1]).find(".wcnf") != std::string::npos) {
        WCNFFormula wformula = WCNFFormula(argv[1]);
        formula = CNFFormula(wformula.hard_formula, wformula.nVars());
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

    Trail trail {};
    if (!trail.init(formula)) {
        std::cout << "s UNSATISFIABLE" << std::endl;
        return 0;
    }

    std::vector<Lit> perm = get_permutation(formula.nVars(), seed);

    bool result = false;
    if (abs(k) <= 1) {
        result = slursat(perm, trail);
    } else if (k > 1) {
        result = slurk(perm, trail, k);
    } else {
        result = slurpk(perm, trail, -k);
    }

    if (result) {
        std::cout << "s SATISFIABLE" << std::endl;
        trail.print();
        if (!trail.satisfies(formula)) {
            std::cout << "c ERROR: Trail does not satisfy the formula." << std::endl;
        }
    } else {
        std::cout << "s UNKNOWN" << std::endl;
        std::cout << "c GAVE UP" << std::endl;
    }

    double fraction = (double)formula.nVars() / trail.trail.size();

    std::vector<int> differences;
    for (size_t i = 1; i < trail.trail_lim.size(); ++i) {
        differences.push_back(trail.trail_lim[i] - trail.trail_lim[i - 1]);
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