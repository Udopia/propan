#include <doctest/doctest.h>

#include <concepts>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "propan/clause_arena.h"
#include "test_util.h"

using namespace propan;

using test::asCl;
using test::lits;

constexpr ClauseFlags learnedFlags() {
    ClauseFlags flags;
    flags.setLearned();
    return flags;
}

TEST_SUITE("clause_arena") {

TEST_CASE("ClauseFlags names independent deletion and learned bits") {
    static_assert(sizeof(ClauseFlags) == 1);
    static_assert(!std::is_convertible_v<uint32_t, ClauseFlags>);
    static_assert(learnedFlags().learned());
    static_assert(!learnedFlags().deleted());
    ClauseFlags flags;
    CHECK_FALSE(flags.deleted());
    CHECK_FALSE(flags.learned());
    flags.setLearned();
    flags.setDeleted();
    CHECK(flags.learned());
    CHECK(flags.deleted());
    CHECK(ClauseFlags::fromBits(flags.bits()) == flags);
    flags.setLearned(false);
    CHECK_FALSE(flags.learned());
    CHECK(flags.deleted());
    flags.setDeleted(false);
    CHECK(flags == ClauseFlags{});
    flags.set(ClauseFlags::LEARNED);
    CHECK(flags.test(ClauseFlags::LEARNED));
    CHECK(flags.learned());
    CHECK_FALSE(flags.test(ClauseFlags::DELETED));
    flags.set(ClauseFlags::DELETED | ClauseFlags::LEARNED);
    CHECK(flags.deleted());
    CHECK(flags.learned());
    flags.set(ClauseFlags::DELETED | ClauseFlags::LEARNED, false);
    CHECK(flags == ClauseFlags{});
}

TEST_CASE("ClauseRef is a 32-bit offset, default UNDEF") {
    static_assert(sizeof(ClauseRef) == 4);
    static_assert(std::is_trivially_copyable_v<ClauseRef>);
    static_assert(ClauseRef().offset() == ClauseRef::UNDEF);
    static_assert(ClauseRef(42).offset() == 42);
    CHECK(ClauseRef(7) == ClauseRef(7));
    CHECK(ClauseRef(7) != ClauseRef(8));
}

TEST_CASE("ClauseId is a distinct 32-bit metadata index") {
    static_assert(sizeof(ClauseId) == 4);
    static_assert(std::is_trivially_copyable_v<ClauseId>);
    static_assert(ClauseId().index() == ClauseId::UNDEF);
    static_assert(ClauseId(42).index() == 42);
    static_assert(!std::is_convertible_v<ClauseRef, ClauseId>);
    static_assert(!std::is_convertible_v<uint32_t, ClauseId>);
    CHECK(ClauseId(7) != ClauseId(8));
}

TEST_CASE("ClauseHeader packs size and flags into one word") {
    static_assert(sizeof(ClauseHeader) == 4);
    static_assert(ClauseHeader::FLAG_BITS + ClauseHeader::SIZE_BITS == 32);
    static_assert(ClauseHeader::MAX_PAYLOAD == (uint32_t(1) << ClauseHeader::SIZE_BITS) - 1);
    static_assert(ClauseHeader::MAX_SIZE == ClauseHeader::MAX_PAYLOAD - 1);
    constexpr ClauseHeader h(ClauseHeader::MAX_SIZE, learnedFlags());
    static_assert(h.size() == ClauseHeader::MAX_SIZE);
    static_assert(h.payload() == ClauseHeader::MAX_PAYLOAD);
    static_assert(ClauseHeader().payload() == 1);
    static_assert(ClauseHeader().size() == 0);
    static_assert(ClauseHeader(3, learnedFlags()).payload() == 4);
    static_assert(h.flags() == learnedFlags());
    static_assert(ClauseHeader(3, learnedFlags()).size() == 3);
    static_assert(ClauseHeader(3, learnedFlags()).flags().learned());
    static_assert(ClauseHeader::fromWord(ClauseHeader(5, learnedFlags()).word()) ==
                  ClauseHeader(5, learnedFlags()));
    CHECK_FALSE(h.deleted());
    auto deleted = h;
    deleted.setDeleted();
    CHECK(deleted.deleted());
    CHECK(deleted.flags().learned());
    CHECK(deleted.payload() == h.payload());
    CHECK(ClauseHeader::padding(0).deleted());
    CHECK(ClauseHeader::padding(0).payload() == 0);
    CHECK(ClauseHeader::padding(ClauseHeader::MAX_PAYLOAD).payload() == ClauseHeader::MAX_PAYLOAD);
}

TEST_CASE("a watcher {blocker, ClauseRef} fits into 8 bytes") {
    struct Watcher {
        Lit blocker;
        ClauseRef ref;
    };
    CHECK(sizeof(Watcher) == 8);
}

TEST_CASE("clauses store size and flags, dense ID, then literals") {
    ClauseArena a;
    ClauseRef r1 = a.add(lits({ 1, -2 }));
    ClauseRef r2 = a.add(lits({ 3 }), learnedFlags());
    ClauseRef r3 = a.add(lits({ -1, 2, -3 }));
    CHECK(a.size() == 3);
    CHECK(a.nLits() == 6);
    CHECK(r1 == ClauseRef(0));
    CHECK(r2 == ClauseRef(4));
    CHECK(r3 == ClauseRef(7));
    CHECK(a.header(r1) == ClauseHeader(2));
    CHECK(a.header(r2) == ClauseHeader(1, learnedFlags()));
    CHECK(asCl(a[r2]) == lits({ 3 }));
    CHECK(a[r3].data() == a.data() + 9);
    CHECK(a.refs() == std::vector<ClauseRef> { r1, r2, r3 });
    CHECK(a.id(r1) == ClauseId(0));
    CHECK(a.id(r2) == ClauseId(1));
    CHECK(a.id(r3) == ClauseId(2));
    CHECK(a.ref(ClauseId(2)) == r3);
    CHECK(a[r2].id() == ClauseId(1));
    CHECK(a[r2].ref() == r2);
    CHECK(a[r2].header() == ClauseHeader(1, learnedFlags()));
    CHECK(asCl(a[ClauseId(1)]) == lits({ 3 }));
    CHECK(a.data()[r2.offset() + 1].index() == 1);
    CHECK(a.nWords() == 12);
}

TEST_CASE("flags are mutable and do not affect size or literals") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }), learnedFlags());
    ClauseRef s = a.add(lits({ 4 }));
    CHECK(a.flags(r).learned());
    auto flags = a.flags(r);
    flags.setLearned(false);
    a.setFlags(r, flags);
    CHECK_FALSE(a.flags(r).learned());
    CHECK(a.header(r).size() == 3);
    CHECK(asCl(a[r]) == lits({ 1, 2, 3 }));
    a.setFlags(r, learnedFlags());
    CHECK(a.flags(r).learned());
    CHECK_FALSE(a.flags(s).learned());
    CHECK(asCl(a[s]) == lits({ 4 }));
}

