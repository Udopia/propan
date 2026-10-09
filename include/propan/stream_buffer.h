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
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <string>

#include "propan/solver_types.h"

namespace propan {

/// Error while opening or parsing an input file.
class ParserException : public std::exception {
 public:
    explicit ParserException(const std::string& what) noexcept : what_(what) { }

    const char* what() const noexcept override { return what_.c_str(); }

 private:
    std::string what_;
};

/**
 * @brief Buffered reader for text files, transparently decompressing any format
 * supported by libarchive (e.g. gzip, bzip2, xz, lzma).
 *
 * The file is read in chunks into a fixed-size buffer. Each chunk is aligned
 * to end at whitespace, so that a token never crosses a chunk boundary; tokens
 * longer than the buffer are rejected with a ParserException (except within
 * lines skipped by skipLine()).
 *
 * Reading methods skip leading whitespace (including newlines) and return
 * `false` if the end of the file is reached before a token is found. Errors
 * are reported by throwing ParserException.
 *
 * Not copyable: the object owns the archive handle and the buffer.
 */
class StreamBuffer {
 public:
    /**
     * @brief Opens the file and reads the first chunk.
     * @throw ParserException if the file cannot be opened or read
     */
    explicit StreamBuffer(const char* filename) : filename_(filename) {
        file_ = archive_read_new();
        archive_read_support_filter_all(file_);
        archive_read_support_format_raw(file_);
        archive_read_support_format_empty(file_);
        int r = archive_read_open_filename(file_, filename, buffer_size_);
        if (r != ARCHIVE_OK) {
            std::string msg = std::string(archive_error_string(file_)) + " Error opening file: " + filename;
            archive_read_free(file_);
            throw ParserException(msg);
        }
        struct archive_entry* entry;
        r = archive_read_next_header(file_, &entry);
        if (r == ARCHIVE_EOF) {  // empty input
            buffer_ = new char[buffer_size_]();
            end_of_file_ = true;
            return;
        }
        if (r != ARCHIVE_OK) {
            archive_read_free(file_);
            throw ParserException(std::string("Error reading header: ") + filename);
        }
        buffer_ = new char[buffer_size_];
        try {
            refillBuffer();
        } catch (...) {
            archive_read_free(file_);
            delete[] buffer_;
            throw;
        }
    }

    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    ~StreamBuffer() {
        archive_read_free(file_);
        delete[] buffer_;
    }

    /// Current character, or `EOF` at the end of the file.
    char operator*() const { return eof() ? EOF : buffer_[pos_]; }

    /// True if the end of the file is reached.
    bool eof() const { return pos_ >= end_ && end_of_file_; }

    /**
     * @brief Advances by one character.
     * @param align align the next chunk to whitespace (see class documentation)
     * @pre !eof()
     * @return false if the end of the file is reached
     */
    bool skip(bool align = true) {
        if (++pos_ < end_) return true;
        return refillBuffer(align);
    }

    /**
     * @brief Skips the rest of the current line and all subsequent whitespace.
     *
     * The line may be longer than the internal buffer (e.g. long comments).
     * @pre !eof()
     * @return false if the end of the file is reached
     */
    bool skipLine() {
        while (buffer_[pos_] != '\n' && buffer_[pos_] != '\r') {
            if (!skip(false)) return false;
        }
        // realign manually since skip(false) did not; nothing to align at eof
        if (!end_of_file_) alignBuffer();
        return skipWhitespace();
    }

    /**
     * @brief Skips whitespace, including newlines.
     * @return false if the end of the file is reached
     */
    bool skipWhitespace() {
        if (eof()) return false;  // also covers pos == end == 0 after the last refill
        while (isSpace(buffer_[pos_])) {
            if (!skip()) return false;
        }
        return true;
    }

    /**
     * @brief Skips blanks (spaces and tabs) but not newlines.
     *
     * Allows to detect optional trailing tokens of a line, e.g. an optional
     * field in a header line.
     * @return true if another token follows on the current line
     */
    bool skipBlanks() {
        if (eof()) return false;
        while (buffer_[pos_] == ' ' || buffer_[pos_] == '\t') {
            if (!skip()) return false;
        }
        return !isSpace(buffer_[pos_]) && buffer_[pos_] != '\0';
    }

    /**
     * @brief Skips the given string, which must match exactly at the current position.
     * @throw ParserException if the string does not match
     * @return false if the end of the file is reached right after the string
     */
    bool skipString(const char* str) {
        if (eof()) return false;
        while (*str == buffer_[pos_]) {
            ++str;
            if (*str == '\0') {
                return skip();
            }
            if (!skip()) {
                throw ParserException(std::string(filename_) + ": expected '" + str + "'");
            }
        }
        throw ParserException(std::string(filename_) + ": expected '" + str + "'");
    }

    /**
     * @brief Skips the next number (with optional sign) after leading whitespace.
     * @throw ParserException if the next token is not a number
     * @return false if the end of the file is reached before a number
     */
    bool skipNumber() {
        if (!skipWhitespace()) return false;
        if (buffer_[pos_] == '-' || buffer_[pos_] == '+') {
            if (!skip()) return false;
        }
        if (!isDigit(buffer_[pos_])) {
            if (!skipWhitespace()) return false;
            if (!isDigit(buffer_[pos_])) {
                throw ParserException(std::string(filename_) + ": unexpected character: " + buffer_[pos_]);
            }
        }
        while (isDigit(buffer_[pos_])) {
            if (!skip()) break;
        }
        return true;
    }

