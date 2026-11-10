#ifndef PROPAN_TESTS_TEST_UTIL_H_
#define PROPAN_TESTS_TEST_UTIL_H_

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>

#include <unistd.h>

#include "propan/cnf_formula.h"
#include "propan/occurrence_propagator.h"
#include "propan/solver_types.h"
#include "propan/trail.h"
#include "propan/watch_propagator.h"

/// All propagator implementations, for TEST_CASE_TEMPLATE.
#define PROPAGATORS propan::OccurrencePropagator, propan::WatchPropagator

namespace test {

/// Clause from DIMACS literals.
inline propan::Cl lits(std::initializer_list<int> dimacs) {
    propan::Cl c;
    for (int d : dimacs) c.push_back(propan::Lit(d));
    return c;
}

/// Owning copy of a clause view, for comparisons in checks.
inline propan::Cl asCl(std::span<const propan::Lit> clause) {
    return { clause.begin(), clause.end() };
}

/// Clause content as a sorted vector, since propagators may reorder literals.
inline propan::Cl sorted(std::span<const propan::Lit> clause) {
    propan::Cl c = asCl(clause);
    std::sort(c.begin(), c.end());
    return c;
}

/// Formula from clauses given as DIMACS literals.
inline propan::CNFFormula cnf(std::initializer_list<std::initializer_list<int>> clauses) {
    propan::CNFFormula f;
    for (auto& c : clauses) f.addClause(lits(c));
    return f;
}

/// Attaches all clauses of the arena. @return false if any attach() failed
inline bool attachAll(propan::Propagator& p, const propan::ClauseArena& clauses) {
    bool ok = true;
    for (auto clause : clauses) ok = p.attach(clause.ref()) && ok;
    return ok;
}

/// Formula, trail and a concrete propagator bound together, for tests of propagator behaviour.
template<class P>
struct Fixture {
    propan::CNFFormula f;
    propan::Trail t;
    P p;

    explicit Fixture(propan::CNFFormula formula) : f(std::move(formula)), t(f.nVars()), p(f.clauses(), t) { }

    /// Attaches all clauses and propagates the root level. @return false if refuted
    bool init() { return attachAll(p, f.clauses()) && ok(); }

    /// Propagates. @return true if there is no conflict
    bool ok() { return p.propagate() == propan::ClauseRef(); }

    bool isTrue(int d) const { return t.value(propan::Lit(d)) == propan::VAL_TRUE; }

    bool complete() const {
        for (unsigned v = 1; v <= t.nVars(); ++v) {
            if (t.value(propan::Var(v)) == propan::VAL_UNASSIGNED) return false;
        }
        return true;
    }
};

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
