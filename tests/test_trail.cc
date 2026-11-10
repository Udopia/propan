#include <doctest/doctest.h>

#include <sstream>
#include <vector>

#include "propan/clause_arena.h"
#include "propan/trail.h"
#include "test_util.h"

using namespace propan;
using test::lits;

namespace {

/// Trail invariants: every trail literal is true, no variable occurs twice,
/// every assigned variable is on the trail, levels partition the trail, a
/// level's segment contains no literal of a higher level, and decisions form
/// a prefix of every non-root level.
void checkInvariants(const Trail& t) {
    std::vector<int> seen(t.nVars() + 1, 0);
    for (Lit l : t) {
        CHECK(t.value(l) == VAL_TRUE);
        CHECK(seen[l.var()]++ == 0);
    }
    for (unsigned v = 1; v <= t.nVars(); ++v) {
        CHECK((t.value(Var(v)) != VAL_UNASSIGNED) == (seen[v] == 1));
    }
    const Lit* expected = t.begin();
    for (unsigned l = 0; l <= t.level(); ++l) {
        auto segment = t.literals(l);
        CHECK(segment.data() == expected);
        expected += segment.size();
        bool decisions = l > 0;
        for (Lit lit : segment) {
            CHECK(t.level(lit.var()) <= l);
            if (!t.isDecision(lit)) decisions = false;
            else CHECK((l == 0 || decisions));
        }
    }
    CHECK(expected == t.end());
}

/// Marks the queue as fully propagated, as a propagator would leave it.
void propagated(Trail& t) { t.head = t.size(); }

}  // namespace