    /**
     * @brief Reads the next integer after leading whitespace.
     *
     * Accepts values in the symmetric range [-INT32_MAX, INT32_MAX], which
     * covers all DIMACS literals.
     * @param[out] out the integer read
     * @throw ParserException if the next token is not an integer or out of range
     * @return false if the end of the file is reached before an integer
     */
    bool readInteger(int* out) {
        if (!skipWhitespace()) return false;
        char* str = buffer_ + pos_;
        char* str_end = nullptr;
        errno = 0;
        long number = std::strtol(str, &str_end, 10);
        if (errno != 0) {
            throw ParserException(std::string(filename_) + ": strtol() errno: " + std::to_string(errno));
        }
        if (str_end == str) {
            throw ParserException(std::string(filename_) + ": unexpected character: " + buffer_[pos_]);
        }
        if (std::abs(number) > std::numeric_limits<int32_t>::max()) {
            throw ParserException(std::string(filename_) + ": number out of int32 range");
        }
        pos_ += static_cast<unsigned>(str_end - str);
        *out = static_cast<int>(number);
        return true;
    }

    /**
     * @brief Reads the next unsigned 64-bit integer after leading whitespace.
     * @param[out] out the integer read
     * @throw ParserException if the next token is not a number or out of range
     * @return false if the end of the file is reached before a number
     */
    bool readUInt64(uint64_t* out) {
        if (!skipWhitespace()) return false;
        char* str = buffer_ + pos_;
        char* str_end = nullptr;
        errno = 0;
        unsigned long long number = std::strtoull(str, &str_end, 10);
        if (errno != 0) {
            throw ParserException(std::string(filename_) + ": strtoull() errno: " + std::to_string(errno));
        }
        if (str_end == str) {
            throw ParserException(std::string(filename_) + ": unexpected character: " + buffer_[pos_]);
        }
        static_assert(std::numeric_limits<unsigned long long>::max() <= std::numeric_limits<uint64_t>::max());
        pos_ += static_cast<unsigned>(str_end - str);
        *out = static_cast<uint64_t>(number);
        return true;
    }

    /**
     * @brief Reads the next number as a string of digits after leading whitespace.
     *
     * A leading '-' is kept, a leading '+' is dropped. Useful for numbers of
     * arbitrary length.
     * @param[out] out the digits read
     * @throw ParserException if the next token is not a number
     * @return false if the end of the file is reached before a number
     */
    bool readNumber(std::string* out) {
        std::string result;
        if (!skipWhitespace()) return false;
        if (buffer_[pos_] == '-') {
            result.append(1, buffer_[pos_]);
            if (!skip()) return false;
        } else if (buffer_[pos_] == '+') {
            if (!skip()) return false;
        }
        if (!isDigit(buffer_[pos_])) {
            if (!skipWhitespace()) return false;
            if (!isDigit(buffer_[pos_])) {
                throw ParserException(std::string(filename_) + ": unexpected character: " + buffer_[pos_]);
            }
        }
        while (isDigit(buffer_[pos_])) {
            result.append(1, buffer_[pos_]);
            if (!skip()) break;
        }
        *out = result;
        return true;
    }

    /**
     * @brief Reads the next DIMACS clause (literals terminated by 0).
     *
     * Skips comment lines ('c') and header lines ('p') before the clause. A
     * clause that is not terminated by 0 at the end of the file is accepted.
     * @param[out] out the clause read
     * @throw ParserException on malformed input
     * @return false if the end of the file is reached before a clause
     */
    bool readClause(Cl& out) {
        Cl clause;
        if (!skipWhitespace()) return false;
        while (buffer_[pos_] == 'p' || buffer_[pos_] == 'c') {
            if (!skipLine()) return false;
        }
        int plit;
        while (readInteger(&plit)) {
            if (plit == 0) break;
            clause.push_back(Lit::fromDimacs(plit));
        }
        out = clause;
        return true;
    }

 private:
    static constexpr unsigned buffer_size_ = 16384;

    struct archive* file_ = nullptr;
    char* buffer_ = nullptr;
    unsigned pos_ = 0;           ///< current read position
    unsigned end_ = 0;           ///< one past the last valid position
    bool end_of_file_ = false;   ///< true once the last chunk of the file is in the buffer
    const char* filename_;

    static bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)); }
    static bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

    /**
     * Moves the unread tail of the buffer (beyond end_) to the front and fills
     * the rest from the file.
     * @return false if nothing is left to read
     */
    bool refillBuffer(bool align = true) {
        if (pos_ >= end_ && !end_of_file_) {
            pos_ = 0;
            if (end_ > 0 && end_ < buffer_size_) {
                std::copy(buffer_ + end_, buffer_ + buffer_size_, buffer_);
                end_ = buffer_size_ - end_;
            } else {
                end_ = 0;
            }
            la_ssize_t n = archive_read_data(file_, buffer_ + end_, buffer_size_ - end_);
            if (n < 0) {
                throw ParserException(std::string(filename_) + ": " + archive_error_string(file_));
            }
            end_ += static_cast<unsigned>(n);
            if (end_ < buffer_size_) {
                std::memset(buffer_ + end_, 0, buffer_size_ - end_);
                end_of_file_ = true;
            } else if (align) {
                alignBuffer();
            }
            // the last chunk may line up exactly with eof, leaving pos == end == 0
            return end_ > 0;
        }
        return false;
    }

    /// Shrinks end_ to the last whitespace so that no token crosses the chunk boundary.
    void alignBuffer() {
        while (!isSpace(buffer_[end_ - 1])) {
            end_--;
            if (end_ < 1) {
                throw ParserException("Error reading file: maximum token length exceeded");
            }
        }
    }
};

}  // namespace propan
