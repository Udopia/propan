#include <doctest/doctest.h>

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "propan/stream_buffer.h"
#include "test_util.h"

using namespace propan;

using test::TempFile;

namespace {

/// Read all integers from a file.
std::vector<int> readAll(const char* filename) {
    StreamBuffer in(filename);
    std::vector<int> result;
    int x;
    while (in.read(x)) result.push_back(x);
    return result;
}

}  // namespace

TEST_SUITE("stream_buffer") {

TEST_CASE("reads signed integers separated by arbitrary whitespace") {
    TempFile f(" 1  -2\t3\n\n-44 +5 0");
    CHECK(readAll(f.c_str()) == std::vector<int> { 1, -2, 3, -44, 5, 0 });
}

TEST_CASE("handles CRLF line endings") {
    TempFile f("c comment\r\n1 2 0\r\n3 0\r\n");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipWhitespace());
    CHECK(*in == 'c');
    REQUIRE(in.skipLine());
    std::vector<int> v;
    int x;
    while (in.read(x)) v.push_back(x);
    CHECK(v == std::vector<int> { 1, 2, 0, 3, 0 });
    CHECK(in.eof());
}

TEST_CASE("last token without trailing newline is read") {
    TempFile f("1 2 0\n3 0");
    CHECK(readAll(f.c_str()) == std::vector<int> { 1, 2, 0, 3, 0 });
}

TEST_CASE("last token without trailing newline is read after skipLine") {
    TempFile f("c comment\n1 2 0\n3 0");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipWhitespace());
    REQUIRE(in.skipLine());
    std::vector<int> v;
    int x;
    while (in.read(x)) v.push_back(x);
    CHECK(v == std::vector<int> { 1, 2, 0, 3, 0 });
}

TEST_CASE("skipLine skips comments and returns false at eof") {
    TempFile f("c first\nc second\n7\n");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipWhitespace());
    REQUIRE(in.skipLine());
    REQUIRE(in.skipLine());
    int x;
    REQUIRE(in.read(x));
    CHECK(x == 7);
    CHECK_FALSE(in.skipWhitespace());
    CHECK(in.eof());
}

TEST_CASE("skipLine over a comment longer than the internal buffer") {
    std::string longComment = "c " + std::string(40000, 'x') + "\n";
    TempFile f(longComment + "5 0\n");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipWhitespace());
    REQUIRE(in.skipLine());
    int x;
    REQUIRE(in.read(x));
    CHECK(x == 5);
}

TEST_CASE("integers crossing the internal buffer boundary are read correctly") {
    std::string content;
    std::vector<int> expected;
    for (int i = 0; i < 20000; ++i) {
        int v = (i % 2 ? -1 : 1) * (100000 + i);
        content += std::to_string(v) + " ";
        expected.push_back(v);
    }
    TempFile f(content);
    CHECK(readAll(f.c_str()) == expected);
}

TEST_CASE("token longer than the internal buffer throws when it is read") {
    TempFile f(std::string(StreamBuffer::BUFFER_SIZE + 1, '1') + " 0\n");
    StreamBuffer in(f.c_str());  // opening succeeds, the token is only inspected on read
    std::string s;
    CHECK_THROWS_AS(in.readNumber(s), ParserException);

    TempFile g(std::string(StreamBuffer::BUFFER_SIZE, '1') + " 0\n");
    StreamBuffer in2(g.c_str());
    CHECK_THROWS_AS(in2.readNumber(s), ParserException);  // no room for the terminating whitespace
}

TEST_CASE("token of almost the buffer size is read, also when it starts late in a chunk") {
    const std::string digits(StreamBuffer::BUFFER_SIZE - 1, '7');
    for (std::size_t prefix : { std::size_t(0), std::size_t(1), StreamBuffer::BUFFER_SIZE / 2, StreamBuffer::BUFFER_SIZE - 2 }) {
        CAPTURE(prefix);
        TempFile f(std::string(prefix, ' ') + digits + " 0");
        StreamBuffer in(f.c_str());
        std::string s;
        REQUIRE(in.readNumber(s));
        CHECK(s == digits);
        int x;
        REQUIRE(in.read(x));
        CHECK(x == 0);
        CHECK_FALSE(in.read(x));
        CHECK(in.eof());
    }
}

TEST_CASE("read<int> rejects INT32_MIN and a bare sign") {
    int x;
    TempFile f("-2147483648\n");
    CHECK_THROWS_AS(StreamBuffer(f.c_str()).read(x), ParserException);
    TempFile g("- 1\n");
    CHECK_THROWS_AS(StreamBuffer(g.c_str()).read(x), ParserException);
    TempFile h("+\n");
    CHECK_THROWS_AS(StreamBuffer(h.c_str()).read(x), ParserException);
}

TEST_CASE("read<uint64_t> rejects negative numbers") {
    TempFile f("-1\n");
    uint64_t x;
    CHECK_THROWS_AS(StreamBuffer(f.c_str()).read(x), ParserException);
}

