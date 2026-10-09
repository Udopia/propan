#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <vector>

#include "propan/trail.h"
#include "test_util.h"

using namespace propan;

namespace {

Lit L(int d) { return Lit(std::abs(d), d < 0); }

CNFFormula cnf(std::initializer_list<std::initializer_list<int>> clauses) {
    CNFFormula f;
    for (auto& c : clauses) {
        Cl cl;
        for (int d : c) cl.push_back(L(d));
        f.addClause(cl);
    }
    return f;
}

/// Literal is assigned true on the trail.
bool isTrue(Trail& t, int d) {
    Lit l = L(d);
    return t.value(l) == VAL_TRUE;
}

/// Trail invariants: every trail literal is true, no variable occurs twice,
/// every assigned variable is on the trail, level limits are monotone.
void checkInvariants(const Trail& t) {
    std::vector<int> seen(t.nVars() + 1, 0);
    for (size_t i = 0; i < t.size(); ++i) {
        Lit l = t[i];
        CHECK(t.value(l) == VAL_TRUE);
        CHECK(seen[l.var()]++ == 0);
    }
    for (unsigned v = 1; v <= t.nVars(); ++v) {
        CHECK((t.value(Var(v)) != VAL_UNASSIGNED) == (seen[v] == 1));
    }
    CHECK(t.levelStart(0) == 0);
    for (unsigned l = 1; l <= t.level(); ++l) {
        CHECK(t.levelStart(l - 1) <= t.levelStart(l));
    }
    CHECK(t.levelStart(t.level()) <= t.size());
}

}  // namespace

