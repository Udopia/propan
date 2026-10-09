// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file clause_arena.h
 * @brief Flat clause storage: headers and literals in one contiguous array.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "propan/solver_types.h"

namespace propan {

/**
 * @brief Reference to a clause in a ClauseArena; also serves as the clause id.
 *
 * The position of the clause header in the arena. References are unique per
 * clause (also for empty clauses) and stay valid when clauses are added.
 */
class ClauseRef {
 public:
    /// Reference to no clause.
    static constexpr uint32_t UNDEF = UINT32_MAX;

    /// Reference to no clause (#UNDEF).
    constexpr ClauseRef() noexcept : offset_(UNDEF) { }

    constexpr explicit ClauseRef(uint32_t offset) noexcept : offset_(offset) { }

    /// Position of the clause header in the arena.
    constexpr uint32_t offset() const noexcept { return offset_; }

    constexpr bool operator==(const ClauseRef&) const noexcept = default;

 private:
    uint32_t offset_;
};

/**
 * @brief Clause header: size and user-defined flags packed into one 32-bit word.
 *
 * The flags occupy the upper #FLAG_BITS bits, the size the remaining lower
 * bits. Change #FLAG_BITS to trade flags for maximum clause size.
 */
class ClauseHeader {
 public:
    /// Number of flag bits.
    static constexpr unsigned FLAG_BITS = 8;
    /// Number of size bits.
    static constexpr unsigned SIZE_BITS = 32 - FLAG_BITS;
    /// Maximum number of literals in a clause.
    static constexpr uint32_t MAX_SIZE = (uint32_t(1) << SIZE_BITS) - 1;
    /// All flag bits set.
    static constexpr uint32_t MAX_FLAGS = (uint32_t(1) << FLAG_BITS) - 1;

    static_assert(FLAG_BITS > 0 && FLAG_BITS < 32);

    constexpr ClauseHeader() noexcept : word_(0) { }

    /// Header with the given size and flags (both truncated to their bit width).
    constexpr ClauseHeader(uint32_t size, uint32_t flags) noexcept
        : word_((size & MAX_SIZE) | ((flags & MAX_FLAGS) << SIZE_BITS)) { }

    /// Header from its raw word.
    static constexpr ClauseHeader fromWord(uint32_t word) noexcept {
        ClauseHeader h;
        h.word_ = word;
        return h;
    }

    /// The raw word.
    constexpr uint32_t word() const noexcept { return word_; }

    /// Number of literals.
    constexpr uint32_t size() const noexcept { return word_ & MAX_SIZE; }

    /// User-defined flags.
    constexpr uint32_t flags() const noexcept { return word_ >> SIZE_BITS; }

    constexpr bool operator==(const ClauseHeader&) const noexcept = default;

 private:
    uint32_t word_;
};

/**
 * @brief Flat storage for clauses in the style of MiniSat: one contiguous
 * array holding, for each clause, a header word followed by its literals,
 * plus the list of clause references in insertion order.
 *
 * Layout: `[header][lit_0]...[lit_{n-1}][header][lit_0]...`. The header
 * (ClauseHeader) lies on the same cache line as the first literals, so
 * reading the size and flags costs no additional memory access. Header words
 * are stored as Lit objects with ClauseHeader::word() as index, so that the
 * whole arena is a single `Lit` array without type punning.
 *
 * References stay valid when clauses are added (they are offsets, not
 * pointers); spans obtained from operator[] do not.
 */
class ClauseArena {
 public:
    /// Iterator over the clauses as literal spans, in insertion order.
    class const_iterator {
     public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::span<const Lit>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        const_iterator() = default;
        const_iterator(const ClauseArena* arena, std::vector<ClauseRef>::const_iterator it)
            : arena_(arena), it_(it) { }

        value_type operator*() const { return (*arena_)[*it_]; }
        const_iterator& operator++() { ++it_; return *this; }
        const_iterator operator++(int) { const_iterator r = *this; ++it_; return r; }
        bool operator==(const const_iterator& o) const { return it_ == o.it_; }

