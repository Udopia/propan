#include <doctest/doctest.h>

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "propan/clause_arena.h"
#include "test_util.h"

using namespace propan;

using test::asCl;
using test::lits;

TEST_SUITE("clause_arena") {

TEST_CASE("ClauseRef is a 32-bit offset, default UNDEF") {
    static_assert(sizeof(ClauseRef) == 4);
    static_assert(std::is_trivially_copyable_v<ClauseRef>);
    static_assert(ClauseRef().offset() == ClauseRef::UNDEF);
    static_assert(ClauseRef(42).offset() == 42);
    CHECK(ClauseRef(7) == ClauseRef(7));
    CHECK(ClauseRef(7) != ClauseRef(8));
}

TEST_CASE("ClauseHeader packs size and flags into one word") {
    static_assert(sizeof(ClauseHeader) == 4);
    static_assert(ClauseHeader::FLAG_BITS + ClauseHeader::SIZE_BITS == 32);
    static_assert(ClauseHeader::MAX_SIZE == (uint32_t(1) << ClauseHeader::SIZE_BITS) - 1);
    constexpr ClauseHeader h(ClauseHeader::MAX_SIZE, ClauseHeader::MAX_FLAGS);
    static_assert(h.size() == ClauseHeader::MAX_SIZE);
    static_assert(h.flags() == ClauseHeader::MAX_FLAGS);
    static_assert(ClauseHeader(3, 1).size() == 3);
    static_assert(ClauseHeader(3, 1).flags() == 1);
    static_assert(ClauseHeader::fromWord(ClauseHeader(5, 2).word()) == ClauseHeader(5, 2));
    CHECK(ClauseHeader(3, ClauseHeader::MAX_FLAGS + 1).flags() == 0);  // truncated
}

TEST_CASE("a watcher {blocker, ClauseRef} fits into 8 bytes") {
    struct Watcher {
        Lit blocker;
        ClauseRef ref;
    };
    CHECK(sizeof(Watcher) == 8);
}

TEST_CASE("clauses are stored contiguously as header followed by literals") {
    ClauseArena a;
    ClauseRef r1 = a.add(lits({ 1, -2 }));
    ClauseRef r2 = a.add(lits({ 3 }), 7);
    ClauseRef r3 = a.add(lits({ -1, 2, -3 }));
    CHECK(a.size() == 3);
    CHECK(a.nLits() == 6);
    CHECK(r1 == ClauseRef(0));
    CHECK(r2 == ClauseRef(3));
    CHECK(r3 == ClauseRef(5));
    CHECK(a.header(r1) == ClauseHeader(2, 0));
    CHECK(a.header(r2) == ClauseHeader(1, 7));
    CHECK(asCl(a[r2]) == lits({ 3 }));
    CHECK(a[r3].data() == a.data() + 6);
    CHECK(a.refs() == std::vector<ClauseRef> { r1, r2, r3 });
    CHECK(asCl(a[std::size_t(2)]) == lits({ -1, 2, -3 }));
}

TEST_CASE("flags are mutable and do not affect size or literals") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }), 1);
    ClauseRef s = a.add(lits({ 4 }));
    CHECK(a.flags(r) == 1);
    a.setFlags(r, ClauseHeader::MAX_FLAGS);
    CHECK(a.flags(r) == ClauseHeader::MAX_FLAGS);
    CHECK(a.header(r).size() == 3);
    CHECK(asCl(a[r]) == lits({ 1, 2, 3 }));
    a.setFlags(r, 0);
    CHECK(a.flags(r) == 0);
    CHECK(a.flags(s) == 0);
    CHECK(asCl(a[s]) == lits({ 4 }));
}

TEST_CASE("clauses longer than MAX_SIZE are rejected") {
    if constexpr (ClauseHeader::MAX_SIZE < (1u << 20)) {
        ClauseArena a;
        std::vector<Lit> big(ClauseHeader::MAX_SIZE + 1, Lit::fromDimacs(1));
        CHECK_THROWS_AS(a.add(big), std::length_error);
        CHECK(a.empty());
    }
}

TEST_CASE("references stay valid when the arena grows") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }));
    for (int i = 0; i < 10000; ++i) a.add(lits({ i + 4, -(i + 5) }));
    CHECK(asCl(a[r]) == lits({ 1, 2, 3 }));
}

TEST_CASE("iteration yields literal spans in insertion order") {
    ClauseArena a;
    a.add(lits({ 1 }));
    a.add(lits({}));
    a.add(lits({ 2, 3 }));
    std::vector<Cl> seen;
    for (auto c : a) seen.push_back(asCl(c));
    CHECK(seen == std::vector<Cl> { lits({ 1 }), lits({}), lits({ 2, 3 }) });
}

TEST_CASE("empty clauses are stored with size 0 and have a unique reference") {
    ClauseArena a;
    ClauseRef e = a.add(lits({}));
    ClauseRef r = a.add(lits({ 1 }));
    CHECK(a.header(e).size() == 0);
    CHECK(a[e].empty());
    CHECK(e != r);
    CHECK(a.size() == 2);
    CHECK(a.nLits() == 1);
}

TEST_CASE("literals can be modified in place") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }));
    std::span<Lit> c = a[r];
    std::swap(c[0], c[2]);
    c[1] = Lit::fromDimacs(-5);
    CHECK(asCl(a[r]) == lits({ 3, -5, 1 }));
    CHECK(a.header(r).size() == 3);
}

TEST_CASE("copies are deep, moves leave the source empty") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2 }));
    ClauseArena b(a);
    b[r][0] = Lit::fromDimacs(5);
    CHECK(asCl(a[r]) == lits({ 1, 2 }));
    ClauseArena c(std::move(a));
    CHECK(c.size() == 1);
    CHECK(a.size() == 0);
    CHECK(a.nLits() == 0);
    ClauseArena d;
    d = std::move(c);
    CHECK(d.size() == 1);
    CHECK(c.empty());
}

TEST_CASE("clear removes all clauses") {
    ClauseArena a;
    a.add(lits({ 1, 2 }));
    a.clear();
    CHECK(a.empty());
    CHECK(a.nLits() == 0);
    CHECK(a.begin() == a.end());
}

}  // TEST_SUITE