TEST_SUITE("trail") {

TEST_CASE("init without units leaves everything unassigned") {
    CNFFormula f = cnf({ { 1, 2 }, { -1, 3 } });
    Trail t;
    CHECK(t.init(f));
    CHECK(t.size() == 0);
    for (unsigned v = 1; v <= f.nVars(); ++v) CHECK(t.value(Var(v)) == VAL_UNASSIGNED);
    checkInvariants(t);
}

TEST_CASE("init propagates unit clauses to fixpoint") {
    CNFFormula f = cnf({ { 1 }, { -1, 2 }, { -2, 3 }, { -3, -4 } });
    Trail t;
    REQUIRE(t.init(f));
    CHECK(t.size() == 4);
    for (int d : { 1, 2, 3, -4 }) CHECK(isTrue(t, d));
    checkInvariants(t);
}

TEST_CASE("init detects contradicting units") {
    Trail t;
    CNFFormula f = cnf({ { 1 }, { -1 } });
    CHECK_FALSE(t.init(f));
}

TEST_CASE("init detects conflict by propagation of units") {
    Trail t;
    CNFFormula f = cnf({ { 1 }, { -1, 2 }, { -1, -2 } });
    CHECK_FALSE(t.init(f));
}

TEST_CASE("init detects the empty clause") {
    Trail t;
    CNFFormula f = cnf({ { 1, 2 }, {} });
    CHECK_FALSE(t.init(f));
}

TEST_CASE("re-init with another formula starts from a clean state") {
    Trail t;
    CNFFormula f = cnf({ { 1 }, { 2 }, { 3 } });
    REQUIRE(t.init(f));
    CNFFormula g = cnf({ { 1 }, { -1, 2 } });
    REQUIRE(t.init(g));
    CHECK(t.size() == 2);
    CHECK(isTrue(t, 2));
}

TEST_CASE("push assigns, accepts repeated and rejects contradicting literals") {
    CNFFormula f = cnf({ { 1, 2, 3 } });
    Trail t;
    REQUIRE(t.init(f));
    CHECK(t.push(L(1)));
    CHECK(isTrue(t, 1));
    CHECK(t.size() == 1);
    CHECK(t.push(L(1)));
    CHECK(t.size() == 1);
    CHECK_FALSE(t.push(L(-1)));
    CHECK(t.size() == 1);
    CHECK(isTrue(t, 1));
    checkInvariants(t);
}

TEST_CASE("value of a negative literal is the negated value of its variable") {
    CNFFormula f = cnf({ { 1, 2, 3 } });
    Trail t;
    REQUIRE(t.init(f));
    CHECK(t.value(L(1)) == VAL_UNASSIGNED);
    CHECK(t.value(L(-1)) == VAL_UNASSIGNED);
    REQUIRE(t.push(L(1)));
    REQUIRE(t.push(L(-2)));
    CHECK(t.value(Var(1)) == VAL_TRUE);
    CHECK(t.value(Var(2)) == VAL_FALSE);
    CHECK(t.value(L(1)) == VAL_TRUE);
    CHECK(t.value(L(-1)) == VAL_FALSE);
    CHECK(t.value(L(2)) == VAL_FALSE);
    CHECK(t.value(L(-2)) == VAL_TRUE);
    CHECK(t.value(L(3)) == VAL_UNASSIGNED);
}

TEST_CASE("propagate follows implication chains") {
    CNFFormula f = cnf({ { -1, 2 }, { -2, 3 }, { -3, 4 }, { -4, 5 } });
    Trail t;
    REQUIRE(t.init(f));
    t.newLevel();
    t.push(L(1));
    CHECK(t.propagate());
    CHECK(t.size() == 5);
    for (int d : { 1, 2, 3, 4, 5 }) CHECK(isTrue(t, d));
    CHECK(t[0] == L(1));
    checkInvariants(t);
}

TEST_CASE("propagate does not fire satisfied or non-unit clauses") {
    CNFFormula f = cnf({ { -1, 2, 3 }, { -1, 4, -5 } });
    Trail t;
    REQUIRE(t.init(f));
    t.push(L(1));
    CHECK(t.propagate());
    CHECK(t.size() == 1);
    t.push(L(5));
    CHECK(t.propagate());
    CHECK(isTrue(t, 4));
    t.push(L(-3));
    CHECK(t.propagate());
    CHECK(isTrue(t, 2));
    checkInvariants(t);
}

TEST_CASE("propagate ignores clauses satisfied before becoming unit") {
    CNFFormula f = cnf({ { -1, -2, 3 } });
    Trail t;
    REQUIRE(t.init(f));
    t.push(L(3));
    t.push(L(1));
    t.push(L(2));
    CHECK(t.propagate());
    CHECK(t.size() == 3);
}

TEST_CASE("propagate detects conflicts") {
    SUBCASE("two binary clauses") {
        CNFFormula f = cnf({ { -1, 2 }, { -1, -2 } });
        Trail t;
        REQUIRE(t.init(f));
        t.push(L(1));
        CHECK_FALSE(t.propagate());
    }
    SUBCASE("all literals of a clause falsified by decisions") {
        CNFFormula f = cnf({ { 1, 2, 3 } });
        Trail t;
        REQUIRE(t.init(f));
        t.push(L(-1));
        t.push(L(-2));
        t.push(L(-3));
        CHECK_FALSE(t.propagate());
    }
    SUBCASE("longer chain") {
        CNFFormula f = cnf({ { -1, 2 }, { -2, 3 }, { -3, -1 } });
        Trail t;
        REQUIRE(t.init(f));
        t.push(L(1));
        CHECK_FALSE(t.propagate());
    }
}

TEST_CASE("backtrack undoes exactly the last level") {
    CNFFormula f = cnf({ { 1 }, { -2, 3 }, { -4, 5 } });
    Trail t;
    REQUIRE(t.init(f));
    REQUIRE(t.size() == 1);

    t.newLevel();
    t.push(L(2));
    REQUIRE(t.propagate());
    t.newLevel();
    t.push(L(4));
    REQUIRE(t.propagate());
    CHECK(t.size() == 5);

    CHECK(t.backtrack());
    CHECK(t.size() == 3);
    CHECK(isTrue(t, 1));
    CHECK(isTrue(t, 2));
    CHECK(isTrue(t, 3));
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    CHECK(t.value(Var(5)) == VAL_UNASSIGNED);
    checkInvariants(t);

    CHECK(t.backtrack());
    CHECK(t.size() == 1);
    CHECK(t.value(Var(2)) == VAL_UNASSIGNED);
    checkInvariants(t);
}

TEST_CASE("backtrack after conflict allows propagating the other branch") {
    CNFFormula f = cnf({ { -1, 2 }, { -1, -2 }, { 1, 3 } });
    Trail t;
    REQUIRE(t.init(f));
    t.newLevel();
    t.push(L(1));
    REQUIRE_FALSE(t.propagate());
    REQUIRE(t.backtrack());
    CHECK(t.size() == 0);
    t.newLevel();
    t.push(L(-1));
    CHECK(t.propagate());
    CHECK(isTrue(t, 3));
    checkInvariants(t);
}

TEST_CASE("propagation after backtrack re-propagates nothing stale") {
    CNFFormula f = cnf({ { -1, 2 }, { -3, 4 } });
    Trail t;
    REQUIRE(t.init(f));
    t.newLevel();
    t.push(L(1));
    REQUIRE(t.propagate());
    t.newLevel();
    t.push(L(3));
    REQUIRE(t.propagate());
    REQUIRE(t.backtrack());
    t.newLevel();
    t.push(L(-4));
    CHECK(t.propagate());
    CHECK(isTrue(t, -3));
    CHECK(t.size() == 4);
    checkInvariants(t);
}

TEST_CASE("several decisions on one level are undone together") {
    CNFFormula f = cnf({ { -1, 2 }, { -3, 4 } });
    Trail t;
    REQUIRE(t.init(f));
    t.newLevel();
    t.push(L(1));
    t.push(L(3));
    REQUIRE(t.propagate());
    CHECK(t.size() == 4);
    REQUIRE(t.backtrack());
    CHECK(t.size() == 0);
    checkInvariants(t);
}

TEST_CASE("backtrack on level 0 keeps root-level units and returns false") {
    CNFFormula f = cnf({ { 1 }, { -1, 2 } });
    Trail t;
    REQUIRE(t.init(f));
    CHECK_FALSE(t.backtrack());
    CHECK(t.size() == 2);
    CHECK(isTrue(t, 2));
}

TEST_CASE("satisfies checks the formula against the assignment") {
    CNFFormula f = cnf({ { 1, 2 }, { -1, 3 } });
    Trail t;
    REQUIRE(t.init(f));
    CHECK_FALSE(t.satisfies(f));
    t.push(L(1));
    CHECK_FALSE(t.satisfies(f));
    REQUIRE(t.propagate());
    CHECK(t.satisfies(f));
}

TEST_CASE("solves the test formula by decisions and propagation") {
    CNFFormula f(test::data("sat_small.cnf").c_str());
    Trail t;
    REQUIRE(t.init(f));
    t.newLevel();
    t.push(L(1));
    REQUIRE(t.propagate());
    CHECK(t.size() == 4);
    CHECK(t.satisfies(f));
    checkInvariants(t);
}

}  // TEST_SUITE