TEST_CASE("typed flags cannot bypass arena deletion bookkeeping") {
    ClauseArena a;
    ClauseFlags flags = learnedFlags();
    flags.setDeleted();
    CHECK_THROWS_AS(a.add(lits({ 1 }), flags), std::invalid_argument);
    CHECK(a.empty());
    CHECK(a.refs().empty());
    CHECK(a.nWords() == 0);
    ClauseRef ref = a.add(lits({ 1 }), learnedFlags());
    CHECK_THROWS_AS(a.setFlags(ref, flags), std::invalid_argument);
    CHECK_THROWS_AS(a[ref].setFlags(flags), std::invalid_argument);
    CHECK(a.size() == 1);
    CHECK(a.nLits() == 1);
    CHECK(a.flags(ref) == learnedFlags());
    CHECK(a.ref(ClauseId(0)) == ref);
    a.erase(ref);
    CHECK(a.header(ref).flags() == flags);
    CHECK(a.empty());
    CHECK(a.ref(ClauseId(0)) == ClauseRef());
}

TEST_CASE("clauses longer than MAX_SIZE are rejected") {
    ClauseArena a;
    std::vector<Lit> big(ClauseHeader::MAX_SIZE + 1, Lit::fromDimacs(1));
    CHECK_THROWS_AS(a.add(big), std::length_error);
    CHECK(a.empty());
    CHECK(a.refs().empty());
    CHECK(a.nWords() == 0);
}

