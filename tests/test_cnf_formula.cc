#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "propan/cnf_formula.h"
#include "test_util.h"

using namespace propan;

using test::asCl;
using test::lits;
using test::TempFile;

TEST_SUITE("cnf_formula") {

TEST_CASE("empty formula") {
    CNFFormula f;
    CHECK(f.nVars() == 0);
    CHECK(f.nClauses() == 0);
    CHECK(f.begin() == f.end());
}

TEST_CASE("addClause normalizes: sorted, duplicates removed") {
    CNFFormula f;
    CHECK(f.addClause(lits({ 3, -1, 3, 2, -1 })));
    REQUIRE(f.nClauses() == 1);
    CHECK(asCl(f[0]) == lits({ -1, 2, 3 }));
    CHECK(f.nVars() == 3);
}

TEST_CASE("addClause drops tautologies") {
    CNFFormula f;
    CHECK_FALSE(f.addClause({ Lit(1, false), Lit(2, false), Lit(1, true) }));
    CHECK(f.nClauses() == 0);
}

TEST_CASE("addClause keeps the empty clause") {
    CNFFormula f;
    CHECK(f.addClause({}));
    REQUIRE(f.nClauses() == 1);
    CHECK(f[0].empty());
}

TEST_CASE("normalize") {
    Cl c = lits({ 2, -1, 2 });
    CHECK(CNFFormula::normalize(c));
    CHECK(c == lits({ -1, 2 }));
    Cl t = lits({ 2, -1, -2 });
    CHECK_FALSE(CNFFormula::normalize(t));
}

TEST_CASE("nVars tracks the maximum variable") {
    CNFFormula f;
    f.addClause({ Lit(5) });
    f.addClause({ Lit(2) });
    CHECK(f.nVars() == 5);
    CHECK(f.newVar() == 6);
    CHECK(f.nVars() == 6);
    f.ensureVars(4);
    CHECK(f.nVars() == 6);
    f.ensureVars(9);
    CHECK(f.nVars() == 9);
}

TEST_CASE("iteration visits clauses in input order") {
    CNFFormula f;
    f.addClause({ Lit(1) });
    f.addClause({ Lit(2) });
    f.addClause({ Lit(3) });
    int expected = 1;
    for (auto c : f) {
        CHECK(int(c[0].var()) == expected++);
    }
}

TEST_CASE("clauses are accessible by reference") {
    CNFFormula f;
    f.addClause(lits({ 1, 2 }));
    f.addClause(lits({ -3 }));
    const auto& refs = f.clauses().refs();
    REQUIRE(refs.size() == 2);
    CHECK(asCl(f[refs[1]]) == lits({ -3 }));
    CHECK(f.clauses().header(refs[1]).size() == 1);
}

TEST_CASE("parses DIMACS files") {
    CNFFormula f(test::data("sat_small.cnf").c_str());
    CHECK(f.nVars() == 4);
    REQUIRE(f.nClauses() == 4);
    CHECK(asCl(f[0]) == lits({ 1, 2 }));
    CHECK(asCl(f[1]) == lits({ -1, 3 }));
    CHECK(asCl(f[2]) == lits({ -3, 4 }));
    CHECK(asCl(f[3]) == lits({ -2, -4 }));
}

TEST_CASE("parses compressed DIMACS files") {
    CNFFormula f(test::data("sat_small.cnf.xz").c_str());
    CHECK(f.nClauses() == 4);
}

TEST_CASE("DIMACS: clauses spanning lines, several per line, comments in between") {
    TempFile file("c head\np cnf 4 3\n1\n2 0 -3\n4 0\nc tail\n-1 0\n");
    CNFFormula f(file.c_str());
    REQUIRE(f.nClauses() == 3);
    CHECK(asCl(f[0]) == lits({ 1, 2 }));
    CHECK(asCl(f[1]) == lits({ -3, 4 }));
    CHECK(asCl(f[2]) == lits({ -1 }));
}

TEST_CASE("DIMACS: last clause without terminating zero and newline is accepted") {
    TempFile file("p cnf 2 2\n1 0\n-1 2");
    CNFFormula f(file.c_str());
    REQUIRE(f.nClauses() == 2);
    CHECK(asCl(f[1]) == lits({ -1, 2 }));
}

TEST_CASE("DIMACS: variables declared in header but unused count in nVars") {
    TempFile file("p cnf 10 1\n1 2 0\n");
    CNFFormula f(file.c_str());
    CHECK(f.nVars() == 10);
}

TEST_CASE("copies are deep") {
    CNFFormula f;
    f.addClause({ Lit(1), Lit(2) });
    CNFFormula g(f);
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0].data() != f[0].data());
    CHECK(asCl(g[0]) == asCl(f[0]));
    CHECK(g.nVars() == f.nVars());

    CNFFormula h;
    h.addClause({ Lit(7) });
    h.addClause({ Lit(8) });
    h = f;
    REQUIRE(h.nClauses() == 1);
    CHECK(h[0].data() != f[0].data());
    CHECK(asCl(h[0]) == asCl(f[0]));
    CHECK(h.nVars() == 2);
}

TEST_CASE("moves transfer the clauses and leave the source empty") {
    CNFFormula f;
    f.addClause({ Lit(1), Lit(2) });
    const Lit* data = f[0].data();
    CNFFormula g(std::move(f));
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0].data() == data);
    CHECK(g.nVars() == 2);
    CHECK(f.nClauses() == 0);
    CHECK(f.nVars() == 0);

    CNFFormula h;
    h.addClause({ Lit(7) });
    h = std::move(g);
    REQUIRE(h.nClauses() == 1);
    CHECK(h[0].data() == data);
    CHECK(h.nVars() == 2);
    CHECK(g.nClauses() == 0);
    CHECK(g.nVars() == 0);
}

TEST_CASE("empty file yields an empty formula") {
    TempFile file("");
    CNFFormula f(file.c_str());
    CHECK(f.nClauses() == 0);
    CHECK(f.nVars() == 0);
}

TEST_CASE("clear yields an empty formula") {
    CNFFormula f;
    f.addClause({ Lit(1), Lit(2) });
    f.clear();
    CHECK(f.nClauses() == 0);
    CHECK(f.nVars() == 0);
}

}  // TEST_SUITE
