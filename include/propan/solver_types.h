// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file solver_types.h
 * @brief Basic Boolean types: variables, literals and clauses.
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <span>
#include <vector>

namespace propan {

/**
 * @brief A Boolean variable, identified by a positive integer as in DIMACS.
 *
 * Id 0 is reserved and denotes the undefined variable; its literals are the
 * constants #TRUE_LIT and #FALSE_LIT. The largest id #MAX_ID is one below the
 * representable maximum, as the topmost literal index is #UNDEF_LIT. Converts
 * implicitly to `int` so that it can be used directly as an index into
 * per-variable arrays.
 */
class Var {
 public:
    /// Largest valid variable id.
    static constexpr uint32_t MAX_ID = (UINT32_MAX >> 1) - 1;

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
 * @brief A literal, i.e., a variable or its negation.
 *
 * Encoded as `2 * var + sign`, where sign `true` means negated. Hence, a literal
 * and its complement are adjacent in the order of literals. Converts implicitly
 * to `int` (its index) so that it can be used directly as an index into
 * per-literal arrays such as occurrence lists.
 *
 * The literals of the reserved variable 0 are the constants #TRUE_LIT (index 0)
 * and #FALSE_LIT (index 1). The default literal is #UNDEF_LIT (index
 * `UINT32_MAX`), which denotes "no literal"; it lies outside every per-literal
 * array and must not be negated.
 */
class Lit {
 public:
    /// The undefined literal #UNDEF_LIT ("no literal").
    constexpr Lit() noexcept : x_(UINT32_MAX) { }

    /// Literal of variable with the given id; negated if `sign` is true.
    constexpr Lit(uint32_t var_id, bool sign) noexcept : x_(2 * var_id + sign) { }

    /// Literal of the given variable; negated if `sign` is true.
    constexpr Lit(Var var, bool sign) noexcept : Lit(var.id(), sign) { }

    /// Positive literal of variable with the given id.
    constexpr explicit Lit(uint32_t var_id) noexcept : Lit(var_id, false) { }

    /// Positive literal of the given variable.
    constexpr explicit Lit(Var var) noexcept : Lit(var, false) { }

    /// Literal from its DIMACS representation (e.g. -3 for the negation of variable 3).
    static constexpr Lit fromDimacs(int dimacs) noexcept {
        return dimacs < 0 ? Lit(static_cast<uint32_t>(-dimacs), true) : Lit(static_cast<uint32_t>(dimacs), false);
    }

    /// Literal from its index (inverse of index()).
    static constexpr Lit fromIndex(uint32_t index) noexcept {
        Lit lit;
        lit.x_ = index;
        return lit;
    }

    /// Index into per-literal arrays: `2 * var + sign`.
    constexpr uint32_t index() const noexcept { return x_; }

    /// Index into per-literal arrays (equals index()).
    constexpr operator int() const noexcept { return x_; }

    /// True if the literal is negated.
    constexpr bool sign() const noexcept { return x_ & 1; }

    /// Variable of the literal.
    constexpr Var var() const noexcept { return Var(x_ >> 1); }

    /// Positive literal of the same variable.
    constexpr Lit positive() const noexcept { return fromIndex(x_ & ~1u); }

    /// Negative literal of the same variable.
    constexpr Lit negative() const noexcept { return fromIndex(x_ | 1u); }

    /// DIMACS representation (inverse of fromDimacs()).
    constexpr int toDimacs() const noexcept {
        return sign() ? -static_cast<int>(var().id()) : static_cast<int>(var().id());
    }

    /// Complementary literal.
    constexpr Lit operator~() const noexcept { return fromIndex(x_ ^ 1); }

    /// Complementary literal if `b` is true, same literal otherwise.
    constexpr Lit operator^(bool b) const noexcept { return fromIndex(x_ ^ (b ? 1 : 0)); }

    constexpr bool operator==(Lit p) const noexcept { return x_ == p.x_; }
    constexpr bool operator!=(Lit p) const noexcept { return x_ != p.x_; }

    /// Order by index; `p` and `~p` are adjacent.
    constexpr bool operator<(Lit p) const noexcept { return x_ < p.x_; }

    /// Next literal in index order.
    constexpr Lit& operator++() noexcept {
        ++x_;
        return *this;
    }

    /// Previous literal in index order.
    constexpr Lit& operator--() noexcept {
        --x_;
        return *this;
    }

 private:
    uint32_t x_;
};

/// Constant true literal: positive literal of the reserved variable 0 (index 0).
inline constexpr Lit TRUE_LIT = Lit::fromIndex(0);

/// Constant false literal: negative literal of the reserved variable 0 (index 1).
inline constexpr Lit FALSE_LIT = Lit::fromIndex(1);

/// Undefined literal ("no literal"), index `UINT32_MAX`; equals `Lit()`.
inline constexpr Lit UNDEF_LIT = Lit::fromIndex(UINT32_MAX);

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
