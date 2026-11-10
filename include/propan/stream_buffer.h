// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Ashlin Iser, KIT - Karlsruhe Institute of Technology

/**
 * @file stream_buffer.h
 * @brief Buffered, token-oriented reader for (compressed) text files such as DIMACS.
 */

#pragma once

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace propan {

/// Error while opening or parsing an input file.
struct ParserException : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/**
 * @brief Buffered reader for text files, transparently decompressing any format
 * supported by libarchive (e.g. gzip, bzip2, xz, lzma).
 *
 * The file is read in chunks into a fixed-size buffer. Before a token is
 * parsed, the buffer is refilled until the token is complete, so tokens may
 * cross chunk boundaries but must not exceed the buffer size (#BUFFER_SIZE);
 * longer tokens are rejected with a ParserException. Lines skipped with
 * skipLine() may be arbitrarily long.
 *
 * Reading methods skip leading whitespace (including newlines) and return
 * `false` if the end of the file is reached before a token is found. Errors
 * are reported by throwing ParserException. The object is move-only.
 */
class StreamBuffer {
 public:
    /// Size of the internal buffer and hence the maximum token length.
    static constexpr std::size_t BUFFER_SIZE = 16384;

    /**
     * @brief Opens the file and reads the first chunk.
     * @throw ParserException if the file cannot be opened or read
     */
    explicit StreamBuffer(const char* filename) : filename_(filename) {
        archive_read_support_filter_all(file_.get());
        archive_read_support_format_raw(file_.get());
        archive_read_support_format_empty(file_.get());
        if (archive_read_open_filename(file_.get(), filename, BUFFER_SIZE) != ARCHIVE_OK) fail(archiveError());
        archive_entry* entry;
        switch (archive_read_next_header(file_.get(), &entry)) {
            case ARCHIVE_OK: refill(); break;
            case ARCHIVE_EOF: drained_ = true; break;  // empty input
            default: fail(archiveError());
        }
    }

    /// Current character, or `EOF` at the end of the file.
    char operator*() const { return eof() ? EOF : cur(); }

    /// True if the end of the file is reached.
    bool eof() const { return pos_ == end_; }

    /**
     * @brief Advances by one character.
     * @return false if the end of the file is reached (or was already)
     */
    bool skip() {
        if (eof()) return false;
        if (++pos_ < end_) return true;
        return refill();
    }

    /**
     * @brief Skips the rest of the current line and all subsequent whitespace.
     * @return false if the end of the file is reached
     */
    bool skipLine() {
        while (!eof() && cur() != '\n' && cur() != '\r') skip();
        return skipWhitespace();
    }

    /**
     * @brief Skips whitespace, including newlines.
     * @return false if the end of the file is reached
     */
    bool skipWhitespace() {
        while (!eof() && isSpace(cur())) skip();
        return !eof();
    }

    /**
     * @brief Skips blanks (spaces and tabs) but not newlines.
     *
     * Allows to detect optional trailing tokens of a line, e.g. an optional
     * field in a header line.
     * @return true if another token follows on the current line
     */
    bool skipBlanks() {
        while (!eof() && (cur() == ' ' || cur() == '\t')) skip();
        return !eof() && !isSpace(cur());
    }

    /**
     * @brief Skips the given string, which must match exactly at the current position.
     * @throw ParserException if the string does not match
     * @return false if the end of the file is reached right after the string
     */
    bool skipString(std::string_view str) {
        for (char c : str) {
            if (eof() || cur() != c) fail("expected '" + std::string(str) + "'");
            skip();
        }
        return !eof();
    }

    /**
     * @brief Reads the next integer after leading whitespace.
     *
     * Accepts an optional sign. For signed types the symmetric range
     * `[-max, max]` is accepted, so that the negation is representable, too.
     * @param[out] out the integer read
     * @throw ParserException if the next token is not an integer or out of range
     * @return false if the end of the file is reached before a token
     */
    template<std::integral T>
    bool read(T& out) {
        if (!skipWhitespace()) return false;
        std::string_view token = this->token();
        if (token.starts_with('+')) token.remove_prefix(1);  // std::from_chars rejects a plus sign
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
        if (ec == std::errc::invalid_argument) unexpected();
        if (ec == std::errc::result_out_of_range) fail("number out of range");
        if constexpr (std::is_signed_v<T>) {
            if (out == std::numeric_limits<T>::min()) fail("number out of range");
        }
        pos_ = ptr - buf_.data();
        return true;
    }

 private:
    // Locale-independent "C" classification; also well-defined for negative chars.
    static bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

    /// Current character. @pre !eof()
    char cur() const { return buf_[pos_]; }

    [[noreturn]] void fail(const std::string& what) const { throw ParserException(filename_ + ": " + what); }
    [[noreturn]] void unexpected() const { fail(std::string("unexpected character: ") + cur()); }

    std::string archiveError() const {
        const char* msg = archive_error_string(file_.get());
        return msg ? msg : "unknown libarchive error";
    }

    /**
     * Moves the unread part of the buffer to the front and appends the next
     * chunk of the file.
     * @throw ParserException if the buffer is full, i.e. a token exceeds #BUFFER_SIZE
     * @return false if nothing is left to read
     */
    bool refill() {
        if (drained_) return false;
        if (pos_ == 0 && end_ == BUFFER_SIZE) fail("maximum token length exceeded");
        std::copy(buf_.begin() + pos_, buf_.begin() + end_, buf_.begin());
        end_ -= pos_;
        pos_ = 0;
        la_ssize_t n = archive_read_data(file_.get(), buf_.data() + end_, BUFFER_SIZE - end_);
        if (n < 0) fail(archiveError());
        end_ += n;
        drained_ = n == 0;
        return !drained_;
    }

    /**
     * The whitespace-delimited token at the current position, refilling the
     * buffer until it is complete. The view is valid until the next refill.
     * @pre !eof()
     */
    std::string_view token() {
        for (;;) {
            const char* begin = buf_.data() + pos_;
            const char* end = buf_.data() + end_;
            const char* space = std::find_if(begin, end, isSpace);
            if (space != end) return { begin, space };
            if (!refill()) return { buf_.data() + pos_, buf_.data() + end_ };
        }
    }

    std::unique_ptr<archive, decltype(&archive_read_free)> file_ { archive_read_new(), archive_read_free };
    std::vector<char> buf_ = std::vector<char>(BUFFER_SIZE);
    std::string filename_;
    std::size_t pos_ = 0;  ///< current read position
    std::size_t end_ = 0;  ///< one past the last valid position
    bool drained_ = false; ///< true once the file has been read completely
};

}  // namespace propan
