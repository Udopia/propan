#include <doctest/doctest.h>

#include <sstream>
#include <type_traits>

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

TEST_CASE("Lit: encoding as 2*var+sign") {
    CHECK(Lit() == UNDEF_LIT);
    CHECK(Lit(3, false).index() == 6);
    CHECK(Lit(3, true).index() == 7);
    CHECK(Lit(3).index() == 6);
    CHECK(Lit(Var(3), true).index() == 7);
    CHECK(Lit(Var(3)) == Lit(3, false));
    CHECK(int(Lit(3, true)) == 7);
    CHECK(Lit::fromIndex(7) == Lit(3, true));
}

TEST_CASE("Var and Lit are trivially copyable 32-bit values") {
    static_assert(std::is_trivially_copyable_v<Var>);
    static_assert(std::is_trivially_copyable_v<Lit>);
    static_assert(sizeof(Var) == 4);
    static_assert(sizeof(Lit) == 4);
    static_assert(~Lit(1, false) == Lit(1, true));  // usable in constant expressions
}

TEST_CASE("Lit: var and sign") {
    Lit p(42, false), n(42, true);
    CHECK(int(p.var()) == 42);
    CHECK(int(n.var()) == 42);
    CHECK_FALSE(p.sign());
    CHECK(n.sign());
}

TEST_CASE("Lit: negation and polarity") {
    Lit p(7, false), n(7, true);
    CHECK(~p == n);
    CHECK(~n == p);
    CHECK(~~p == p);
    CHECK((p ^ true) == n);
    CHECK((p ^ false) == p);
    CHECK(n.positive() == p);
    CHECK(p.negative() == n);
    CHECK(p.positive() == p);
    CHECK(p != n);
}

TEST_CASE("Lit: ordering places complementary literals next to each other") {
    CHECK(Lit(1, false) < Lit(1, true));
    CHECK(Lit(1, true) < Lit(2, false));
    Lit l(3, false);
    ++l;
    CHECK(l == Lit(3, true));
    --l;
    CHECK(l == Lit(3, false));
}

TEST_CASE("Lit: DIMACS round trip") {
    for (int d : { 1, -1, 17, -17, 123456 }) {
        const Lit lit(std::abs(d), d < 0);
        CHECK(lit.toDimacs() == d);
        CHECK(Lit::fromDimacs(d) == lit);
    }
}

TEST_CASE("Val encoding: VAL_TRUE = 1, VAL_FALSE = -1, VAL_UNASSIGNED = 0, symmetric under negation") {
    static_assert(std::is_same_v<Val, int8_t>);
    static_assert(VAL_TRUE == 1 && VAL_FALSE == -1 && VAL_UNASSIGNED == 0);
    static_assert(-VAL_TRUE == VAL_FALSE && -VAL_FALSE == VAL_TRUE && -VAL_UNASSIGNED == VAL_UNASSIGNED);
}

TEST_CASE("UNDEF_LIT is the default literal at the topmost index, distinct from all variable literals") {
    static_assert(UNDEF_LIT.index() == UINT32_MAX);
    static_assert(Lit() == UNDEF_LIT);
    static_assert(UNDEF_LIT != TRUE_LIT && UNDEF_LIT != FALSE_LIT);
    static_assert(Lit(Var::MAX_ID, true).index() < UNDEF_LIT.index());
    static_assert(Lit(Var::MAX_ID, true).index() == UINT32_MAX - 2);
}

TEST_CASE("TRUE_LIT and FALSE_LIT are the literals of the reserved variable 0") {
    static_assert(TRUE_LIT.index() == 0 && FALSE_LIT.index() == 1);
    CHECK(TRUE_LIT.var() == Var());
    CHECK_FALSE(TRUE_LIT.sign());
    CHECK(FALSE_LIT.sign());
    CHECK(~TRUE_LIT == FALSE_LIT);
    CHECK(~FALSE_LIT == TRUE_LIT);
    CHECK((TRUE_LIT ^ true) == FALSE_LIT);
    CHECK(TRUE_LIT < FALSE_LIT);
    CHECK(FALSE_LIT < Lit(1, false));
}

TEST_CASE("printing of Var, Lit and clauses") {
    std::ostringstream os;
    os << Var(3) << "|" << Lit(3, false) << "|" << Lit(3, true);
    CHECK(os.str() == "3|3|-3");

    Cl c { Lit(1, false), Lit(2, true) };
    std::ostringstream oc;
    oc << c;
    CHECK(oc.str() == "1 -2 ");

    std::ostringstream osp;
    osp << std::span<const Lit>(c).subspan(1);
    CHECK(osp.str() == "-2 ");
}

}  // TEST_SUITE
