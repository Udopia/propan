// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file solver_types.h
 * @brief Basic Boolean types: variables, literals, clauses and maps indexed by them.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <ostream>
#include <span>
#include <vector>

namespace propan {

/**
 * @brief A Boolean variable, identified by a positive integer as in DIMACS.
 *
 * Id 0 is reserved and denotes the undefined variable. Converts implicitly to
 * `int` so that it can be used directly as an index into per-variable arrays.
 */
class Var {
 public:
    /// Largest valid variable id.
    static constexpr uint32_t MAX_ID = INT32_MAX;

    /// Undefined variable (id 0).
    constexpr Var() noexcept : id_(0) { }

    /// Variable with the given DIMACS id.
    constexpr explicit Var(uint32_t id) noexcept : id_(id) { }

    /// DIMACS id of the variable.
    constexpr uint32_t id() const noexcept { return id_; }

    /// Index into per-variable arrays (equals id()).
    constexpr operator int() const noexcept { return id_; }

    /// Advance to the next variable (prefix).
    constexpr Var& operator++() noexcept {
        ++id_;
        return *this;
    }

    /// Advance to the next variable (postfix).
    constexpr Var operator++(int) noexcept {
        Var result(*this);
        ++id_;
        return result;
    }

 private:
    uint32_t id_;
};

/**
 * @brief A literal, i.e., a variable or its negation, in DIMACS style.
 *
 * Stored as the signed integer of the DIMACS format: `var` for the positive
 * and `-var` for the negative literal, so the complement of `lit` is `-lit`
 * and toDimacs() is the identity. The default literal is #UNDEF_LIT ("no
 * literal"), stored as 0 like the DIMACS clause terminator; it has no
 * variable and must not be negated.
 *
 * A literal does not convert to an integer. Use toDimacs() for the signed
 * value and index() for the position in per-literal arrays, where the two
 * literals of a variable are adjacent: `index() = 2 * var + sign`. LitMap
 * wraps a vector indexed by literals. Comparison with `<` follows the index,
 * not the signed value, so sorting places `lit` right before `-lit`.
 */
class Lit {
 public:
    /// The undefined literal #UNDEF_LIT ("no literal").
    constexpr Lit() noexcept : x_(0) { }

    /// Literal from its DIMACS representation (e.g. -3 for the negation of variable 3).
    constexpr explicit Lit(int dimacs) noexcept : x_(dimacs) { }

    /// Literal of variable with the given id; negated if `sign` is true.
    constexpr Lit(uint32_t var_id, bool sign) noexcept
        : x_(sign ? -static_cast<int32_t>(var_id) : static_cast<int32_t>(var_id)) {
        assert(var_id <= Var::MAX_ID);
    }

    /// Literal of the given variable; negated if `sign` is true.
    constexpr Lit(Var var, bool sign) noexcept : Lit(var.id(), sign) { }

    /// Positive literal of the given variable.
    constexpr explicit Lit(Var var) noexcept : Lit(var, false) { }

    /// Literal from its index (inverse of index()).
    static constexpr Lit fromIndex(uint32_t index) noexcept { return Lit(index >> 1, index & 1); }

    /// Index into per-literal arrays: `2 * var + sign`.
    constexpr uint32_t index() const noexcept { return 2 * var().id() + sign(); }

    /// True if the literal is negated.
    constexpr bool sign() const noexcept { return x_ < 0; }

    /// Variable of the literal.
    constexpr Var var() const noexcept { return Var(static_cast<uint32_t>(x_ < 0 ? -x_ : x_)); }

    /// Positive literal of the same variable.
    constexpr Lit positive() const noexcept { return sign() ? -*this : *this; }

    /// Negative literal of the same variable.
    constexpr Lit negative() const noexcept { return sign() ? *this : -*this; }

    /// DIMACS representation (inverse of Lit(int)).
    constexpr int toDimacs() const noexcept { return x_; }

    /// Complementary literal.
    constexpr Lit operator-() const noexcept { return Lit(-x_); }

    /// Complementary literal if `b` is true, same literal otherwise.
    constexpr Lit operator^(bool b) const noexcept { return b ? -*this : *this; }

    constexpr bool operator==(const Lit&) const noexcept = default;

    /// Order by index(); `p` and `-p` are adjacent.
    constexpr bool operator<(Lit p) const noexcept { return index() < p.index(); }

 private:
    int32_t x_;
};

/// Undefined literal ("no literal"), stored as 0; equals `Lit()`.
inline constexpr Lit UNDEF_LIT = Lit();

/**
 * @brief A vector with one element per literal, indexed by Lit via Lit::index().
 *
 * The two literals of a variable occupy adjacent slots. Slots 0 and 1 belong
 * to the undefined variable, so a map over `n` variables holds `2 * n + 2`
 * elements.
 */
template<typename T>
class LitMap {
 public:
    using reference = typename std::vector<T>::reference;
    using const_reference = typename std::vector<T>::const_reference;

    LitMap() = default;

    /// Map over the literals of variables `1..n_vars`, every element initialized to `value`.
    explicit LitMap(unsigned n_vars, const T& value = T()) : data_(2 * n_vars + 2, value) { }

    /// Resizes to cover the literals of variables `1..n_vars`; new elements are initialized to `value`.
    void resize(unsigned n_vars, const T& value = T()) { data_.resize(2 * n_vars + 2, value); }

    /// Removes all elements (zero variables).
    void clear() noexcept { data_.clear(); }

    /// Sets every element to `value`.
    void assign(const T& value) { std::fill(data_.begin(), data_.end(), value); }

    /// Number of variables covered.
    unsigned nVars() const noexcept { return data_.empty() ? 0 : static_cast<unsigned>(data_.size() / 2 - 1); }

    reference operator[](Lit lit) {
        assert(lit.index() < data_.size());
        return data_[lit.index()];
    }

    const_reference operator[](Lit lit) const {
        assert(lit.index() < data_.size());
        return data_[lit.index()];
    }

    auto begin() noexcept { return data_.begin(); }
    auto end() noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end() const noexcept { return data_.end(); }

 private:
    std::vector<T> data_;
};

/**
 * @brief Truth value of a variable or literal: #VAL_TRUE, #VAL_FALSE or #VAL_UNASSIGNED.
 *
 * The encoding is symmetric, so the value of a negative literal is the
 * negated value of its variable.
 */
using Val = int8_t;

/// Value of a true variable or literal.
inline constexpr Val VAL_TRUE = 1;

/// Value of a false variable or literal.
inline constexpr Val VAL_FALSE = -1;

/// Value of an unassigned variable or literal.
inline constexpr Val VAL_UNASSIGNED = 0;

/// An owning clause buffer, e.g. for building a clause before adding it to a formula.
typedef std::vector<Lit> Cl;

/// Prints the DIMACS id of the variable.
inline std::ostream& operator<<(std::ostream& stream, Var const& var) {
    return stream << var.id();
}

/// Prints the literal in DIMACS notation.
inline std::ostream& operator<<(std::ostream& stream, Lit const& lit) {
    return stream << lit.toDimacs();
}

/// Prints the literals of the clause, each followed by a space.
inline std::ostream& operator<<(std::ostream& stream, Cl const& clause) {
    for (Lit lit : clause) {
        stream << lit << " ";
    }
    return stream;
}

/// Prints the literals of the clause, each followed by a space.
inline std::ostream& operator<<(std::ostream& stream, std::span<const Lit> clause) {
    for (Lit lit : clause) {
        stream << lit << " ";
    }
    return stream;
}

}  // namespace propan