     private:
        const ClauseArena* arena_ = nullptr;
        std::vector<ClauseRef>::const_iterator it_;
    };

    ClauseArena() = default;
    ClauseArena(const ClauseArena&) = default;
    ClauseArena& operator=(const ClauseArena&) = default;

    /// Leaves `other` empty.
    ClauseArena(ClauseArena&& other) noexcept
        : lits_(std::exchange(other.lits_, {})), refs_(std::exchange(other.refs_, {})) { }

    /// Leaves `other` empty.
    ClauseArena& operator=(ClauseArena&& other) noexcept {
        if (this != &other) {
            lits_ = std::exchange(other.lits_, {});
            refs_ = std::exchange(other.refs_, {});
        }
        return *this;
    }

    /**
     * @brief Appends a clause.
     * @throw std::length_error if the clause or the arena exceeds its maximum size
     * @return reference to the new clause
     */
    ClauseRef add(std::span<const Lit> clause, uint32_t flags = 0) {
        if (clause.size() > ClauseHeader::MAX_SIZE) {
            throw std::length_error("propan::ClauseArena: clause too long");
        }
        if (lits_.size() + clause.size() + 1 >= ClauseRef::UNDEF) {
            throw std::length_error("propan::ClauseArena: arena full");
        }
        ClauseRef ref(static_cast<uint32_t>(lits_.size()));
        ClauseHeader header(static_cast<uint32_t>(clause.size()), flags);
        lits_.push_back(Lit::fromIndex(header.word()));
        lits_.insert(lits_.end(), clause.begin(), clause.end());
        refs_.push_back(ref);
        return ref;
    }

    /// Header of the referenced clause.
    ClauseHeader header(ClauseRef ref) const { return ClauseHeader::fromWord(lits_[ref.offset()].index()); }

    /// Flags of the referenced clause.
    uint32_t flags(ClauseRef ref) const { return header(ref).flags(); }

    /// Replaces the flags of the referenced clause (truncated to ClauseHeader::FLAG_BITS).
    void setFlags(ClauseRef ref, uint32_t flags) {
        lits_[ref.offset()] = Lit::fromIndex(ClauseHeader(header(ref).size(), flags).word());
    }

    /// Literals of the referenced clause.
    std::span<Lit> operator[](ClauseRef ref) { return { lits_.data() + ref.offset() + 1, header(ref).size() }; }

    /// Literals of the referenced clause.
    std::span<const Lit> operator[](ClauseRef ref) const {
        return { lits_.data() + ref.offset() + 1, header(ref).size() };
    }

    /// Literals of the i-th clause in insertion order.
    std::span<const Lit> operator[](std::size_t i) const { return (*this)[refs_[i]]; }

    /// References of all clauses in insertion order.
    const std::vector<ClauseRef>& refs() const noexcept { return refs_; }

    /// The raw arena (headers and literals).
    Lit* data() noexcept { return lits_.data(); }

    /// The raw arena (headers and literals).
    const Lit* data() const noexcept { return lits_.data(); }

    const_iterator begin() const { return { this, refs_.begin() }; }
    const_iterator end() const { return { this, refs_.end() }; }

    /// Number of clauses.
    std::size_t size() const noexcept { return refs_.size(); }

    /// True if there are no clauses.
    bool empty() const noexcept { return refs_.empty(); }

    /// Total number of literals in all clauses (excluding headers).
    std::size_t nLits() const noexcept { return lits_.size() - refs_.size(); }

    /// Removes all clauses.
    void clear() noexcept {
        lits_.clear();
        refs_.clear();
    }

 private:
    std::vector<Lit> lits_;
    std::vector<ClauseRef> refs_;
};

static_assert(sizeof(ClauseRef) == 4);
static_assert(sizeof(ClauseHeader) == 4);
static_assert(std::is_trivially_copyable_v<ClauseRef>);
static_assert(std::is_trivially_copyable_v<ClauseHeader>);

}  // namespace propan