TEST_CASE("read stops at the first non-digit and the remainder is reported on the next read") {
    TempFile f("12x 3");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.read(x));
    CHECK(x == 12);
    CHECK(*in == 'x');
    CHECK_THROWS_AS(in.read(x), ParserException);
}

TEST_CASE("read leaves the output untouched when the end of the file is reached") {
    TempFile f("  \n\t");
    StreamBuffer in(f.c_str());
    int x = 42;
    CHECK_FALSE(in.read(x));
    CHECK(x == 42);
    CHECK(in.eof());
}

TEST_CASE("read<int> accepts the symmetric int32 range and rejects values outside") {
    TempFile f("2147483647 -2147483647 2147483648\n");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.read(x));
    CHECK(x == std::numeric_limits<int32_t>::max());
    REQUIRE(in.read(x));
    CHECK(x == -std::numeric_limits<int32_t>::max());
    CHECK_THROWS_AS(in.read(x), ParserException);
}

TEST_CASE("read<int> rejects non-numeric input") {
    TempFile f("1 x 0\n");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.read(x));
    CHECK_THROWS_AS(in.read(x), ParserException);
}

TEST_CASE("read<uint64_t> reads full 64-bit range") {
    TempFile f("18446744073709551615 0\n");
    StreamBuffer in(f.c_str());
    uint64_t x = 0;
    REQUIRE(in.read(x));
    CHECK(x == std::numeric_limits<uint64_t>::max());
}

TEST_CASE("read<uint64_t> rejects values beyond 64 bit") {
    TempFile f("18446744073709551616\n");
    StreamBuffer in(f.c_str());
    uint64_t x = 0;
    CHECK_THROWS_AS(in.read(x), ParserException);
}

TEST_CASE("skipString matches prefix and throws on mismatch") {
    TempFile f("p cnf 3 2\n");
    StreamBuffer in(f.c_str());
    CHECK(in.skipString("p cnf"));
    int x;
    REQUIRE(in.read(x));
    CHECK(x == 3);

    TempFile g("p wcnf\n");
    StreamBuffer in2(g.c_str());
    CHECK_THROWS_AS(in2.skipString("p cnf"), ParserException);

    TempFile h("p cn");  // file ends inside the string
    StreamBuffer in3(h.c_str());
    CHECK_THROWS_AS(in3.skipString("p cnf"), ParserException);

    TempFile k("p cnf");  // file ends right after the string
    StreamBuffer in4(k.c_str());
    CHECK_FALSE(in4.skipString("p cnf"));
    CHECK(in4.eof());
}

TEST_CASE("readNumber returns digits as string, keeping '-' and leading zeros") {
    TempFile f("-123 +45 007 340282366920938463463374607431768211456 9x\n");
    StreamBuffer in(f.c_str());
    std::string s;
    REQUIRE(in.readNumber(s));
    CHECK(s == "-123");
    REQUIRE(in.readNumber(s));
    CHECK(s == "45");
    REQUIRE(in.readNumber(s));
    CHECK(s == "007");
    REQUIRE(in.readNumber(s));
    CHECK(s == "340282366920938463463374607431768211456");  // 2^128, beyond any integer type
    REQUIRE(in.readNumber(s));
    CHECK(s == "9");
    CHECK(*in == 'x');
    CHECK_THROWS_AS(in.readNumber(s), ParserException);
    CHECK_FALSE(StreamBuffer(TempFile("\n").c_str()).readNumber(s));
}

TEST_CASE("skipNumber skips signed numbers and rejects other tokens") {
    TempFile f("1 -2 +3 x");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipNumber());
    REQUIRE(in.skipNumber());
    REQUIRE(in.skipNumber());
    CHECK(*in == ' ');
    CHECK_THROWS_AS(in.skipNumber(), ParserException);
    CHECK_FALSE(StreamBuffer(TempFile("").c_str()).skipNumber());
}

TEST_CASE("readClause skips header and comments") {
    TempFile f("c hello\np cnf 3 2\n1 -2 0\nc mid\n3 0\n");
    StreamBuffer in(f.c_str());
    Cl c;
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(1), Lit(-2) });
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(3) });
    CHECK_FALSE(in.readClause(c));
    CHECK(c.empty());
}

TEST_CASE("readClause accepts a final clause without terminating 0 and clears the output at eof") {
    TempFile f("1 2 0\n-3");
    StreamBuffer in(f.c_str());
    Cl c;
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(1), Lit(2) });
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(-3) });
    CHECK_FALSE(in.readClause(c));
    CHECK(c.empty());

    TempFile g("c only comments\nc here\n");
    StreamBuffer in2(g.c_str());
    c.push_back(Lit(9));
    CHECK_FALSE(in2.readClause(c));
    CHECK(c.empty());
}

TEST_CASE("CR-only line endings are treated as line ends") {
    TempFile f("c comment\r1 2 0\r3 0\r");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipLine());
    std::vector<int> v;
    int x;
    while (in.read(x)) v.push_back(x);
    CHECK(v == std::vector<int> { 1, 2, 0, 3, 0 });
}

