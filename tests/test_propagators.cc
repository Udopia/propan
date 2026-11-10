#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "propan/cnf_formula.h"
#include "propan/dimacs.h"
#include "propan/occurrence_propagator.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"
#include "test_util.h"

using namespace propan;
using test::lits;

using test::cnf;
using test::sorted;
TEST_SUITE("propagators") {

TEST_CASE_TEMPLATE("init without units leaves everything unassigned", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1, 2 }, { -1, 3 } }));
    CHECK(s.init());
    CHECK(s.t.size() == 0);
}

TEST_CASE_TEMPLATE("init propagates unit clauses to fixpoint with the units as reasons", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1 }, { -1, 2 }, { -2, 3 }, { -3, -4 } }));
    REQUIRE(s.init());
    CHECK(s.t.size() == 4);
    for (int d : { 1, 2, 3, -4 }) CHECK(s.isTrue(d));
    for (Lit l : s.t) {
        CHECK_FALSE(s.t.isDecision(l));
        CHECK(s.t.level(l.var()) == 0);
    }
    CHECK(sorted(s.f[s.t.reason(Var(1))]) == lits({ 1 }));
    CHECK(sorted(s.f[s.t.reason(Var(3))]) == lits({ -2, 3 }));
}

TEST_CASE_TEMPLATE("init detects contradicting units", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1 }, { -1 } }));
    CHECK_FALSE(s.init());
}

TEST_CASE_TEMPLATE("init detects conflict by propagation of units", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1 }, { -1, 2 }, { -1, -2 } }));
    CHECK_FALSE(s.init());
}

TEST_CASE_TEMPLATE("init detects the empty clause", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1, 2 }, {} }));
    CHECK_FALSE(s.init());
}

TEST_CASE_TEMPLATE("propagate follows implication chains and returns UNDEF", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2 }, { -2, 3 }, { -3, 4 }, { -4, 5 } }));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(Lit(1));
    CHECK(s.p.propagate() == ClauseRef());
    CHECK(s.t.size() == 5);
    for (int d : { 1, 2, 3, 4, 5 }) CHECK(s.isTrue(d));
    CHECK(s.t[0] == Lit(1));
    CHECK(s.t.isDecision(Lit(1)));
    for (int d : { 2, 3, 4, 5 }) {
        CHECK_FALSE(s.t.isDecision(Lit(d)));
        CHECK(s.t.level(Var(d)) == 1);
    }
    CHECK(s.t.head == s.t.size());
}

TEST_CASE_TEMPLATE("propagate does not fire satisfied or non-unit clauses", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2, 3 }, { -1, 4, -5 } }));
    REQUIRE(s.init());
    s.t.push(Lit(1));
    CHECK(s.ok());
    CHECK(s.t.size() == 1);
    s.t.push(Lit(5));
    CHECK(s.ok());
    CHECK(s.isTrue(4));
    s.t.push(Lit(-3));
    CHECK(s.ok());
    CHECK(s.isTrue(2));
}

TEST_CASE_TEMPLATE("propagate ignores clauses satisfied before becoming unit", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, -2, 3 } }));
    REQUIRE(s.init());
    s.t.push(lits({ 3, 1, 2 }));
    CHECK(s.ok());
    CHECK(s.t.size() == 3);
}

TEST_CASE_TEMPLATE("propagate returns the conflicting clause", P, PROPAGATORS) {
    SUBCASE("two binary clauses") {
        test::Fixture<P> s(cnf({ { -1, 2 }, { -1, -2 } }));
        REQUIRE(s.init());
        s.t.push(Lit(1));
        ClauseRef conflict = s.p.propagate();
        REQUIRE(conflict != ClauseRef());
        CHECK(sorted(s.f[conflict]) == lits({ -1, -2 }));
        for (Lit l : s.f[conflict]) CHECK(s.t.value(l) == VAL_FALSE);
    }
    SUBCASE("all literals of a clause falsified by decisions") {
        test::Fixture<P> s(cnf({ { 1, 2, 3 } }));
        REQUIRE(s.init());
        s.t.push(lits({ -1, -2, -3 }));
        CHECK(s.p.propagate() == s.f.clauses().ref(ClauseId(0)));
    }
    SUBCASE("longer chain") {
        test::Fixture<P> s(cnf({ { -1, 2 }, { -2, 3 }, { -3, -1 } }));
        REQUIRE(s.init());
        s.t.push(Lit(1));
        CHECK_FALSE(s.ok());
    }
}

TEST_CASE_TEMPLATE("backtrack after conflict allows propagating the other branch", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2 }, { -1, -2 }, { 1, 3 } }));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(Lit(1));
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.t.backtrack());
    CHECK(s.t.size() == 0);
    s.t.newLevel();
    s.t.push(Lit(-1));
    CHECK(s.ok());
    CHECK(s.isTrue(3));
}

