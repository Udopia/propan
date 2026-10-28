// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file clause_arena.h
 * @brief Flat clause storage: headers and literals in one contiguous array.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "propan/solver_types.h"

namespace propan {

/**
 * @brief Word offset of a clause header in a ClauseArena.
 *
 * The position of the clause header in the arena. References are unique per
 * clause (also for empty clauses) and stay valid when clauses are added or
 * shrunk. Compaction relocates references; deletion invalidates them.
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

/// Stable index into per-clause metadata arrays; never reused until clear().
class ClauseId {
 public:
    static constexpr uint32_t UNDEF = UINT32_MAX;

    constexpr ClauseId() noexcept : index_(UNDEF) { }
    constexpr explicit ClauseId(uint32_t index) noexcept : index_(index) { }
    constexpr uint32_t index() const noexcept { return index_; }
    constexpr bool operator==(const ClauseId&) const noexcept = default;

 private:
    uint32_t index_;
};

/// Clause flag assignments; the remaining six bits are reserved.
class ClauseFlags {
 public:
    static constexpr unsigned BITS = 8;
    static constexpr uint8_t DELETED = uint8_t(1) << 7;
    static constexpr uint8_t LEARNED = uint8_t(1) << 0;

    constexpr ClauseFlags() noexcept = default;

    static constexpr ClauseFlags fromBits(uint8_t bits) noexcept {
        ClauseFlags flags;
        flags.bits_ = bits;
        return flags;
    }

    constexpr uint8_t bits() const noexcept { return bits_; }
    /// True if any bit in the mask is set.
    constexpr bool test(uint8_t mask) const noexcept { return (bits_ & mask) != 0; }
    constexpr bool deleted() const noexcept { return test(DELETED); }
    constexpr bool learned() const noexcept { return test(LEARNED); }

    constexpr void setDeleted(bool value = true) noexcept { set(DELETED, value); }
    constexpr void setLearned(bool value = true) noexcept { set(LEARNED, value); }
    constexpr bool operator==(const ClauseFlags&) const noexcept = default;

    constexpr void set(uint8_t mask, bool value = true) noexcept {
        if (value) bits_ |= mask;
        else bits_ &= static_cast<uint8_t>(~mask);
    }

 private:
    uint8_t bits_ = 0;
};

/**
 * @brief Record header: payload length and typed clause flags.
 *
 * The lower #SIZE_BITS bits always count words following the header.
 * A live payload contains an ID and literals; deleted payload is unused.
 */
class ClauseHeader {
 public:
    /// Number of flag bits, including the reserved deletion bit.
    static constexpr unsigned FLAG_BITS = ClauseFlags::BITS;
    /// Number of size bits.
    static constexpr unsigned SIZE_BITS = 32 - FLAG_BITS;
    static constexpr uint32_t MAX_PAYLOAD = (uint32_t(1) << SIZE_BITS) - 1;
    /// Maximum number of literals, excluding the ID word.
    static constexpr uint32_t MAX_SIZE = MAX_PAYLOAD - 1;
    static_assert(FLAG_BITS > 0 && FLAG_BITS < 32);

    constexpr ClauseHeader() noexcept : word_(1) { }

    /// Live header with the given literal count (at most MAX_SIZE).
    constexpr ClauseHeader(uint32_t size, ClauseFlags flags = {}) noexcept
        : word_((size + 1) | (uint32_t(flags.bits()) << SIZE_BITS)) {
        assert(size <= MAX_SIZE);
        assert(!flags.deleted());
    }

    /// Header from its raw word.
    static constexpr ClauseHeader fromWord(uint32_t word) noexcept {
        ClauseHeader h;
        h.word_ = word;
        return h;
    }

    /// Deleted record with this many payload words after its header.
    static constexpr ClauseHeader padding(uint32_t payload) noexcept {
        assert(payload <= MAX_PAYLOAD);
        ClauseFlags flags;
        flags.setDeleted();
        return fromWord((uint32_t(flags.bits()) << SIZE_BITS) | payload);
    }

    /// The raw word.
    constexpr uint32_t word() const noexcept { return word_; }

    /// Number of words following the header, live or deleted.
    constexpr uint32_t payload() const noexcept { return word_ & MAX_PAYLOAD; }