TEST_CASE("maximum-sized clauses can be deleted without overflowing padding size") {
    ClauseArena a;
    const std::vector<Lit> big(ClauseHeader::MAX_SIZE, Lit(1));
    ClauseRef ref = a.add(big);
    CHECK(a[ref].size() == ClauseHeader::MAX_SIZE);
    CHECK(a[ref][ClauseHeader::MAX_SIZE - 1] == Lit(1));
    ClauseRef next = a.add(lits({ 2 }));
    ClauseHeader before = a.header(ref);
    before.setDeleted();
    a.erase(ref);
    CHECK(a.header(ref) == before);
    CHECK(a.header(ref).payload() == ClauseHeader::MAX_PAYLOAD);
    REQUIRE(a.size() == 1);
    CHECK(a.nLits() == 1);
    REQUIRE(a.begin() != a.end());
    CHECK((*a.begin()).ref() == next);
    CHECK(++a.begin() == a.end());
    a.compact();
    CHECK(a.nWords() == 3);
    CHECK(a.ref(ClauseId(1)) == ClauseRef(0));
}

TEST_CASE("references stay valid when the arena grows") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }));
    for (int i = 0; i < 10000; ++i) a.add(lits({ i + 4, -(i + 5) }));
    CHECK(asCl(a[r]) == lits({ 1, 2, 3 }));
}

TEST_CASE("physical iteration yields clause views") {
    static_assert(std::forward_iterator<ClauseArena::iterator>);
    static_assert(std::forward_iterator<ClauseArena::const_iterator>);
    ClauseArena a;
    a.add(lits({ 1 }));
    a.add(lits({}));
    a.add(lits({ 2, 3 }));
    std::vector<Cl> seen;
    for (auto c : a) seen.push_back(asCl(c));
    CHECK(seen == std::vector<Cl> { lits({ 1 }), lits({}), lits({ 2, 3 }) });
}

TEST_CASE("views and physical iterators preserve constness and expose identity") {
    ClauseArena a;
    ClauseRef ref = a.add(lits({ 1, 2 }));
    ClauseView clause = *a.begin();
    static_assert(std::is_same_v<decltype(clause[0]), Lit&>);
    CHECK(clause.id() == a.id(ref));
    CHECK(clause.ref() == ref);
    clause.setFlags(learnedFlags());
    clause[0] = Lit::fromDimacs(-1);
    std::span<Lit> mutable_lits = clause;
    mutable_lits[1] = Lit(4);
    ConstClauseView converted = clause;
    const ClauseArena& ca = a;
    auto c = *ca.begin();
    CHECK(ca[ClauseId(0)].ref() == ref);
    static_assert(std::is_same_v<decltype(c[0]), const Lit&>);
    CHECK(c.flags().learned());
    CHECK(asCl(c) == lits({ -1, 4 }));
    CHECK(asCl(converted) == asCl(c));
    CHECK(c.literals().data() == clause.literals().data());
    auto it = ca.begin();
    CHECK((it++).operator*().ref() == ref);
    CHECK(it == ca.end());
    CHECK(ClauseArena::const_iterator{} == ClauseArena::const_iterator{});
    ClauseArena other;
    CHECK(other.end() != a.end());
}

TEST_CASE("adding a clause from the same arena survives reallocation") {
    ClauseArena a;
    ClauseRef first = a.add(lits({ 1, -2, 3 }));
    for (unsigned i = 0; i < 100; ++i) {
        ClauseRef copy = a.add(a[first].literals(), learnedFlags());
        CHECK(asCl(a[copy]) == lits({ 1, -2, 3 }));
        CHECK(a.id(copy).index() == i + 1);
        CHECK(asCl(a[first]) == lits({ 1, -2, 3 }));
    }
}

TEST_CASE("shrinking creates skippable padding including a one-word record") {
    ClauseArena a;
    ClauseRef first = a.add(lits({ 1, 2, 3, 4 }), learnedFlags());
    ClauseRef next = a.add(lits({ 5 }));
    const auto words = a.nWords();
    const auto refs = a.refs();
    a.shrink(first, 3);
    CHECK(a.header(first) == ClauseHeader(3, learnedFlags()));
    CHECK(asCl(a[first]) == lits({ 1, 2, 3 }));
    CHECK(ClauseHeader::fromWord(a.data()[5].index()) == ClauseHeader::padding(0));
    a.shrink(first, 1);
    CHECK(a.header(first).payload() == 2);
    CHECK(ClauseHeader::fromWord(a.data()[3].index()) == ClauseHeader::padding(1));
    a.shrink(first, 0);
    CHECK(a.header(first).payload() == 1);
    CHECK(ClauseHeader::fromWord(a.data()[2].index()) == ClauseHeader::padding(0));
    a.shrink(first, 0);
    CHECK(a[first].empty());
    CHECK(a.nLits() == 1);
    CHECK(a.size() == 2);
    CHECK(a.nWords() == words);
    CHECK(a.refs() == refs);
    auto it = a.begin();
    CHECK((*it++).ref() == first);
    CHECK((*it++).ref() == next);
    CHECK(it == a.end());
    CHECK_THROWS_AS(a.shrink(first, 1), std::invalid_argument);
    CHECK(a[first].empty());
    CHECK(a.nLits() == 1);
}

