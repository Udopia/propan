#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "propan/cnf_formula.h"
#include "propan/wcnf_formula.h"
#include "test_util.h"

using namespace propan;

using test::asCl;
using test::lits;
using test::TempFile;

TEST_SUITE("wcnf_formula") {

TEST_CASE("pre-2022 format: weight >= top marks hard clauses") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    CHECK(f.nVars() == 3);
    REQUIRE(f.nHardClauses() == 2);
    REQUIRE(f.nSoftClauses() == 2);
    CHECK(f.nClauses() == 4);
    CHECK(asCl(f.hard()[0]) == lits({ 1, 2 }));
    CHECK(asCl(f.hard()[1]) == lits({ -1, 3 }));
    CHECK(asCl(f.soft()[0]) == lits({ -3 }));
    CHECK(asCl(f.soft()[1]) == lits({ -2 }));
    CHECK(f.weights() == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("pre-2022 format: header without top weight makes all clauses soft") {
    TempFile file("p wcnf 3 2\n4 1 -2 0\n7 3 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    CHECK(f.nVars() == 3);
    CHECK(f.nHardClauses() == 0);
    REQUIRE(f.nSoftClauses() == 2);
    CHECK(f.weights() == std::vector<uint64_t> { 4, 7 });
    CHECK(asCl(f.soft()[0]) == lits({ 1, -2 }));
}

TEST_CASE("2022 format: 'h' marks hard clauses") {
    WCNFFormula f(test::data("sample_new.wcnf").c_str());
    CHECK(f.nHardClauses() == 2);
    CHECK(f.nSoftClauses() == 2);
    CHECK(f.weights() == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("2022 format: numeric weights without 'h' are soft") {
    TempFile file("c only soft\n2 1 0\n3 -1 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    CHECK(f.nHardClauses() == 0);
    CHECK(f.nSoftClauses() == 2);
    CHECK(f.weights() == std::vector<uint64_t> { 2, 3 });
}

TEST_CASE("weights stay aligned with soft clauses when a tautology is dropped") {
    TempFile file("p wcnf 2 3 10\n10 1 2 0\n4 1 -1 0\n6 -2 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    CHECK(f.nSoftClauses() == 1);
    CHECK(f.weights().size() == f.nSoftClauses());
    CHECK(f.weights().at(0) == 6);
}

TEST_CASE("soft weights larger than 2^31 are preserved") {
    TempFile file("p wcnf 1 2 10000000000\n10000000000 1 0\n5000000000 -1 0\n", ".wcnf");
    WCNFFormula f(file.c_str());
    REQUIRE(f.nSoftClauses() == 1);
    CHECK(f.weights().at(0) == 5000000000ULL);
}

TEST_CASE("hard and soft part share the variable range") {
    WCNFFormula f;
    f.addHardClause(lits({ 1, 2 }));
    f.addSoftClause(lits({ -5 }), 3);
    CHECK(f.nVars() == 5);
    CHECK(f.hard().nVars() == 5);
    CHECK(f.soft().nVars() == 5);
    CHECK(f.newVar() == 6);
    CHECK(f.soft().nVars() == 6);
}

TEST_CASE("hard part can be moved out as a CNFFormula") {
    CNFFormula f;
    {
        WCNFFormula w(test::data("sample_old.wcnf").c_str());
        f = std::move(w.hard());
        CHECK(w.nHardClauses() == 0);
        CHECK(w.nSoftClauses() == 2);
        CHECK(w.nVars() == 3);
    }
    CHECK(f.nVars() == 3);
    REQUIRE(f.nClauses() == 2);
    CHECK(asCl(f[1]) == lits({ -1, 3 }));
}

TEST_CASE("moves transfer all clauses and leave the source empty") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    const Lit* hard0 = f.hard()[0].data();
    WCNFFormula g(std::move(f));
    CHECK(g.hard()[0].data() == hard0);
    CHECK(f.nClauses() == 0);
    CHECK(f.weights().empty());
    WCNFFormula h;
    h = std::move(g);
    CHECK(h.hard()[0].data() == hard0);
    CHECK(h.nClauses() == 4);
    CHECK(h.weights().size() == 2);
    CHECK(g.nClauses() == 0);
}

TEST_CASE("copies are deep") {
    WCNFFormula f(test::data("sample_old.wcnf").c_str());
    WCNFFormula g(f);
    CHECK(g.nClauses() == 4);
    CHECK(g.hard()[0].data() != f.hard()[0].data());
    CHECK(g.weights() == f.weights());
}

}  // TEST_SUITE