    /// Number of literals in a live clause.
    constexpr uint32_t size() const noexcept {
        assert(!deleted() && payload() > 0);
        return payload() - 1;
    }

    constexpr ClauseFlags flags() const noexcept {
        return ClauseFlags::fromBits(static_cast<uint8_t>(word_ >> SIZE_BITS));
    }

    constexpr bool deleted() const noexcept { return flags().deleted(); }

    constexpr void setDeleted() noexcept {
        word_ |= uint32_t(ClauseFlags::DELETED) << SIZE_BITS;
    }

    constexpr bool operator==(const ClauseHeader&) const noexcept = default;

 private:
    uint32_t word_;
};

/// Non-owning clause access. Size/identity changes must go through ClauseArena.
template<bool Const>
class BasicClauseView {
 public:
    using pointer = std::conditional_t<Const, const Lit*, Lit*>;

    BasicClauseView(pointer words, ClauseRef ref) noexcept : words_(words), ref_(ref) { }

    ClauseHeader header() const noexcept { return ClauseHeader::fromWord(words_[0].index()); }
    ClauseId id() const noexcept { return ClauseId(words_[1].index()); }
    ClauseRef ref() const noexcept { return ref_; }
    ClauseFlags flags() const noexcept { return header().flags(); }
    void setFlags(ClauseFlags flags) const requires (!Const) {
        if (flags.deleted()) {
            throw std::invalid_argument("propan::ClauseArena: use erase() to delete a clause");
        }
        words_[0] = Lit::fromIndex(ClauseHeader(size(), flags).word());
    }

    uint32_t size() const noexcept { return header().size(); }
    bool empty() const noexcept { return size() == 0; }
    pointer data() const noexcept { return words_ + 2; }
    pointer begin() const noexcept { return data(); }
    pointer end() const noexcept { return data() + size(); }
    auto& operator[](std::size_t i) const noexcept { return data()[i]; }
    std::span<std::conditional_t<Const, const Lit, Lit>> literals() const noexcept {
        return { data(), size() };
    }
    operator std::span<const Lit>() const noexcept { return { data(), size() }; }
    operator std::span<Lit>() const noexcept requires (!Const) { return literals(); }
    operator BasicClauseView<true>() const noexcept requires (!Const) { return { words_, ref_ }; }

 private:
    pointer words_;
    ClauseRef ref_;
};

using ClauseView = BasicClauseView<false>;
using ConstClauseView = BasicClauseView<true>;

/**
 * @brief Flat clause storage with direct offset references and stable dense IDs.
 *
 * Live layout: `[payload/flags][id][literals...]`. Deleted records use
 * `[payload/deleted][unused...]`, permitting single-word padding.
 * Every record occupies 1 + payload words, regardless of deletion.
 * All words are Lit objects; headers/IDs are encoded without type punning.
 * Iteration scans physical records, not the ID-to-reference table.
 *
 * Adding may invalidate views and iterators; shrinking/deleting invalidates
 * views of that clause. An iterator can advance after shrinking/deleting its
 * current clause. Compaction invalidates all views, iterators and offset
 * references.
 * IDs survive compaction. Access by reference requires a live clause boundary;
 * stale references cannot be detected after compaction or clear().
 */
class ClauseArena {
 public:
    template<bool Const>
    class Iterator {
     public:
        using Arena = std::conditional_t<Const, const ClauseArena, ClauseArena>;
        using iterator_category = std::forward_iterator_tag;
        using value_type = BasicClauseView<Const>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        Iterator() = default;
        Iterator(Arena* arena, uint32_t offset) : arena_(arena), offset_(offset) { skipDeleted(); }

        value_type operator*() const { return (*arena_)[ClauseRef(offset_)]; }
        Iterator& operator++() {
            const ClauseHeader h = arena_->header(ClauseRef(offset_));
            offset_ += 1 + h.payload();
            skipDeleted();
            return *this;
        }
        Iterator operator++(int) { Iterator r = *this; ++*this; return r; }
        bool operator==(const Iterator&) const = default;

     private:
        void skipDeleted() {
            while (offset_ < arena_->lits_.size()) {
                ClauseHeader h = arena_->header(ClauseRef(offset_));
                if (!h.deleted()) break;
                offset_ += 1 + h.payload();
            }
        }

