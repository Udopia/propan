#ifndef PROPAN_TESTS_TEST_UTIL_H_
#define PROPAN_TESTS_TEST_UTIL_H_

#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>

#include <unistd.h>

#include "propan/solver_types.h"

namespace test {

/// Clause from DIMACS literals.
inline propan::Cl lits(std::initializer_list<int> dimacs) {
    propan::Cl c;
    for (int d : dimacs) c.push_back(propan::Lit::fromDimacs(d));
    return c;
}

/// Owning copy of a clause view, for comparisons in checks.
inline propan::Cl asCl(std::span<const propan::Lit> clause) {
    return { clause.begin(), clause.end() };
}

/// Path of a file in tests/data.
inline std::string data(const std::string& name) {
    return std::string(PROPAN_TEST_DATA_DIR) + "/" + name;
}

/// Temporary file with given content, removed on destruction.
class TempFile {
 public:
    explicit TempFile(const std::string& content, const std::string& suffix = ".cnf") {
        static std::atomic<unsigned> counter { 0 };
        path_ = std::filesystem::temp_directory_path() /
            ("propan_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + suffix);
        std::ofstream out(path_, std::ios::binary);
        out << content;
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string path() const { return path_.string(); }
    const char* c_str() const { return path_.c_str(); }

 private:
    std::filesystem::path path_;
};

}  // namespace test

#endif  // PROPAN_TESTS_TEST_UTIL_H_