TEST_CASE("deletion skips leading, middle and trailing records without reusing IDs") {
    ClauseArena a;
    ClauseRef leading = a.add(lits({}), learnedFlags());
    ClauseRef live = a.add(lits({ 1, 2 }));
    ClauseRef middle = a.add(lits({ 3 }));
    ClauseRef trailing = a.add(lits({ 4, 5 }));
    ClauseHeader before = a.header(leading);
    before.setDeleted();
    a.erase(leading);
    CHECK(a.header(leading) == before);
    CHECK(a.header(leading).payload() == 1);
    CHECK(a.header(leading).flags().learned());
    a.erase(middle);
    a.erase(trailing);
    CHECK(a.size() == 1);
    CHECK(a.nLits() == 2);
    CHECK(a.refs() == std::vector<ClauseRef>{ ClauseRef(), live, ClauseRef(), ClauseRef() });
    auto it = a.begin();
    REQUIRE(it != a.end());
    CHECK((*it++).ref() == live);
    CHECK(it == a.end());
    a.erase(live);
    CHECK(a.empty());
    CHECK(a.nLits() == 0);
    CHECK(a.begin() == a.end());
    CHECK(a.compact().empty());
    CHECK(a.nWords() == 0);
    CHECK(a.refs().size() == 4);
    CHECK(a.id(a.add(lits({}))) == ClauseId(4));
}

TEST_CASE("iteration can advance after shrinking or deleting the current clause") {
    ClauseArena a;
    for (unsigned i = 0; i < 10; ++i) a.add(lits({ 1, 2, 3 }));
    unsigned visited = 0;
    for (auto clause : a) {
        CHECK(clause.id().index() == visited++);
        const ClauseRef ref = clause.ref();
        const ClauseId id = clause.id();
        a.shrink(ref, 1);
        if (id.index() % 2 == 0) a.erase(ref);
    }
    CHECK(visited == 10);
    CHECK(a.size() == 5);
    CHECK(a.nLits() == 5);
    for (auto clause : a) a.erase(clause.ref());
    CHECK(a.begin() == a.end());
}

TEST_CASE("compaction preserves IDs and metadata and returns reference relocation") {
    ClauseArena a;
    CHECK(a.compact().empty());
    ClauseRef deleted = a.add(lits({ -1 }));
    ClauseRef first = a.add(lits({ 1, 2, 3 }), learnedFlags());
    ClauseRef empty = a.add(lits({}));
    ClauseRef last = a.add(lits({ 4, 5 }), learnedFlags());
    const std::vector<uint64_t> signatures{ 11, 22, 33, 44 };
    a.shrink(first, 2);
    a.erase(deleted);
    const auto relocation = a.compact();
    const std::vector<ClauseArena::Relocation> expected{
        { first, ClauseRef(0) }, { empty, ClauseRef(4) }, { last, ClauseRef(6) }
    };
    CHECK(relocation == expected);
    CHECK(a.nWords() == a.nLits() + 2 * a.size());
    CHECK(a.ref(ClauseId(0)) == ClauseRef());
    CHECK(a.ref(ClauseId(1)) == ClauseRef(0));
    CHECK(a.ref(ClauseId(2)) == ClauseRef(4));
    CHECK(a.ref(ClauseId(3)) == ClauseRef(6));
    CHECK(asCl(a[a.ref(ClauseId(1))]) == lits({ 1, 2 }));
    CHECK(a.flags(a.ref(ClauseId(1))).learned());
    CHECK(a[a.ref(ClauseId(2))].empty());
    CHECK(a.flags(a.ref(ClauseId(3))).learned());
    for (auto clause : a) {
        CHECK(a.ref(clause.id()) == clause.ref());
        CHECK(signatures[clause.id().index()] == uint64_t(11 * (clause.id().index() + 1)));
    }
    const auto again = a.compact();
    REQUIRE(again.size() == 3);
    for (auto [old_ref, new_ref] : again) CHECK(old_ref == new_ref);
    ClauseRef added = a.add(lits({ 6 }));
    CHECK(a.id(added) == ClauseId(4));
    a.shrink(added, 0);
    a.erase(added);
    CHECK(a.ref(ClauseId(4)) == ClauseRef());
}