TEST_CASE("skipBlanks reports whether another token follows on the same line") {
    TempFile f("p wcnf 3 2 \t 10\np wcnf 3 2  \n5 0");
    StreamBuffer in(f.c_str());
    int x;
    uint64_t top = 0;
    REQUIRE(in.skipString("p wcnf"));
    REQUIRE(in.read(x));
    REQUIRE(in.read(x));
    REQUIRE(in.skipBlanks());
    REQUIRE(in.read(top));
    CHECK(top == 10);
    CHECK_FALSE(in.skipBlanks());  // at newline
    REQUIRE(in.skipLine());
    REQUIRE(in.skipString("p wcnf"));
    REQUIRE(in.read(x));
    REQUIRE(in.read(x));
    CHECK_FALSE(in.skipBlanks());  // trailing blanks, then newline
    REQUIRE(in.skipLine());
    REQUIRE(in.read(x));
    CHECK(x == 5);
    REQUIRE(in.read(x));
    CHECK(x == 0);
    CHECK_FALSE(in.skipBlanks());  // at eof
}

TEST_CASE("non-ASCII bytes in comments are skipped") {
    TempFile f("c gr\xc3\xbc\xc3\x9f \xff\xfe\n1 0\n");
    StreamBuffer in(f.c_str());
    Cl c;
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(1, false) });
}

TEST_CASE("corrupt compressed input throws instead of returning garbage") {
    std::string gz;
    {
        std::ifstream src(test::data("sat_small.cnf.gz"), std::ios::binary);
        gz.assign(std::istreambuf_iterator<char>(src), {});
    }
    REQUIRE(gz.size() > 20);
    std::string corrupt = gz.substr(0, 10) + std::string(gz.size() - 10, '\x5a');
    TempFile f(corrupt, ".cnf.gz");
    CHECK_THROWS_AS({ StreamBuffer in(f.c_str()); Cl c; while (in.readClause(c)) { } }, ParserException);
}

TEST_CASE("missing file throws") {
    CHECK_THROWS_AS(StreamBuffer("/nonexistent/propan/file.cnf"), ParserException);
}

TEST_CASE("empty file is a valid empty stream for every operation") {
    TempFile f("");
    StreamBuffer in(f.c_str());
    CHECK(in.eof());
    CHECK(*in == EOF);
    CHECK_FALSE(in.skip());
    CHECK_FALSE(in.skipWhitespace());
    CHECK_FALSE(in.skipBlanks());
    CHECK_FALSE(in.skipLine());
    CHECK_FALSE(in.skipNumber());
    int x;
    CHECK_FALSE(in.read(x));
    std::string s;
    CHECK_FALSE(in.readNumber(s));
    Cl c;
    CHECK_FALSE(in.readClause(c));
    CHECK_THROWS_AS(in.skipString("p"), ParserException);
    CHECK(in.eof());
}

TEST_CASE("whitespace-only file is exhausted by skipWhitespace") {
    TempFile f(" \n\t\r\n ");
    StreamBuffer in(f.c_str());
    CHECK_FALSE(in.eof());
    CHECK_FALSE(in.skipWhitespace());
    CHECK(in.eof());
    CHECK_FALSE(in.skip());
}

TEST_CASE("skip past the end of the file is safe and idempotent") {
    TempFile f("1");
    StreamBuffer in(f.c_str());
    CHECK(*in == '1');
    CHECK_FALSE(in.skip());
    CHECK(in.eof());
    CHECK_FALSE(in.skip());
    CHECK(*in == EOF);
}

TEST_CASE("StreamBuffer is move-only") {
    static_assert(!std::is_copy_constructible_v<StreamBuffer>);
    static_assert(!std::is_copy_assignable_v<StreamBuffer>);
    static_assert(std::is_nothrow_move_constructible_v<StreamBuffer>);
    TempFile f("1 2 0\n");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.read(x));
    StreamBuffer moved(std::move(in));
    REQUIRE(moved.read(x));
    CHECK(x == 2);
    REQUIRE(moved.read(x));
    CHECK(x == 0);
    CHECK_FALSE(moved.read(x));
}

TEST_CASE("transparently decompresses gzip and xz") {
    std::vector<int> expected { 1, 2, 0, -1, 3, 0, -3, 4, 0, -2, -4, 0 };
    for (const char* name : { "sat_small.cnf", "sat_small.cnf.gz", "sat_small.cnf.xz" }) {
        CAPTURE(name);
        StreamBuffer in(test::data(name).c_str());
        std::vector<int> v;
        int x;
        while (in.skipWhitespace()) {
            if (*in == 'c' || *in == 'p') {
                if (!in.skipLine()) break;
            } else if (in.read(x)) {
                v.push_back(x);
            }
        }
        CHECK(v == expected);
    }
}

}  // TEST_SUITE
