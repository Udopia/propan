#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <sstream>
#include <type_traits>
#include <vector>

#include "propan/solver_types.h"

using namespace propan;

TEST_SUITE("solver_types") {

TEST_CASE("Var: construction, conversion and increment") {
    CHECK(Var().id() == 0);
    Var v(5);
    CHECK(int(v) == 5);
    CHECK(int(v++) == 5);
    CHECK(int(v) == 6);
    CHECK(int(++v) == 7);
    Var w = v;
    CHECK(int(w) == 7);
}

TEST_CASE("Lit: DIMACS-style signed encoding") {
    CHECK(Lit() == UNDEF_LIT);
    CHECK(Lit(3, false).toDimacs() == 3);
    CHECK(Lit(3, true).toDimacs() == -3);
    CHECK(Lit(3) == Lit(3, false));
    CHECK(Lit(-3) == Lit(3, true));
    CHECK(Lit(Var(3), true) == Lit(-3));
    CHECK(Lit(Var(3)) == Lit(3));
    static_assert(!std::is_convertible_v<Lit, int>);
    static_assert(!std::is_convertible_v<int, Lit>);
}

TEST_CASE("Lit: index places the two literals of a variable next to each other") {
    CHECK(Lit(3).index() == 6);
    CHECK(Lit(-3).index() == 7);
    CHECK(Lit::fromIndex(6) == Lit(3));
    CHECK(Lit::fromIndex(7) == Lit(-3));
    for (int d : { 1, -1, 17, -17, 123456 }) {
        CHECK(Lit::fromIndex(Lit(d).index()) == Lit(d));
    }
    static_assert(Lit(Var::MAX_ID, true).index() == UINT32_MAX);
}

TEST_CASE("Var and Lit are trivially copyable 32-bit values") {
    static_assert(std::is_trivially_copyable_v<Var>);
    static_assert(std::is_trivially_copyable_v<Lit>);
    static_assert(sizeof(Var) == 4);
    static_assert(sizeof(Lit) == 4);
    static_assert(-Lit(1) == Lit(-1));  // usable in constant expressions
}

TEST_CASE("Lit: var and sign") {
    Lit p(42), n(-42);
    CHECK(int(p.var()) == 42);
    CHECK(int(n.var()) == 42);
    CHECK_FALSE(p.sign());
    CHECK(n.sign());
}

TEST_CASE("Lit: negation and polarity") {
    Lit p(7), n(-7);
    CHECK(-p == n);
    CHECK(-n == p);
    CHECK(-(-p) == p);
    CHECK((p ^ true) == n);
    CHECK((p ^ false) == p);
    CHECK(n.positive() == p);
    CHECK(p.negative() == n);
    CHECK(p.positive() == p);
    CHECK(n.negative() == n);
    CHECK(p != n);
}

TEST_CASE("Lit: ordering follows the index, not the signed value") {
    CHECK(Lit(1) < Lit(-1));
    CHECK(Lit(-1) < Lit(2));
    CHECK_FALSE(Lit(-2) < Lit(1));
    std::vector<Lit> lits { Lit(-2), Lit(3), Lit(1), Lit(-1), Lit(2) };
    std::sort(lits.begin(), lits.end());
    CHECK(lits == std::vector<Lit> { Lit(1), Lit(-1), Lit(2), Lit(-2), Lit(3) });
}

TEST_CASE("Lit: DIMACS round trip") {
    for (int d : { 1, -1, 17, -17, 123456 }) {
        const Lit lit(std::abs(d), d < 0);
        CHECK(lit.toDimacs() == d);
        CHECK(Lit(d) == lit);
    }
}

TEST_CASE("Val encoding: VAL_TRUE = 1, VAL_FALSE = -1, VAL_UNASSIGNED = 0, symmetric under negation") {
    static_assert(std::is_same_v<Val, int8_t>);
    static_assert(VAL_TRUE == 1 && VAL_FALSE == -1 && VAL_UNASSIGNED == 0);
    static_assert(-VAL_TRUE == VAL_FALSE && -VAL_FALSE == VAL_TRUE && -VAL_UNASSIGNED == VAL_UNASSIGNED);
}

TEST_CASE("UNDEF_LIT is the default literal, stored as 0 like the DIMACS terminator") {
    static_assert(UNDEF_LIT.toDimacs() == 0);
    static_assert(std::bit_cast<int32_t>(UNDEF_LIT) == 0);
    static_assert(Lit() == UNDEF_LIT);
    static_assert(UNDEF_LIT != Lit(1) && UNDEF_LIT != Lit(-1));
    static_assert(UNDEF_LIT.var() == Var());
    Lit zeroed[2] {};
    CHECK(zeroed[0] == UNDEF_LIT);
    CHECK(zeroed[1] == UNDEF_LIT);
}

TEST_CASE("LitMap: sized by number of variables, indexed by Lit") {
    LitMap<int> lm(3);
    CHECK(lm.nVars() == 3);
    lm[Lit(2)] = 1;
    lm[Lit(-2)] = -1;
    CHECK(lm[Lit(2)] == 1);
    CHECK(lm[Lit(-2)] == -1);
    CHECK(lm[Lit(3)] == 0);
    lm.resize(4, 9);
    CHECK(lm.nVars() == 4);
    CHECK(lm[Lit(-2)] == -1);
    CHECK(lm[Lit(-4)] == 9);
    lm.clear();
    CHECK(lm.nVars() == 0);

    LitMap<bool> flags(2);
    flags[Lit(-1)] = true;
    CHECK(flags[Lit(-1)]);
    CHECK_FALSE(flags[Lit(1)]);

    LitMap<int> empty;
    CHECK(empty.nVars() == 0);
}

TEST_CASE("printing of Var, Lit and clauses") {
    std::ostringstream os;
    os << Var(3) << "|" << Lit(3) << "|" << Lit(-3);
    CHECK(os.str() == "3|3|-3");

    Cl c { Lit(1), Lit(-2) };
    std::ostringstream oc;
    oc << c;
    CHECK(oc.str() == "1 -2 ");

    std::ostringstream osp;
    osp << std::span<const Lit>(c).subspan(1);
    CHECK(osp.str() == "-2 ");
}

}  // TEST_SUITE