TEST_CASE("raw words including UNDEF are not mistaken for padding") {
    ClauseArena a;
    ClauseRef ref = a.add(std::vector<Lit>{ UNDEF_LIT, Lit::fromIndex(ClauseHeader::padding(0).word()) });
    CHECK(a.begin() != a.end());
    CHECK(a[ref].size() == 2);
    CHECK(a[ref][0] == UNDEF_LIT);
    CHECK(++a.begin() == a.end());
    a.shrink(ref, 1);
    a.compact();
    CHECK(a[a.ref(ClauseId(0))][0] == UNDEF_LIT);
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

TEST_CASE("copy and move preserve tombstones, padding and live counters") {
    ClauseArena a;
    ClauseRef r = a.add(lits({ 1, 2, 3 }));
    ClauseRef dead = a.add(lits({}));
    a.shrink(r, 1);
    a.erase(dead);
    ClauseArena b = a;
    b[r][0] = Lit(9);
    b.compact();
    CHECK(asCl(a[r]) == lits({ 1 }));
    CHECK(a.nWords() == 7);
    CHECK(b.nWords() == 3);
    CHECK(b.size() == 1);
    CHECK(b.nLits() == 1);
    CHECK(b.refs().size() == 2);
    CHECK(b.ref(ClauseId(1)) == ClauseRef());
    ClauseArena c;
    c = a;
    ClauseArena d(std::move(c));
    CHECK(c.empty());
    CHECK(c.nLits() == 0);
    CHECK(c.refs().empty());
    CHECK(d.refs() == a.refs());
    CHECK(d.nWords() == a.nWords());
    c = std::move(d);
    CHECK(d.begin() == d.end());
    CHECK(d.nWords() == 0);
    CHECK(c.size() == 1);
    CHECK(c.nLits() == 1);
    c.clear();
    CHECK(c.refs().empty());
    CHECK(c.nWords() == 0);
    CHECK(c.id(c.add(lits({}))) == ClauseId(0));
}

TEST_CASE("mixed arena mutations agree with an ID-indexed reference model") {
    ClauseArena a;
    std::vector<std::optional<Cl>> model;
    std::vector<ClauseFlags> flags;
    std::mt19937 random(17);
    for (unsigned step = 0; step < 2000; ++step) {
        unsigned action = random() % 4;
        if (model.empty() || action == 0) {
            Cl clause(random() % 12);
            for (auto& lit : clause) lit = Lit::fromIndex(random() % 100);
            ClauseFlags f;
            f.setLearned(random() % 2 != 0);
            ClauseRef ref = a.add(clause, f);
            CHECK(a.id(ref).index() == model.size());
            model.emplace_back(clause);
            flags.push_back(f);
        } else if (action == 3) {
            const auto old_refs = a.refs();
            const auto relocation = a.compact();
            REQUIRE(relocation.size() == a.size());
            for (auto [old_ref, new_ref] : relocation) {
                CHECK(old_refs[a.id(new_ref).index()] == old_ref);
                CHECK(a.ref(a.id(new_ref)) == new_ref);
            }
            CHECK(a.nWords() == a.nLits() + 2 * a.size());
        } else {
            const auto id = random() % model.size();
            if (model[id]) {
                ClauseRef ref = a.ref(ClauseId(static_cast<uint32_t>(id)));
                if (action == 1) {
                    const auto size = static_cast<uint32_t>(random() % (model[id]->size() + 1));
                    a.shrink(ref, size);
                    model[id]->resize(size);
                } else {
                    a.erase(ref);
                    model[id].reset();
                }
            }
        }
        if (step % 31 != 0 && step + 1 < 2000) continue;
        std::vector<bool> seen(model.size(), false);
        std::size_t count = 0, literals = 0;
        for (auto clause : std::as_const(a)) {
            const auto id = clause.id().index();
            REQUIRE(id < model.size());
            REQUIRE(model[id].has_value());
            CHECK_FALSE(seen[id]);
            seen[id] = true;
            CHECK(asCl(clause) == *model[id]);
            CHECK(clause.flags() == flags[id]);
            CHECK(a.ref(clause.id()) == clause.ref());
            ++count;
            literals += clause.size();
        }
        for (std::size_t i = 0; i < model.size(); ++i) {
            CHECK(seen[i] == model[i].has_value());
            CHECK((a.refs()[i] != ClauseRef()) == model[i].has_value());
        }
        CHECK(a.size() == count);
        CHECK(a.nLits() == literals);
        CHECK(a.empty() == (count == 0));
    }
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
