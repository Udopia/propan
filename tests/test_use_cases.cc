// Scenario tests modelled on the intended applications of the trail and the
// propagators: SLUR(k) style block decisions, failed literal probing with
// lifting, MaxSAT soft literal probing, local search repair by propagation,
// and a minimal CDCL loop with clause learning. Every scenario runs with
// every propagator.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <utility>
#include <vector>

#include "propan/binary_first_propagator.h"
#include "propan/cnf_formula.h"
#include "propan/occurrence_propagator.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"
#include "test_util.h"

using namespace propan;
using test::lits;

namespace {

CNFFormula cnf(std::initializer_list<std::initializer_list<int>> clauses) {
    CNFFormula f;
    for (auto& c : clauses) {
        Cl cl;
        for (int d : c) cl.push_back(Lit(d));
        f.addClause(cl);
    }
    return f;
}

template<class P>
struct Solver {
    CNFFormula f;
    Trail t;
    P p;

    explicit Solver(CNFFormula formula) : f(std::move(formula)), t(f.nVars()), p(f.clauses(), t) { }

    bool init() { return p.attachAll(f.clauses()) && ok(); }
    bool ok() { return p.propagate() == ClauseRef(); }
    bool isTrue(int d) const { return t.value(Lit(d)) == VAL_TRUE; }
    bool complete() const {
        for (unsigned v = 1; v <= t.nVars(); ++v) {
            if (t.value(Var(v)) == VAL_UNASSIGNED) return false;
        }
        return true;
    }
};

#define PROPAGATORS OccurrencePropagator, WatchPropagator, BinaryFirstPropagator

}  // namespace

