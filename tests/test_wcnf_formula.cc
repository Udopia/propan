#include <doctest/doctest.h>

#include "propan/cnf_formula.h"
#include "propan/wcnf_formula.h"
#include "test_util.h"

using test::TempFile;

namespace {

Cl lits(std::initializer_list<int> dimacs) {
    Cl c;
    for (int d : dimacs) c.push_back(Lit(std::abs(d), d < 0));
    return c;
}

}  // namespace

TEST_SUITE("wcnf_formula") {

TEST_CASE("pre-2022 format: weight >= top marks hard clauses") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    CHECK(f.nVars() == 3);
    REQUIRE(f.nHardClauses() == 2);
    REQUIRE(f.nSoftClauses() == 2);
    CHECK(f.nClauses() == 4);
    CHECK(*f.hard_formula[0] == lits({ 1, 2 }));
    CHECK(*f.hard_formula[1] == lits({ -1, 3 }));
    CHECK(*f.soft_formula[0] == lits({ -3 }));
    CHECK(*f.soft_formula[1] == lits({ -2 }));
    CHECK(f.soft_weights == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("operator[] indexes hard clauses first, then soft clauses") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    CHECK(f[0] == f.hard_formula[0]);
    CHECK(f[1] == f.hard_formula[1]);
    CHECK(f[2] == f.soft_formula[0]);
    CHECK(f[3] == f.soft_formula[1]);
}

TEST_CASE("hard and soft iterators") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    CHECK(std::distance(f.hard_begin(), f.hard_end()) == 2);
    CHECK(std::distance(f.soft_begin(), f.soft_end()) == 2);
}

TEST_CASE("2022 format: 'h' marks hard clauses") {
    WCNFFormula f(test::data("sample_new.wcnf").c_str());
    CHECK(f.nHardClauses() == 2);
    CHECK(f.nSoftClauses() == 2);
    CHECK(f.soft_weights == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("weights stay aligned with soft clauses when a tautology is dropped") {
    TempFile file("p wcnf 2 3 10\n10 1 2 0\n4 1 -1 0\n6 -2 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    CHECK(f.nSoftClauses() == 1);
    CHECK(f.soft_weights.size() == f.nSoftClauses());
    CHECK(f.soft_weights.at(0) == 6);
}

TEST_CASE("soft weights larger than 2^31 are preserved") {
    TempFile file("p wcnf 1 2 10000000000\n10000000000 1 0\n5000000000 -1 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    REQUIRE(f.nSoftClauses() == 1);
    CHECK(f.soft_weights.at(0) == 5000000000ULL);
}

TEST_CASE("hard part converts to an independent CNFFormula") {
    CNFFormula f;
    {
        WCNFFormula w(test::data("sample_old.wcnf").c_str());
        f = CNFFormula(w.hard_formula, w.nVars());
        CHECK(f[0] != w.hard_formula[0]);
    }
    REQUIRE(f.nClauses() == 2);
    CHECK(f.nVars() == 3);
    CHECK(*f[0] == lits({ 1, 2 }));
}

TEST_CASE("2022 format: numeric weights without 'h' are soft") {
    TempFile file("c only soft\n2 1 0\n3 -1 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    CHECK(f.nHardClauses() == 0);
    CHECK(f.nSoftClauses() == 2);
    CHECK(f.soft_weights == std::vector<uint64_t> { 2, 3 });
}

TEST_CASE("move construction and assignment transfer all clauses") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    const Cl* hard0 = f.hard_formula[0];
    WCNFFormula g(std::move(f));
    CHECK(g.hard_formula[0] == hard0);
    CHECK(f.nClauses() == 0);
    WCNFFormula h;
    h = std::move(g);
    CHECK(h.hard_formula[0] == hard0);
    CHECK(h.nClauses() == 4);
    CHECK(h.soft_weights.size() == 2);
    CHECK(g.nClauses() == 0);
}

}  // TEST_SUITE