        Arena* arena_ = nullptr;
        uint32_t offset_ = 0;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using Relocation = std::pair<ClauseRef, ClauseRef>;

    ClauseArena() = default;
    ClauseArena(const ClauseArena&) = default;
    ClauseArena& operator=(const ClauseArena&) = default;

    /// Leaves `other` empty.
    ClauseArena(ClauseArena&& other) noexcept
        : lits_(std::exchange(other.lits_, {})), refs_(std::exchange(other.refs_, {})),
          count_(std::exchange(other.count_, 0)), n_lits_(std::exchange(other.n_lits_, 0)) { }

    /// Leaves `other` empty.
    ClauseArena& operator=(ClauseArena&& other) noexcept {
        if (this != &other) {
            lits_ = std::exchange(other.lits_, {});
            refs_ = std::exchange(other.refs_, {});
            count_ = std::exchange(other.count_, 0);
            n_lits_ = std::exchange(other.n_lits_, 0);
        }
        return *this;
    }

    /**
     * @brief Appends a clause.
     * @throw std::length_error if the clause or the arena exceeds its maximum size
     * @return reference to the new clause
     */
    ClauseRef add(std::span<const Lit> clause, ClauseFlags flags = {}) {
        if (flags.deleted()) {
            throw std::invalid_argument("propan::ClauseArena: cannot add a deleted clause");
        }
        if (clause.size() > ClauseHeader::MAX_SIZE) {
            throw std::length_error("propan::ClauseArena: clause too long");
        }
        if (clause.size() + 2 >= ClauseRef::UNDEF - lits_.size()) {
            throw std::length_error("propan::ClauseArena: arena full");
        }
        if (refs_.size() >= ClauseId::UNDEF) {
            throw std::length_error("propan::ClauseArena: clause ids exhausted");
        }
        // Preserve a source span into this arena across reallocation.
        const auto less = std::less<const Lit*>{};
        const bool internal = !lits_.empty() && !clause.empty() &&
            !less(clause.data(), lits_.data()) && less(clause.data(), lits_.data() + lits_.size());
        const std::size_t source = internal ? clause.data() - lits_.data() : 0;
        const std::size_t size = clause.size();
        ClauseRef ref(static_cast<uint32_t>(lits_.size()));
        reserveFor(lits_, lits_.size() + size + 2);
        reserveFor(refs_, refs_.size() + 1);
        if (internal) clause = { lits_.data() + source, size };
        lits_.resize(lits_.size() + size + 2);
        lits_[ref.offset()] = Lit::fromIndex(ClauseHeader(static_cast<uint32_t>(size), flags).word());
        lits_[ref.offset() + 1] = Lit::fromIndex(static_cast<uint32_t>(refs_.size()));
        std::copy(clause.begin(), clause.end(), lits_.begin() + ref.offset() + 2);
        refs_.push_back(ref);
        ++count_;
        n_lits_ += size;
        return ref;
    }

    /// Header of the referenced clause.
    ClauseHeader header(ClauseRef ref) const {
        assert(ref.offset() < lits_.size());
        return ClauseHeader::fromWord(lits_[ref.offset()].index());
    }

    /// Stable metadata index of a live clause.
    ClauseId id(ClauseRef ref) const {
        assert(!header(ref).deleted());
        return ClauseId(lits_[ref.offset() + 1].index());
    }

    /// Current offset for an issued ID; UNDEF if that clause was deleted.
    ClauseRef ref(ClauseId id) const {
        assert(id.index() < refs_.size());
        return refs_[id.index()];
    }

    /// Flags of the referenced clause.
    ClauseFlags flags(ClauseRef ref) const { return header(ref).flags(); }

    /// Replaces flags of a live clause; use erase() for deletion.
    void setFlags(ClauseRef ref, ClauseFlags flags) {
        (*this)[ref].setFlags(flags);
    }

    ClauseView operator[](ClauseRef ref) {
        assert(!header(ref).deleted());
        return { lits_.data() + ref.offset(), ref };
    }

    ConstClauseView operator[](ClauseRef ref) const {
        assert(!header(ref).deleted());
        return { lits_.data() + ref.offset(), ref };
    }

    ClauseView operator[](ClauseId id) { return (*this)[ref(id)]; }
    ConstClauseView operator[](ClauseId id) const { return (*this)[ref(id)]; }

