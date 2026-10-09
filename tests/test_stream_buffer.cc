#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <vector>

#include "propan/stream_buffer.h"
#include "test_util.h"

using test::TempFile;

namespace {

/// Read all integers from a file.
std::vector<int> readAll(const char* filename) {
    StreamBuffer in(filename);
    std::vector<int> result;
    int x;
    while (in.readInteger(&x)) result.push_back(x);
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
    while (in.readInteger(&x)) v.push_back(x);
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
    while (in.readInteger(&x)) v.push_back(x);
    CHECK(v == std::vector<int> { 1, 2, 0, 3, 0 });
}

TEST_CASE("skipLine skips comments and returns false at eof") {
    TempFile f("c first\nc second\n7\n");
    StreamBuffer in(f.c_str());
    REQUIRE(in.skipWhitespace());
    REQUIRE(in.skipLine());
    REQUIRE(in.skipLine());
    int x;
    REQUIRE(in.readInteger(&x));
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
    REQUIRE(in.readInteger(&x));
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

TEST_CASE("token longer than the internal buffer throws") {
    TempFile f(std::string(20000, '1') + " 0\n");
    CHECK_THROWS_AS(StreamBuffer(f.c_str()), ParserException);
}

TEST_CASE("readInteger rejects values outside int32 range") {
    TempFile f("2147483647 -2147483647 2147483648\n");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.readInteger(&x));
    CHECK(x == std::numeric_limits<int32_t>::max());
    REQUIRE(in.readInteger(&x));
    CHECK(x == -std::numeric_limits<int32_t>::max());
    CHECK_THROWS_AS(in.readInteger(&x), ParserException);
}

TEST_CASE("readInteger rejects non-numeric input") {
    TempFile f("1 x 0\n");
    StreamBuffer in(f.c_str());
    int x;
    REQUIRE(in.readInteger(&x));
    CHECK_THROWS_AS(in.readInteger(&x), ParserException);
}

TEST_CASE("readUInt64 reads full 64-bit range") {
    TempFile f("18446744073709551615 0\n");
    StreamBuffer in(f.c_str());
    uint64_t x = 0;
    REQUIRE(in.readUInt64(&x));
    CHECK(x == std::numeric_limits<uint64_t>::max());
}

TEST_CASE("readUInt64 rejects values beyond 64 bit") {
    TempFile f("18446744073709551616\n");
    StreamBuffer in(f.c_str());
    uint64_t x = 0;
    CHECK_THROWS_AS(in.readUInt64(&x), ParserException);
}

TEST_CASE("skipString matches prefix and throws on mismatch") {
    TempFile f("p cnf 3 2\n");
    StreamBuffer in(f.c_str());
    CHECK(in.skipString("p cnf"));
    int x;
    REQUIRE(in.readInteger(&x));
    CHECK(x == 3);

    TempFile g("p wcnf\n");
    StreamBuffer in2(g.c_str());
    CHECK_THROWS_AS(in2.skipString("p cnf"), ParserException);
}

TEST_CASE("readNumber returns digits as string") {
    TempFile f("-123 +45\n");
    StreamBuffer in(f.c_str());
    std::string s;
    REQUIRE(in.readNumber(&s));
    CHECK(s == "-123");
    REQUIRE(in.readNumber(&s));
    CHECK(s == "45");
}

TEST_CASE("readClause skips header and comments") {
    TempFile f("c hello\np cnf 3 2\n1 -2 0\nc mid\n3 0\n");
    StreamBuffer in(f.c_str());
    Cl c;
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(1, false), Lit(2, true) });
    REQUIRE(in.readClause(c));
    CHECK(c == Cl { Lit(3, false) });
    CHECK_FALSE(in.readClause(c));
}

TEST_CASE("missing file throws") {
    CHECK_THROWS_AS(StreamBuffer("/nonexistent/propan/file.cnf"), ParserException);
}

TEST_CASE("empty file is a valid empty stream") {
    TempFile f("");
    StreamBuffer in(f.c_str());
    CHECK(in.eof());
    CHECK_FALSE(in.skipWhitespace());
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
            } else if (in.readInteger(&x)) {
                v.push_back(x);
            }
        }
        CHECK(v == expected);
    }
}

}  // TEST_SUITE
