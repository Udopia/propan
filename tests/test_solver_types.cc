#include <doctest/doctest.h>

#include <sstream>

#include "propan/solver_types.h"

TEST_SUITE("solver_types") {

TEST_CASE("Var: construction, conversion and increment") {
    CHECK(Var().id == 0);
    Var v(5);
    CHECK(int(v) == 5);
    CHECK(int(v++) == 5);
    CHECK(int(v) == 6);
    CHECK(int(++v) == 7);
    Var w = v;
    CHECK(int(w) == 7);
}

TEST_CASE("Lit: encoding as 2*var+sign") {
    CHECK(Lit().x == 0);
    CHECK(Lit(3, false).x == 6);
    CHECK(Lit(3, true).x == 7);
    CHECK(Lit(3).x == 6);
    CHECK(Lit(Var(3), true).x == 7);
    CHECK(Lit(Var(3)) == Lit(3, false));
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
        Lit lit(std::abs(d), d < 0);
        CHECK(lit.toDimacs() == d);
    }
}

TEST_CASE("printing of Var, Lit, Cl and For") {
    std::ostringstream os;
    os << Var(3) << "|" << Lit(3, false) << "|" << Lit(3, true);
    CHECK(os.str() == "3|3|-3");

    Cl c { Lit(1, false), Lit(2, true) };
    std::ostringstream oc;
    oc << c;
    CHECK(oc.str() == "1 -2 ");

    Cl d { Lit(3, false) };
    For f { &c, &d };
    std::ostringstream of;
    of << f;
    CHECK(of.str() == "1 -2 \n3 \n");
}

}  // TEST_SUITE