TEST_CASE_TEMPLATE("propagation after backtrack re-propagates nothing stale", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2 }, { -3, 4 } }));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(Lit(1));
    REQUIRE(s.ok());
    s.t.newLevel();
    s.t.push(Lit(3));
    REQUIRE(s.ok());
    REQUIRE(s.t.backtrack());
    s.t.newLevel();
    s.t.push(Lit(-4));
    CHECK(s.ok());
    CHECK(s.isTrue(-3));
    CHECK(s.t.size() == 4);
}

TEST_CASE_TEMPLATE("several decisions on one level are propagated and undone together", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2 }, { -3, 4 }, { -2, -4, 5 } }));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(lits({ 1, 3 }));
    REQUIRE(s.ok());
    CHECK(s.t.size() == 5);
    CHECK(s.isTrue(5));
    CHECK(s.t.level(Var(5)) == 1);
    REQUIRE(s.t.backtrack());
    CHECK(s.t.size() == 0);
}

TEST_CASE_TEMPLATE("attach assigns a clause that is unit under the current assignment", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1, 2 }, { 3, 4 } }));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(Lit(-1));
    REQUIRE(s.ok());
    s.f.addClause(lits({ 1, 3 }));
    const ClauseRef learned = s.f.clauses().ref(ClauseId(2));
    REQUIRE(s.p.attach(learned));
    CHECK(s.isTrue(3));
    CHECK(s.t.reason(Var(3)) == learned);
    CHECK(s.t.level(Var(3)) == 1);
    CHECK(s.ok());
}

TEST_CASE_TEMPLATE("attach reports a clause that is already falsified", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { 1, 2 }, { 3, 4 } }));
    REQUIRE(s.init());
    s.t.push(lits({ -1, -3 }));
    s.f.addClause(lits({ 1, 3 }));
    CHECK_FALSE(s.p.attach(s.f.clauses().ref(ClauseId(2))));
}

TEST_CASE_TEMPLATE("detached clauses no longer propagate", P, PROPAGATORS) {
    test::Fixture<P> s(cnf({ { -1, 2 }, { -1, 3 } }));
    REQUIRE(s.init());
    s.p.detach(s.f.clauses().ref(ClauseId(0)));
    s.t.newLevel();
    s.t.push(Lit(1));
    REQUIRE(s.ok());
    CHECK(s.t.value(Var(2)) == VAL_UNASSIGNED);
    CHECK(s.isTrue(3));
}

TEST_CASE_TEMPLATE("relocate keeps propagating after compaction", P, PROPAGATORS) {
    ClauseArena a;
    a.add(lits({ 9 }));
    a.add(lits({ -1, 2 }));
    a.add(lits({ -2, 3 }));
    a.erase(a.ref(ClauseId(0)));  // leave a hole for compaction
    Trail t(3);
    P p(a, t);
    REQUIRE(test::attachAll(p, a));
    REQUIRE(p.propagate() == ClauseRef());
    t.newLevel();
    t.push(Lit(1));
    REQUIRE(p.propagate() == ClauseRef());
    REQUIRE(t.size() == 3);
    const auto relocation = a.compact();
    REQUIRE_FALSE(relocation.empty());
    p.relocate(relocation);
    t.relocate(relocation);
    CHECK(sorted(a[t.reason(Var(3))]) == lits({ -2, 3 }));
    t.backtrack();
    t.newLevel();
    t.push(Lit(1));
    REQUIRE(p.propagate() == ClauseRef());
    CHECK(t.size() == 3);
    CHECK(t.value(Lit(3)) == VAL_TRUE);
}

TEST_CASE_TEMPLATE("solves the test formula by decisions and propagation", P, PROPAGATORS) {
    test::Fixture<P> s(readCnf(test::data("sat_small.cnf").c_str()));
    REQUIRE(s.init());
    s.t.newLevel();
    s.t.push(Lit(1));
    REQUIRE(s.ok());
    CHECK(s.t.size() == 4);
    CHECK(satisfies(s.t, s.f.clauses()));
}

