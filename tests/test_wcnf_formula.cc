#include <doctest/doctest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "propan/cnf_formula.h"
#include "propan/dimacs.h"
#include "propan/wcnf_formula.h"
#include "test_util.h"

using namespace propan;

using test::asCl;
using test::lits;
using test::TempFile;

namespace {

std::vector<uint64_t> weights(const WCNFFormula& f) {
    std::vector<uint64_t> w;
    for (const SoftClause& c : f.soft()) w.push_back(c.weight);
    return w;
}

}  // namespace

TEST_SUITE("wcnf_formula") {

TEST_CASE("pre-2022 format: weight >= top marks hard clauses") {
    WCNFFormula f = readWcnf(test::data("sample_old.wcnf").c_str());
    CHECK(f.nVars() == 3);
    REQUIRE(f.nHardClauses() == 2);
    REQUIRE(f.nSoftClauses() == 2);
    CHECK(asCl(f.hard()[ClauseId(0)]) == lits({ 1, 2 }));
    CHECK(asCl(f.hard()[ClauseId(1)]) == lits({ -1, 3 }));
    CHECK(f.soft()[0].lits == lits({ -3 }));
    CHECK(f.soft()[1].lits == lits({ -2 }));
    CHECK(weights(f) == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("pre-2022 format: header without top weight makes all clauses soft") {
    TempFile file("p wcnf 3 2\n4 1 -2 0\n7 3 0\n", ".wcnf");
    WCNFFormula f = readWcnf(file.c_str());
    CHECK(f.nVars() == 3);
    CHECK(f.nHardClauses() == 0);
    REQUIRE(f.nSoftClauses() == 2);
    CHECK(weights(f) == std::vector<uint64_t> { 4, 7 });
    CHECK(f.soft()[0].lits == lits({ 1, -2 }));
}

TEST_CASE("2022 format: 'h' marks hard clauses") {
    WCNFFormula f = readWcnf(test::data("sample_new.wcnf").c_str());
    CHECK(f.nHardClauses() == 2);
    CHECK(f.nSoftClauses() == 2);
    CHECK(weights(f) == std::vector<uint64_t> { 3, 5 });
}

TEST_CASE("2022 format: numeric weights without 'h' are soft") {
    TempFile file("c only soft\n2 1 0\n3 -1 0\n", ".wcnf");
    WCNFFormula f = readWcnf(file.c_str());
    CHECK(f.nHardClauses() == 0);
    CHECK(f.nSoftClauses() == 2);
    CHECK(weights(f) == std::vector<uint64_t> { 2, 3 });
}

TEST_CASE("soft clauses are normalized and tautologies dropped") {
    TempFile file("p wcnf 2 3 10\n10 1 2 0\n4 1 -1 0\n6 -2 2 -2 0\n", ".wcnf");
    WCNFFormula f = readWcnf(file.c_str());
    CHECK(f.nHardClauses() == 1);
    REQUIRE(f.nSoftClauses() == 0);  // {1,-1} and {-2,2} are both tautologies
    WCNFFormula g;
    CHECK(g.addSoftClause(lits({ 3, -1, 3 }), 6));
    CHECK_FALSE(g.addSoftClause(lits({ 2, -2 }), 1));
    REQUIRE(g.nSoftClauses() == 1);
    CHECK(g.soft()[0].lits == lits({ -1, 3 }));
    CHECK(g.soft()[0].weight == 6);
    CHECK(g.nVars() == 3);
}

TEST_CASE("soft weights larger than 2^31 are preserved") {
    TempFile file("p wcnf 1 2 10000000000\n10000000000 1 0\n5000000000 -1 0\n", ".wcnf");
    WCNFFormula f = readWcnf(file.c_str());
    REQUIRE(f.nSoftClauses() == 1);
    CHECK(f.soft()[0].weight == 5000000000ULL);
}

TEST_CASE("variable range covers both parts") {
    WCNFFormula f;
    f.addHardClause(lits({ 1, 2 }));
    f.addSoftClause(lits({ -5 }), 3);
    CHECK(f.nVars() == 5);
    CHECK(f.hard().nVars() == 2);
    f.ensureVars(7);
    CHECK(f.nVars() == 7);
    f.clear();
    CHECK(f.nVars() == 0);
    CHECK(f.nSoftClauses() == 0);
}

TEST_CASE("hard part can be moved out as a CNFFormula") {
    CNFFormula f;
    {
        WCNFFormula w = readWcnf(test::data("sample_old.wcnf").c_str());
        f = std::move(w.hard());
        CHECK(w.nHardClauses() == 0);
        CHECK(w.nSoftClauses() == 2);
    }
    CHECK(f.nVars() == 3);
    REQUIRE(f.nClauses() == 2);
    CHECK(asCl(f[ClauseId(1)]) == lits({ -1, 3 }));
}

TEST_CASE("moves transfer all clauses and leave the source empty") {
    WCNFFormula f = readWcnf(test::data("sample_old.wcnf").c_str());
    const Lit* hard0 = f.hard()[ClauseId(0)].data();
    WCNFFormula g(std::move(f));
    CHECK(g.hard()[ClauseId(0)].data() == hard0);
    CHECK(f.nHardClauses() == 0);
    CHECK(f.soft().empty());
    WCNFFormula h;
    h = std::move(g);
    CHECK(h.hard()[ClauseId(0)].data() == hard0);
    CHECK(h.nHardClauses() == 2);
    CHECK(h.nSoftClauses() == 2);
    CHECK(g.nHardClauses() == 0);
}

TEST_CASE("copies are deep") {
    WCNFFormula f = readWcnf(test::data("sample_old.wcnf").c_str());
    WCNFFormula g(f);
    CHECK(g.nHardClauses() == 2);
    CHECK(g.hard()[ClauseId(0)].data() != f.hard()[ClauseId(0)].data());
    CHECK(weights(g) == weights(f));
}

}  // TEST_SUITE