TEST_SUITE("trail") {

TEST_CASE("a fresh trail is empty on the root level") {
    Trail t(3);
    CHECK(t.nVars() == 3);
    CHECK(t.size() == 0);
    CHECK(t.level() == 0);
    CHECK(t.head == 0);
    for (unsigned v = 1; v <= 3; ++v) CHECK(t.value(Var(v)) == VAL_UNASSIGNED);
    CHECK_FALSE(t.backtrack());
    checkInvariants(t);
}

TEST_CASE("resize grows preserving assignments; clear forgets them") {
    Trail t(2);
    t.newLevel();
    t.push(Lit(1));
    t.assign(Lit(-2), ClauseRef(3));
    t.resize(4);
    CHECK(t.nVars() == 4);
    CHECK(t.size() == 2);
    CHECK(t.level() == 1);
    CHECK(t.value(Lit(1)) == VAL_TRUE);
    CHECK(t.value(Lit(2)) == VAL_FALSE);
    CHECK(t.reason(Var(2)) == ClauseRef(3));
    CHECK(t.value(Var(3)) == VAL_UNASSIGNED);
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    propagated(t);
    t.newLevel();
    CHECK(t.push(Lit(4)));
    checkInvariants(t);
    t.clear();
    CHECK(t.nVars() == 4);
    CHECK(t.size() == 0);
    CHECK(t.level() == 0);
    CHECK(t.head == 0);
    for (unsigned v = 1; v <= 4; ++v) CHECK(t.value(Var(v)) == VAL_UNASSIGNED);
}

TEST_CASE("push assigns, accepts repeated and rejects contradicting literals") {
    Trail t(3);
    CHECK(t.push(Lit(1)));
    CHECK(t.value(Lit(1)) == VAL_TRUE);
    CHECK(t.size() == 1);
    CHECK(t.push(Lit(1)));
    CHECK(t.size() == 1);
    CHECK_FALSE(t.push(Lit(-1)));
    CHECK(t.size() == 1);
    CHECK(t.value(Lit(1)) == VAL_TRUE);
    CHECK(t.isDecision(Lit(1)));
    CHECK(t.reason(Var(1)) == ClauseRef());
    CHECK(t.level(Var(1)) == 0);
    checkInvariants(t);
}

TEST_CASE("push of a span stops at the first false literal") {
    Trail t(4);
    t.newLevel();
    CHECK(t.push(lits({ 1, -2 })));
    CHECK(t.size() == 2);
    CHECK_FALSE(t.push(lits({ 3, 2, 4 })));
    CHECK(t.size() == 3);
    CHECK(t.value(Lit(3)) == VAL_TRUE);
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    CHECK(t.literals(1).size() == 3);
    for (Lit l : t.literals(1)) CHECK(t.isDecision(l));
    checkInvariants(t);
}

TEST_CASE("value of a literal is the single-load per-literal value") {
    Trail t(3);
    CHECK(t.value(Lit(1)) == VAL_UNASSIGNED);
    CHECK(t.value(Lit(-1)) == VAL_UNASSIGNED);
    REQUIRE(t.push(Lit(1)));
    REQUIRE(t.push(Lit(-2)));
    CHECK(t.value(Var(1)) == VAL_TRUE);
    CHECK(t.value(Var(2)) == VAL_FALSE);
    CHECK(t.value(Lit(1)) == VAL_TRUE);
    CHECK(t.value(Lit(-1)) == VAL_FALSE);
    CHECK(t.value(Lit(2)) == VAL_FALSE);
    CHECK(t.value(Lit(-2)) == VAL_TRUE);
    CHECK(t.value(Lit(3)) == VAL_UNASSIGNED);
}

TEST_CASE("assign records reason and level; the queue holds unpropagated literals") {
    Trail t(5);
    t.push(Lit(1));  // root fact
    t.head = 1;      // "propagated"
    t.newLevel();
    t.push(Lit(2));
    t.assign(Lit(3), ClauseRef(7));
    CHECK(t.head == 1);
    CHECK(t.reason(Var(3)) == ClauseRef(7));
    CHECK(t.level(Var(3)) == 1);
    CHECK_FALSE(t.isDecision(Lit(3)));
    CHECK(t.isDecision(Lit(2)));
    CHECK(t.level(Var(1)) == 0);
    propagated(t);  // "propagated"
    t.newLevel();
    t.push(Lit(4));
    CHECK(t.level() == 2);
    CHECK(t.literals(0).size() == 1);
    CHECK(t.literals(1).size() == 2);
    CHECK(t.literals(2).size() == 1);
    CHECK(t.literals(1)[1] == Lit(3));
    checkInvariants(t);
}

TEST_CASE("backtrack undoes exactly the last level and resets the queue") {
    Trail t(5);
    t.push(Lit(1));
    t.head = 1;
    t.newLevel();
    t.push(Lit(2));
    t.assign(Lit(3), ClauseRef(0));
    t.head = 3;
    t.newLevel();
    t.push(Lit(4));
    t.assign(Lit(5), ClauseRef(3));
    t.head = 4;  // conflict in the middle of propagation

    CHECK(t.backtrack());
    CHECK(t.level() == 1);
    CHECK(t.size() == 3);
    CHECK(t.head == 3);
    for (int d : { 1, 2, 3 }) CHECK(t.value(Lit(d)) == VAL_TRUE);
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    CHECK(t.value(Var(5)) == VAL_UNASSIGNED);
    checkInvariants(t);

    CHECK(t.backtrack());
    CHECK(t.size() == 1);
    CHECK(t.value(Var(2)) == VAL_UNASSIGNED);
    CHECK_FALSE(t.backtrack());
    CHECK(t.size() == 1);
    checkInvariants(t);
}

TEST_CASE("backtrackTo undoes several levels at once and ignores higher targets") {
    Trail t(6);
    for (int d = 1; d <= 5; ++d) {
        t.newLevel();
        t.push(Lit(d));
        propagated(t);
    }
    CHECK(t.level() == 5);
    t.backtrackTo(7);
    CHECK(t.level() == 5);
    t.backtrackTo(2);
    CHECK(t.level() == 2);
    CHECK(t.size() == 2);
    CHECK(t.value(Lit(2)) == VAL_TRUE);
    CHECK(t.value(Var(3)) == VAL_UNASSIGNED);
    t.backtrackTo(0);
    CHECK(t.level() == 0);
    CHECK(t.size() == 0);
    checkInvariants(t);
}

TEST_CASE("backtrackToRetaining keeps out-of-order assignments and queues them again") {
    Trail t(6);
    t.newLevel();
    t.push(Lit(1));
    t.head = 1;
    t.newLevel();
    t.push(Lit(2));
    t.assign(Lit(3), ClauseRef(0), 1);  // implied by level 1 only, assigned late
    t.assign(Lit(4), ClauseRef(5));
    t.assign(Lit(5), ClauseRef(9), 0);  // a late root-level unit
    t.head = 5;
    checkInvariants(t);

    t.backtrackToRetaining(1);
    CHECK(t.level() == 1);
    CHECK(t.size() == 3);
    CHECK(t.literals(1).size() == 3);
    CHECK(t[0] == Lit(1));
    CHECK(t[1] == Lit(3));
    CHECK(t[2] == Lit(5));
    CHECK(t.value(Var(2)) == VAL_UNASSIGNED);
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    CHECK(t.value(Lit(3)) == VAL_TRUE);
    CHECK(t.value(Lit(5)) == VAL_TRUE);
    CHECK(t.level(Var(3)) == 1);
    CHECK(t.level(Var(5)) == 0);
    CHECK(t.head == 1);  // retained literals must be propagated again
    checkInvariants(t);

    propagated(t);
    t.backtrackToRetaining(0);
    CHECK(t.level() == 0);
    CHECK(t.size() == 1);
    CHECK(t[0] == Lit(5));
    CHECK(t.head == 0);
    checkInvariants(t);
}

TEST_CASE("backtrackToRetaining equals backtrackTo without out-of-order literals") {
    Trail t(4);
    t.newLevel();
    t.push(Lit(1));
    t.assign(Lit(2), ClauseRef(0));
    t.head = 2;
    t.newLevel();
    t.push(Lit(3));
    t.assign(Lit(4), ClauseRef(1));
    t.head = 3;
    t.backtrackToRetaining(1);
    CHECK(t.level() == 1);
    CHECK(t.size() == 2);
    CHECK(t.head == 2);
    CHECK(t.value(Var(3)) == VAL_UNASSIGNED);
    CHECK(t.value(Var(4)) == VAL_UNASSIGNED);
    checkInvariants(t);
}

TEST_CASE("relocate maps reasons through the arena's relocation table") {
    ClauseArena a;
    ClauseRef dead = a.add(lits({ 9 }));
    ClauseRef c1 = a.add(lits({ -1, 2 }));
    ClauseRef c2 = a.add(lits({ -2, 3 }));
    Trail t(3);
    t.newLevel();
    t.push(Lit(1));
    t.assign(Lit(2), c1);
    t.assign(Lit(3), c2);
    a.erase(dead);
    const auto relocation = a.compact();
    REQUIRE(relocation.size() == 2);
    t.relocate(relocation);
    CHECK(t.reason(Var(1)) == ClauseRef());
    CHECK(t.reason(Var(2)) == a.ref(ClauseId(1)));
    CHECK(t.reason(Var(3)) == a.ref(ClauseId(2)));
    CHECK(test::asCl(a[t.reason(Var(3))]) == lits({ -2, 3 }));
}

TEST_CASE("satisfies checks all clauses against the assignment") {
    ClauseArena a;
    a.add(lits({ 1, 2 }));
    a.add(lits({ -1, 3 }));
    Trail t(3);
    CHECK_FALSE(satisfies(t, a));
    t.push(Lit(1));
    CHECK_FALSE(satisfies(t, a));
    t.push(Lit(3));
    CHECK(satisfies(t, a));
    ClauseArena empty;
    CHECK(satisfies(t, empty));
    a.add(lits({}));
    CHECK_FALSE(satisfies(t, a));
}

TEST_CASE("printing") {
    Trail t(3);
    t.push(Lit(1));
    t.push(Lit(-3));
    std::ostringstream os;
    os << t;
    CHECK(os.str() == "1 -3 ");
}

}  // TEST_SUITE
