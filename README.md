# propan

[![CI](https://github.com/Udopia/propan/actions/workflows/ci.yml/badge.svg)](https://github.com/Udopia/propan/actions/workflows/ci.yml)

Boolean Constraint Propagation Library

## Clause storage

[`ClauseArena`](include/propan/clause_arena.h) stores live clauses as
`[payload/flags][ClauseId][pos][literals...]`. Payload counts all words after
the first header word: ID, search position and literals for live clauses,
unused words for deleted records. Every record advances by `1 + payload`; live
literal count is `payload - ClauseHeader::META`. The position word is scratch
for propagators (circular watch replacement search), initialized to 2.
`ClauseRef` is a direct word offset;
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
invalidates all views, iterators and old references. While a propagator is
attached, size and identity changes must be announced to it (see below).

## Trail and propagation

[`Trail`](include/propan/trail.h) is the assignment stack: values per literal,
reason and level per variable, decision levels, and the propagation queue
`[head, size())`. It knows nothing about clauses. A level is a contiguous
block of decisions (literals without reason, pushed by the client) followed by
their consequences (assigned by a propagator); this shape is asserted:
`newLevel()` requires an empty queue, `push()` on a non-root level requires
that the level has not been propagated yet. After a conflict, backtrack before
pushing again. Out-of-order assignments at lower levels (chronological
backtracking) are kept by `backtrackToRetaining()` and queued again, while
`backtrack()`/`backtrackTo()` assume there are none; `literals(level)` is
therefore the segment opened at a level and `level(var)` is authoritative.

[`Propagator`](include/propan/propagator.h) is the interface of unit
propagation over one arena and one trail: `attach(ref)` (assigns units),
`detach(ref)`, `relocate(relocation)` after `compact()`, and `propagate()`,
which returns the conflicting clause or `UNDEF`. A propagator owns the order of
literals within clauses. [`OccurrencePropagator`](include/propan/occurrence_propagator.h)
is the read-only reference implementation with full occurrence lists.
[`WatchPropagator`](include/propan/watch_propagator.h) implements two watched
literals with blocking literals, tagged binary watches (never visiting the
clause) and a circular replacement search resuming at the clause's position
word. [`BinaryFirstPropagator`](include/propan/binary_first_propagator.h)
derives from it and keeps binary clauses in a flat CSR array that is visited
before the watch lists. The applications select the propagator by name:
`watch` (default), `occ` or `bin`.

## Applications and integration tests

- `slursat <cnf|wcnf> [seed] [k] [propagator]`: SLUR, SLUR(k) (blocks of `k`
  decisions per level, `k > 1`) and SLUR(k) with immediate propagation (`k < 0`).
- `probe <cnf|wcnf> [propagator] [rounds]`: failed literal probing with lifting of
  common implications; on WCNF additionally probes the soft unit literals, the
  block of all of them, and a greedy selection. All reported counters depend
  only on propagation fixpoints and are therefore identical for every
  propagator, which makes them integration tests of the propagators.

[`tests/data/instances`](tests/data/instances) holds small mostly-Horn CNF and
WCNF instances from [benchmark-database.de](https://benchmark-database.de)
(provenance in its README). [`tests/integration.cmake`](tests/integration.cmake)
runs `probe` on each with every propagator against reference counters (label
`integration`, `ctest -L integration`), plus `slursat` on the instances it
solves. [`tests/benchmark.py`](tests/benchmark.py) runs the same instances and
prints a timing table per propagator; `--large` downloads bigger instances
listed in `tests/data/instances/large.txt` into the build directory first.

```cpp
CNFFormula f("formula.cnf");
Trail trail(f.nVars());
WatchPropagator prop(f.clauses(), trail);     // or OccurrencePropagator
if (!prop.attachAll(f.clauses()) || prop.propagate() != ClauseRef()) return UNSAT;
trail.newLevel();
trail.push(Lit(3));                       // decision; push(span) for a block of decisions
ClauseRef conflict = prop.propagate();    // UNDEF if none
trail.backtrack();
```

## Building

Requires CMake 3.20, a C++20 compiler and libarchive (`libarchive-dev`).

```sh
cmake -S . -B build
cmake --build build
./build/apps/slursat <formula.cnf> [seed] [k] [watch|occ|bin]
./build/apps/probe <formula.cnf|formula.wcnf> [watch|occ|bin]
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

`-DPROPAN_WERROR=ON` treats warnings as errors, `-DPROPAN_SANITIZERS=ON` builds
with AddressSanitizer and UndefinedBehaviorSanitizer. Integration tests carry
the label `integration` (`ctest -L integration` / `-LE integration`).

## Continuous integration

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds and tests on
every push to `main` and every pull request: GCC and Clang in Release with
`-Werror` and in Debug with sanitizers on Ubuntu, plus Clang on macOS. Release
jobs also run the integration tests; the GCC release job attaches the
propagator benchmark table to the job summary.