TEST_SUITE("use_cases") {

TEST_CASE_TEMPLATE("SLUR(k): blocks of decisions, all polarity vectors of a block on one level", P, PROPAGATORS) {
    // Horn-like chain: x1 -> x2 -> x3 -> x4, plus a clause forcing the last block.
    Solver<P> s(cnf({ { -1, 2 }, { -2, 3 }, { -3, 4 }, { -4, -5, 6 }, { 5, 6 }, { -6, 7 } }));
    REQUIRE(s.init());
    const std::vector<Lit> order = lits({ 1, 5, 3, 7, 2, 4, 6 });
    const unsigned k = 2;
    for (std::size_t i = 0; i < order.size(); i += k) {
        const unsigned base = s.t.level();
        bool conflict = true;
        for (uint64_t bv = 0; bv < (1u << k) && conflict; ++bv) {
            s.t.backtrackTo(base);
            s.t.newLevel();
            Cl block;
            for (std::size_t c = 0; c < k && i + c < order.size(); ++c) {
                const Lit l = order[i + c];
                if (s.t.value(l) == VAL_UNASSIGNED) block.push_back((bv >> c) & 1 ? l : -l);
            }
            if (!s.t.push(block)) continue;  // block contradicts itself under the assignment
            conflict = !s.ok();
            if (!conflict) {
                // Every block literal is a decision, everything after them is implied.
                auto seg = s.t.literals(s.t.level());
                for (std::size_t j = 0; j < seg.size(); ++j) CHECK(s.t.isDecision(seg[j]) == (j < block.size()));
            }
        }
        REQUIRE_FALSE(conflict);
    }
    CHECK(s.complete());
    CHECK(satisfies(s.t, s.f.clauses()));
}

TEST_CASE_TEMPLATE("failed literal probing with lifting of common implications", P, PROPAGATORS) {
    // x1 fails: x1 -> x2, x1 -> -x2. Both polarities of x3 imply x4 (lifted).
    Solver<P> s(cnf({ { -1, 2 }, { -1, -2 }, { -3, 4 }, { 3, 4 }, { -4, 5, 6 } }));
    REQUIRE(s.init());
    std::vector<int> failed, lifted;
    std::vector<unsigned char> mark(2 * s.t.nVars() + 2, 0);
    for (unsigned v = 1; v <= s.t.nVars(); ++v) {
        const Lit pos(static_cast<int>(v));
        if (s.t.value(pos) != VAL_UNASSIGNED) continue;
        s.t.newLevel();
        s.t.push(pos);
        if (!s.ok()) {
            s.t.backtrack();
            failed.push_back(static_cast<int>(v));
            REQUIRE(s.t.push(-pos));
            REQUIRE(s.ok());
            continue;
        }
        auto implied = s.t.literals(s.t.level()).subspan(1);
        Cl first(implied.begin(), implied.end());
        s.t.backtrack();
        for (Lit l : first) mark[l.index()] = 1;
        s.t.newLevel();
        s.t.push(-pos);
        if (!s.ok()) {
            s.t.backtrack();
            failed.push_back(-static_cast<int>(v));
            REQUIRE(s.t.push(pos));
            REQUIRE(s.ok());
        } else {
            Cl common;
            for (Lit l : s.t.literals(s.t.level()).subspan(1)) {
                if (mark[l.index()]) common.push_back(l);
            }
            s.t.backtrack();
            for (Lit l : common) {
                lifted.push_back(l.toDimacs());
                REQUIRE(s.t.push(l));
                REQUIRE(s.ok());
            }
        }
        for (Lit l : first) mark[l.index()] = 0;
    }
    CHECK(failed == std::vector<int> { 1 });
    CHECK(lifted == std::vector<int> { 4 });
    CHECK(s.t.level() == 0);
    CHECK(s.isTrue(-1));
    CHECK(s.isTrue(4));
    CHECK(s.t.value(Var(3)) == VAL_UNASSIGNED);
    CHECK(s.t.isDecision(Lit(-1)));  // root facts pushed by the client have no reason
}

TEST_CASE_TEMPLATE("MaxSAT probing: soft literals individually and as one block", P, PROPAGATORS) {
    // Hard: at most one of s1, s2 (binary), s3 -> s4 is forbidden with s1, s5 implies the rest.
    Solver<P> s(cnf({ { -1, -2 }, { -3, 4 }, { -1, -4 }, { -5, 1 }, { -5, 3 } }));
    REQUIRE(s.init());
    const std::vector<Lit> softs = lits({ 1, 2, 3, 5 });
    // Individual probes: which soft literals fail, what each one implies.
    std::vector<int> failed;
    std::vector<std::size_t> implied;
    for (Lit l : softs) {
        s.t.newLevel();
        REQUIRE(s.t.push(l));
        if (!s.ok()) failed.push_back(l.toDimacs());
        else implied.push_back(s.t.literals(s.t.level()).size() - 1);
        s.t.backtrack();
    }
    CHECK(failed == std::vector<int> { 5 });  // s5 -> s1 and s3 -> s4, but s1 and s4 exclude each other
    CHECK(implied == std::vector<std::size_t> { 4, 2, 3 });  // s1: -2, -4, -3, -5; s2: -1, -5; s3: 4, -1, -5
    // The block of all softs is inconsistent; reading what follows the decisions of a consistent block.
    s.t.newLevel();
    const bool consistent = s.t.push(softs) && s.ok();
    CHECK_FALSE(consistent);
    s.t.backtrack();
    s.t.newLevel();
    const std::vector<Lit> block = lits({ 2, 3 });
    REQUIRE(s.t.push(block));
    REQUIRE(s.ok());
    auto followed = s.t.literals(s.t.level()).subspan(block.size());
    Cl impl(followed.begin(), followed.end());
    std::sort(impl.begin(), impl.end());
    CHECK(impl == lits({ -1, 4, -5 }));
    for (Lit l : followed) CHECK_FALSE(s.t.isDecision(l));
    s.t.backtrack();
    CHECK(s.t.size() == 0);
}

TEST_CASE_TEMPLATE("local search repair: push a full assignment, flip on conflict", P, PROPAGATORS) {
    // Random 3-SAT-ish formula with a planted solution (all variables true).
    std::mt19937 rng(42);
    const unsigned n = 30;
    CNFFormula f;
    for (unsigned i = 0; i < 100; ++i) {
        Cl c;
        for (unsigned k = 0; k < 3; ++k) c.push_back(Lit(int(1 + rng() % n) * (rng() % 4 ? -1 : 1)));
        c[rng() % 3] = c[rng() % 3].positive();  // at least one positive literal: all-true satisfies it
        f.addClause(c);
    }
    Solver<P> s(std::move(f));
    REQUIRE(s.init());
    std::vector<bool> phase(s.t.nVars() + 1);
    for (unsigned v = 1; v <= s.t.nVars(); ++v) phase[v] = rng() % 2;
    bool solved = false;
    for (int round = 0; round < 500 && !solved; ++round) {
        Cl blob;
        for (unsigned v = 1; v <= s.t.nVars(); ++v) {
            if (s.t.value(Var(v)) == VAL_UNASSIGNED) blob.push_back(Lit(phase[v] ? int(v) : -int(v)));
        }
        s.t.newLevel();
        REQUIRE(s.t.push(blob));  // blob literals are over distinct unassigned variables
        const ClauseRef conflict = s.p.propagate();
        if (conflict == ClauseRef()) {
            solved = true;
            break;
        }
        // Flip the phase of every decision variable in the conflicting clause.
        for (Lit l : s.f[conflict]) {
            if (s.t.isDecision(-l)) phase[l.var()] = !phase[l.var()];
        }
        s.t.backtrack();
    }
    REQUIRE(solved);
    CHECK(s.complete());
    CHECK(satisfies(s.t, s.f.clauses()));
}

namespace {

/// Minimal CDCL: one decision per level, decision-cut learning, backjump by one level.
template<class P>
bool cdcl(Solver<P>& s, unsigned max_conflicts = 10000) {
    unsigned conflicts = 0;
    for (;;) {
        const ClauseRef conflict = s.p.propagate();
        if (conflict != ClauseRef()) {
            if (s.t.level() == 0) return false;
            if (++conflicts > max_conflicts) throw std::runtime_error("conflict limit");
            Cl learned;
            for (unsigned l = 1; l <= s.t.level(); ++l) learned.push_back(-s.t.literals(l)[0]);
            s.t.backtrackTo(s.t.level() - 1);
            REQUIRE(s.f.addClause(learned));
            const ClauseRef ref = s.f.clauses().ref(ClauseId(static_cast<uint32_t>(s.f.nClauses() - 1)));
            REQUIRE(s.p.attach(ref));  // asserting: all but the undone decision are false
            continue;
        }
        unsigned v = 1;
        while (v <= s.t.nVars() && s.t.value(Var(v)) != VAL_UNASSIGNED) ++v;
        if (v > s.t.nVars()) return true;
        s.t.newLevel();
        s.t.push(Lit(static_cast<int>(v)));
    }
}

}  // namespace

TEST_CASE_TEMPLATE("minimal CDCL with learned clauses attached at the backjump level", P, PROPAGATORS) {
    SUBCASE("satisfiable") {
        Solver<P> s(CNFFormula(test::data("sat_small.cnf").c_str()));
        REQUIRE(s.init());
        CHECK(cdcl(s));
        CHECK(satisfies(s.t, s.f.clauses()));
    }
    SUBCASE("pigeon hole 3 into 2 is refuted") {
        Solver<P> s(CNFFormula(test::data("php_3_2.cnf").c_str()));
        REQUIRE(s.init());
        CHECK_FALSE(cdcl(s));
    }
    SUBCASE("random formulas: model or refutation agree with the occurrence propagator") {
        std::mt19937 rng(7);
        for (int round = 0; round < 30; ++round) {
            const unsigned n = 6 + rng() % 6;
            CNFFormula f;
            for (unsigned i = 0; i < 4 * n; ++i) {
                Cl c;
                for (unsigned k = 0; k < 3; ++k) c.push_back(Lit(int(1 + rng() % n) * (rng() % 2 ? 1 : -1)));
                f.addClause(c);
            }
            if (f.nVars() == 0) continue;
            Solver<OccurrencePropagator> ref(f);
            Solver<P> s(f);
            const bool ok_ref = ref.init() && cdcl(ref);
            const bool ok = s.init() && cdcl(s);
            CHECK(ok == ok_ref);
            if (ok) CHECK(satisfies(s.t, s.f.clauses()));
        }
    }
}

}  // TEST_SUITE
