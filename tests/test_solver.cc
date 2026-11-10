#include <doctest/doctest.h>

#include <stdexcept>
#include <string_view>

#include "propan/solver.h"
#include "test_util.h"

using namespace propan;
using test::cnf;
using test::lits;

TEST_SUITE("solver") {

TEST_CASE("constructs with every propagator by name and rejects unknown names") {
    for (std::string_view name : PROPAGATOR_NAMES) {
        Solver s(cnf({ { 1, 2 }, { -1, 3 } }), { .propagator = name });
        CHECK(s.consistent());
        CHECK(s.nVars() == 3);
        CHECK(s.trail().size() == 0);
    }
    CHECK_THROWS_AS(Solver(cnf({ { 1 } }), Options { .propagator = "nonsense" }), std::invalid_argument);
}

TEST_CASE("construction attaches and propagates the root level; refutation is reported via consistent()") {
    Solver s(cnf({ { -1, 2 }, { -2, -1 }, { 1 } }));
    CHECK_FALSE(s.consistent());  // unit 1 propagates to a conflict
    CHECK(s.value(Lit(1)) == VAL_TRUE);

    Solver e(cnf({ { 1 }, { -1 } }));
    CHECK_FALSE(e.consistent());  // the second unit is falsified when attached

    Solver a(cnf({ { 1 }, { -1, 2 }, { -2, -1 } }));
    CHECK_FALSE(a.consistent());  // attach order: 1, then 2 implied, then {-2, -1} falsified
}

TEST_CASE("decide, propagate, backtrack, satisfied and complete") {
    Solver s(cnf({ { -1, 2 }, { -2, 3 }, { 3, 4 } }));
    REQUIRE(s.consistent());
    REQUIRE(s.propagate() == ClauseRef());
    CHECK(s.decide(Lit(1)));
    CHECK(s.propagate() == ClauseRef());
    CHECK(s.value(Lit(3)) == VAL_TRUE);
    CHECK_FALSE(s.complete());
    CHECK(s.satisfied());
    CHECK(s.decide(lits({ -4 })));
    CHECK(s.propagate() == ClauseRef());
    CHECK(s.complete());
    CHECK(s.satisfied());
    CHECK_FALSE(s.decide(Lit(-1)));  // already false
    s.trail().backtrackTo(0);
    CHECK(s.trail().size() == 0);
}

TEST_CASE("newVar extends formula, trail and propagator consistently") {
    for (std::string_view name : PROPAGATOR_NAMES) {
        Solver s(cnf({ { 1, 2 } }), { .propagator = name });
        REQUIRE(s.decide(Lit(-1)));
        REQUIRE(s.propagate() == ClauseRef());
        const Var v = s.newVar();
        CHECK(v == Var(3));
        CHECK(s.nVars() == 3);
        CHECK(s.formula().nVars() == 3);
        CHECK(s.value(v) == VAL_UNASSIGNED);
        CHECK(s.value(Lit(2)) == VAL_TRUE);  // existing assignment kept
        CHECK(s.add(lits({ -2, 3 })));       // clause over the new variable propagates
        CHECK(s.value(v) == VAL_TRUE);
        CHECK(s.propagate() == ClauseRef());
        s.trail().backtrack();
        REQUIRE(s.decide(Lit(-3)));
        CHECK(s.propagate() == ClauseRef());
        CHECK(s.value(Lit(2)) == VAL_FALSE);  // watches on the new variable work
        CHECK(s.value(Lit(1)) == VAL_TRUE);
    }
}

TEST_CASE("add attaches a clause, unit under the assignment, with the clause as reason") {
    Solver s(cnf({ { 1, 2 }, { 3, 4 } }));
    REQUIRE(s.decide(Lit(-1)));
    REQUIRE(s.propagate() == ClauseRef());
    CHECK(s.value(Lit(2)) == VAL_TRUE);
    CHECK(s.add(lits({ 1, 3 })));
    CHECK(s.value(Lit(3)) == VAL_TRUE);
    CHECK_FALSE(s.trail().isDecision(Lit(3)));
    CHECK(test::sorted(s.clauses()[s.trail().reason(Var(3))]) == lits({ 1, 3 }));
    CHECK(s.propagate() == ClauseRef());
    CHECK_FALSE(s.add(lits({ 1, -1 })));  // tautology: nothing added
    CHECK(s.formula().nClauses() == 3);
}

TEST_CASE("learn marks the clause learned; a falsified clause at the root refutes") {
    Solver s(cnf({ { 1, 2 } }));
    REQUIRE(s.trail().push(Lit(1)));
    REQUIRE(s.propagate() == ClauseRef());
    REQUIRE(s.learn(lits({ -1, 2 })));  // asserting: -1 is false, 2 is unassigned
    CHECK(s.clauses()[ClauseId(1)].flags().learned());
    CHECK_FALSE(s.clauses()[ClauseId(0)].flags().learned());

    Solver r(cnf({ { 1, 2, 5 }, { 3, 4 } }));
    REQUIRE(r.trail().push(Lit(-1)));
    REQUIRE(r.propagate() == ClauseRef());
    REQUIRE(r.trail().push(Lit(-2)));
    REQUIRE(r.propagate() == ClauseRef());
    CHECK(r.add(lits({ 1, 2, 3 })));  // unit on 3 under -1, -2
    CHECK(r.value(Lit(3)) == VAL_TRUE);
    CHECK(r.consistent());
    CHECK_FALSE(r.add(lits({ 1, 2 })));  // all false at the root
    CHECK_FALSE(r.consistent());
}

TEST_CASE("erase and compact keep trail and propagator consistent") {
    Solver s(cnf({ { 5, 6 }, { -1, 2 }, { -2, 3 } }));
    REQUIRE(s.consistent());
    REQUIRE(s.propagate() == ClauseRef());
    s.erase(s.clauses().ref(ClauseId(0)));  // { 5, 6 } leaves a hole before the reasons
    REQUIRE(s.decide(Lit(1)));
    REQUIRE(s.propagate() == ClauseRef());
    CHECK(s.value(Lit(3)) == VAL_TRUE);
    const ClauseRef before = s.trail().reason(Var(3));
    s.compact();
    const ClauseRef after = s.trail().reason(Var(3));
    CHECK(after != before);
    CHECK(test::sorted(s.clauses()[after]) == lits({ -2, 3 }));
    s.trail().backtrack();
    REQUIRE(s.decide(Lit(1)));
    REQUIRE(s.propagate() == ClauseRef());
    CHECK(s.value(Lit(3)) == VAL_TRUE);
    CHECK(s.value(Var(5)) == VAL_UNASSIGNED);
}

}  // TEST_SUITE
