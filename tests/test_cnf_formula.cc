#include <doctest/doctest.h>

#include <utility>

#include "propan/cnf_formula.h"
#include "test_util.h"

using test::TempFile;

namespace {

Cl lits(std::initializer_list<int> dimacs) {
    Cl c;
    for (int d : dimacs) c.push_back(Lit(std::abs(d), d < 0));
    return c;
}

}  // namespace

TEST_SUITE("cnf_formula") {

TEST_CASE("empty formula") {
    CNFFormula f;
    CHECK(f.nVars() == 0);
    CHECK(f.nClauses() == 0);
    CHECK(f.begin() == f.end());
}

TEST_CASE("readClause normalizes: sorted, duplicates removed") {
    CNFFormula f;
    Cl input = lits({ 3, -1, 3, 2, -1 });
    f.readClause(input.begin(), input.end());
    REQUIRE(f.nClauses() == 1);
    CHECK(*f[0] == lits({ -1, 2, 3 }));
    CHECK(f.nVars() == 3);
}

TEST_CASE("readClause drops tautologies") {
    CNFFormula f;
    f.readClause({ Lit(1, false), Lit(2, false), Lit(1, true) });
    CHECK(f.nClauses() == 0);
}

TEST_CASE("readClause keeps the empty clause") {
    CNFFormula f;
    f.readClause({});
    REQUIRE(f.nClauses() == 1);
    CHECK(f[0]->empty());
}

TEST_CASE("nVars tracks the maximum variable") {
    CNFFormula f;
    f.readClause({ Lit(5) });
    f.readClause({ Lit(2) });
    CHECK(f.nVars() == 5);
    CHECK(f.newVar() == 6);
    CHECK(f.nVars() == 6);
}

TEST_CASE("iteration visits clauses in input order") {
    CNFFormula f;
    f.readClause({ Lit(1) });
    f.readClause({ Lit(2) });
    f.readClause({ Lit(3) });
    int expected = 1;
    for (const Cl* c : f) {
        CHECK(int((*c)[0].var()) == expected++);
    }
}

TEST_CASE("parses DIMACS files") {
    CNFFormula f(test::data("sat_small.cnf").c_str());
    CHECK(f.nVars() == 4);
    REQUIRE(f.nClauses() == 4);
    CHECK(*f[0] == lits({ 1, 2 }));
    CHECK(*f[1] == lits({ -1, 3 }));
    CHECK(*f[2] == lits({ -3, 4 }));
    CHECK(*f[3] == lits({ -2, -4 }));
}

TEST_CASE("parses compressed DIMACS files") {
    CNFFormula f(test::data("sat_small.cnf.xz").c_str());
    CHECK(f.nClauses() == 4);
}

TEST_CASE("DIMACS: clauses spanning lines, several per line, comments in between") {
    TempFile file("c head\np cnf 4 3\n1\n2 0 -3\n4 0\nc tail\n-1 0\n");
    CNFFormula f(file.c_str());
    REQUIRE(f.nClauses() == 3);
    CHECK(*f[0] == lits({ 1, 2 }));
    CHECK(*f[1] == lits({ -3, 4 }));
    CHECK(*f[2] == lits({ -1 }));
}

TEST_CASE("DIMACS: last clause without terminating zero and newline is accepted") {
    TempFile file("p cnf 2 2\n1 0\n-1 2");
    CNFFormula f(file.c_str());
    REQUIRE(f.nClauses() == 2);
    CHECK(*f[1] == lits({ -1, 2 }));
}

TEST_CASE("DIMACS: variables declared in header but unused count in nVars") {
    TempFile file("p cnf 10 1\n1 2 0\n");
    CNFFormula f(file.c_str());
    CHECK(f.nVars() == 10);
}

TEST_CASE("copy construction is deep") {
    CNFFormula f;
    f.readClause({ Lit(1), Lit(2) });
    CNFFormula g(f);
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0] != f[0]);
    CHECK(*g[0] == *f[0]);
    CHECK(g.nVars() == f.nVars());
}

TEST_CASE("copy assignment is deep and replaces previous content") {
    CNFFormula f;
    f.readClause({ Lit(1), Lit(2) });
    CNFFormula g;
    g.readClause({ Lit(7) });
    g.readClause({ Lit(8) });
    g = f;
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0] != f[0]);
    CHECK(*g[0] == *f[0]);
    CHECK(g.nVars() == 2);
    g = g;
    CHECK(g.nClauses() == 1);
}

TEST_CASE("move construction transfers clauses") {
    CNFFormula f;
    f.readClause({ Lit(1), Lit(2) });
    const Cl* c = f[0];
    CNFFormula g(std::move(f));
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0] == c);
    CHECK(g.nVars() == 2);
}

TEST_CASE("move assignment transfers clauses and replaces previous content") {
    CNFFormula f;
    f.readClause({ Lit(1), Lit(2) });
    const Cl* c = f[0];
    CNFFormula g;
    g.readClause({ Lit(7) });
    g = std::move(f);
    REQUIRE(g.nClauses() == 1);
    CHECK(g[0] == c);
    CHECK(g.nVars() == 2);
    CHECK(f.nClauses() == 0);
}

TEST_CASE("construction from clause list copies the clauses") {
    Cl a { Lit(1), Lit(2) };
    For clauses { &a };
    CNFFormula f(clauses, 2);
    REQUIRE(f.nClauses() == 1);
    CHECK(f[0] != &a);
    CHECK(*f[0] == a);
}

TEST_CASE("empty file yields an empty formula") {
    TempFile file("");
    CNFFormula f(file.c_str());
    CHECK(f.nClauses() == 0);
    CHECK(f.nVars() == 0);
}

TEST_CASE("clear yields an empty formula") {
    CNFFormula f;
    f.readClause({ Lit(1), Lit(2) });
    f.clear();
    CHECK(f.nClauses() == 0);
    CHECK(f.nVars() == 0);
}

}  // TEST_SUITE