TEST_CASE("occurrence and watch propagation agree on random formulas") {
    std::mt19937 rng(12345);
    for (int round = 0; round < 200; ++round) {
        const unsigned n = 4 + rng() % 12;
        const unsigned m = 2 + rng() % (3 * n);
        CNFFormula f;
        for (unsigned i = 0; i < m; ++i) {
            Cl c;
            const unsigned len = 1 + rng() % 4;
            for (unsigned k = 0; k < len; ++k) c.push_back(Lit(int(1 + rng() % n) * (rng() % 2 ? 1 : -1)));
            f.addClause(c);
        }
        CNFFormula g = f;  // the watch propagator reorders literals; keep a pristine copy
        const unsigned nv = f.nVars();
        if (nv == 0) continue;
        Trail to(nv), tw(nv);
        OccurrencePropagator po(f.clauses(), to);
        WatchPropagator pw(g.clauses(), tw);
        const bool ok_o = test::attachAll(po, f.clauses()) && po.propagate() == ClauseRef();
        const bool ok_w = test::attachAll(pw, g.clauses()) && pw.propagate() == ClauseRef();
        REQUIRE(ok_o == ok_w);
        if (!ok_o) continue;
        for (int step = 0; step < 30; ++step) {
            const unsigned v = 1 + rng() % nv;
            if (to.value(Var(v)) != VAL_UNASSIGNED) {
                if (to.level() > 0 && rng() % 3 == 0) {
                    to.backtrack();
                    tw.backtrack();
                }
                continue;
            }
            const Lit d(int(v) * (rng() % 2 ? 1 : -1));
            for (Trail* t : { &to, &tw }) {
                t->newLevel();
                t->push(d);
            }
            const ClauseRef co = po.propagate();
            const ClauseRef cw = pw.propagate();
            REQUIRE((co == ClauseRef()) == (cw == ClauseRef()));
            if (co != ClauseRef()) {
                for (Lit l : g.clauses()[cw]) CHECK(tw.value(l) == VAL_FALSE);
                to.backtrack();
                tw.backtrack();
                continue;
            }
            REQUIRE(to.size() == tw.size());
            for (unsigned u = 1; u <= nv; ++u) CHECK(to.value(Var(u)) == tw.value(Var(u)));
            CHECK(satisfies(to, f.clauses()) == satisfies(tw, g.clauses()));
        }
    }
}

TEST_CASE("watch propagator maintains the watch invariants") {
    std::mt19937 rng(777);
    for (int round = 0; round < 50; ++round) {
        const unsigned n = 5 + rng() % 10;
        CNFFormula f;
        for (unsigned i = 0; i < 3 * n; ++i) {
            Cl c;
            const unsigned len = 2 + rng() % 5;
            for (unsigned k = 0; k < len; ++k) c.push_back(Lit(int(1 + rng() % n) * (rng() % 2 ? 1 : -1)));
            f.addClause(c);
        }
        const unsigned nv = f.nVars();
        if (nv == 0) continue;
        Trail t(nv);
        WatchPropagator p(f.clauses(), t);
        REQUIRE(test::attachAll(p, f.clauses()));
        auto check = [&] {
            // Every clause is satisfied, or both watched literals (the first two) are non-false.
            for (auto c : f) {
                const bool sat = std::any_of(c.begin(), c.end(), [&](Lit l) { return t.value(l) == VAL_TRUE; });
                if (!sat) {
                    CHECK(t.value(c[0]) != VAL_FALSE);
                    CHECK(t.value(c[1]) != VAL_FALSE);
                }
                CHECK(c.pos() >= 2);
                if (c.size() >= 2) CHECK(c.pos() <= c.size());
            }
        };
        if (p.propagate() != ClauseRef()) continue;
        check();
        for (int step = 0; step < 20; ++step) {
            const unsigned v = 1 + rng() % nv;
            if (t.value(Var(v)) != VAL_UNASSIGNED) continue;
            t.newLevel();
            t.push(Lit(int(v) * (rng() % 2 ? 1 : -1)));
            if (p.propagate() != ClauseRef()) {
                t.backtrack();
            } else {
                check();
            }
        }
    }
}

TEST_CASE("watch propagator visits long clauses only through their watches") {
    // A long clause with a true blocking literal is never touched: its pos stays 2.
    CNFFormula f = cnf({ { 1, 2, 3, 4, 5 } });
    Trail t(f.nVars());
    WatchPropagator p(f.clauses(), t);
    REQUIRE(test::attachAll(p, f.clauses()));
    t.newLevel();
    t.push(lits({ -3, -4, -5 }));  // not watched: nothing happens
    REQUIRE(p.propagate() == ClauseRef());
    CHECK(t.size() == 3);
    t.newLevel();
    t.push(Lit(-1));  // watched literal falsified, no replacement: unit
    REQUIRE(p.propagate() == ClauseRef());
    CHECK(t.value(Lit(2)) == VAL_TRUE);
    CHECK_FALSE(t.isDecision(Lit(2)));
    t.backtrackTo(0);
    t.newLevel();
    t.push(Lit(-1));  // replacement search moves the watch to another literal
    REQUIRE(p.propagate() == ClauseRef());
    CHECK(t.size() == 1);
    auto c = f[ClauseId(0)];
    CHECK(t.value(c[0]) != VAL_FALSE);
    CHECK(t.value(c[1]) != VAL_FALSE);
}

}  // TEST_SUITE