    /// ID-to-reference table, including UNDEF slots for deleted clauses.
    const std::vector<ClauseRef>& refs() const noexcept { return refs_; }

    /// Keeps the first `size` literals; arrange retained literals before calling.
    void shrink(ClauseRef ref, uint32_t size) {
        ClauseHeader h = header(ref);
        assert(!h.deleted());
        if (size > h.size()) throw std::invalid_argument("propan::ClauseArena: shrink would grow clause");
        if (size == h.size()) return;
        lits_[ref.offset() + 2 + size] = Lit::fromIndex(
            ClauseHeader::padding(h.size() - size - 1).word());
        lits_[ref.offset()] = Lit::fromIndex(ClauseHeader(size, h.flags()).word());
        n_lits_ -= h.size() - size;
    }

    /// Deletes a live clause. Its ID is not reused.
    void erase(ClauseRef ref) {
        ClauseHeader h = header(ref);
        const ClauseId clause_id = id(ref);
        const uint32_t size = h.size();
        h.setDeleted();
        lits_[ref.offset()] = Lit::fromIndex(h.word());
        refs_[clause_id.index()] = ClauseRef();
        --count_;
        n_lits_ -= size;
    }

    /**
     * @brief Removes dead storage, preserving IDs and updating refs().
     * @return old/new offsets for every live clause, sorted by old offset
     *
     * Callers must relocate external offsets or rebuild their dependent structures
     * before accessing clauses again. Deleted references are absent from the map.
     * Metadata indexed by ClauseId does not need relocation.
     */
    std::vector<Relocation> compact() {
        std::vector<Lit> packed;
        std::vector<Relocation> relocation;
        packed.reserve(n_lits_ + 2 * count_);
        relocation.reserve(count_);
        for (auto clause : *this) {
            ClauseRef next(static_cast<uint32_t>(packed.size()));
            relocation.emplace_back(clause.ref(), next);
            const auto first = lits_.begin() + clause.ref().offset();
            packed.insert(packed.end(), first, first + clause.size() + 2);
        }
        for (const auto& [old_ref, new_ref] : relocation) {
            refs_[id(old_ref).index()] = new_ref;
        }
        lits_.swap(packed);
        return relocation;
    }

    /// The raw arena (headers and literals).
    Lit* data() noexcept { return lits_.data(); }

    /// The raw arena (headers and literals).
    const Lit* data() const noexcept { return lits_.data(); }

    iterator begin() { return { this, 0 }; }
    iterator end() { return { this, static_cast<uint32_t>(lits_.size()) }; }
    const_iterator begin() const { return { this, 0 }; }
    const_iterator end() const { return { this, static_cast<uint32_t>(lits_.size()) }; }

    /// Number of live clauses.
    std::size_t size() const noexcept { return count_; }

    /// True if there are no clauses.
    bool empty() const noexcept { return count_ == 0; }

    /// Total number of literals in all clauses (excluding headers).
    std::size_t nLits() const noexcept { return n_lits_; }

    /// Allocated record words, including dead storage (not vector capacity).
    std::size_t nWords() const noexcept { return lits_.size(); }

    /// Removes all clauses.
    void clear() noexcept {
        lits_.clear();
        refs_.clear();
        count_ = 0;
        n_lits_ = 0;
    }

 private:
    template<class T>
    static void reserveFor(std::vector<T>& buffer, std::size_t required) {
        if (required > buffer.capacity()) {
            buffer.reserve(std::max(required, buffer.capacity() + buffer.capacity() / 2));
        }
    }

    std::vector<Lit> lits_;
    std::vector<ClauseRef> refs_;
    std::size_t count_ = 0;
    std::size_t n_lits_ = 0;
};

static_assert(sizeof(ClauseRef) == 4);
static_assert(sizeof(ClauseId) == 4);
static_assert(sizeof(ClauseHeader) == 4);
static_assert(sizeof(ClauseFlags) == 1);
static_assert(std::is_trivially_copyable_v<ClauseRef>);
static_assert(std::is_trivially_copyable_v<ClauseId>);
static_assert(std::is_trivially_copyable_v<ClauseHeader>);
static_assert(std::is_trivially_copyable_v<ClauseFlags>);

}  // namespace propan
