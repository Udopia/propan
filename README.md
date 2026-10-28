# propan

Boolean Constraint Propagation Libary

## Clause storage

[`ClauseArena`](include/propan/clause_arena.h) stores live clauses as
`[payload/flags][ClauseId][literals...]`. Payload counts all words after the
first header word: ID plus literals for live clauses, unused words for deleted
records. Every record advances by `1 + payload`; live literal count is
`payload - 1`. `ClauseRef` is a direct word offset;
`arena.id(ref)` and `arena.ref(id)` map between offsets and stable metadata IDs.
`arena.refs()` is indexed by ID, with `UNDEF` entries for deleted clauses.
IDs are not reused until `clear()`.

Iteration scans live records and yields views exposing `header()`, `id()`,
`ref()` and `literals()`. Mutable views allow literal and user-flag changes.
Ordinal indexing is removed; formulas accept `ClauseId` or `ClauseRef`.
Soft-clause weights are indexed by `clause.id().index()`.

`shrink(ref, size)` retains a literal prefix and turns the tail into padding;
`erase(ref)` only sets the deletion bit, preserving payload length.
Both preserve other clauses' offsets.
`ClauseFlags` centralizes named `deleted()`/`learned()` accessors and
`setDeleted()`/`setLearned()` setters; six bits remain reserved. Flags are
returned by value: modify them and write them back with `setFlags()`.
Public `static constexpr` masks also support `test(mask)` (any bit set) and
`set(mask, value = true)`, so adding a flag only requires another mask constant.
Arena/view setters reject deletion flags; use `erase()` for bookkeeping.
`compact()` removes dead storage, updates the ID table and returns old/new
reference pairs for live clauses. External offsets must be relocated or rebuilt;
ID-indexed metadata stays valid. Growth can invalidate views; compaction
invalidates all views, iterators and old references. A formula must not be
modified while a `Trail` uses it.

## Building

Requires CMake 3.20, a C++20 compiler and libarchive (`libarchive-dev`).

```sh
cmake -S . -B build
cmake --build build
./build/apps/slursat <formula.cnf> [seed] [k]
```

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest) 2.4.12, vendored in
[`third_party/doctest`](third_party/doctest) with its license. No download is needed.
CTest runs unit tests as one suite and applications end-to-end.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
