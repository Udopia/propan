# Parallel propagation handoff

This is a handoff for continuing the design discussion, not an implemented
parallel propagator or a complete conversation transcript. Check the code and
the current Git state before relying on implementation details below.

## Project and workflow

Propan is a C++20 Boolean constraint propagation library, with sequential
occurrence-list and watched-literal propagators and applications including
slursat. See [README.md](README.md) for the current architecture and build
commands. The user prefers to discuss ambiguous design choices and approve
each step before implementation. The current task is **design only**.

The current Trail has decision levels, values, reasons and a propagation queue;
it is not thread-safe. Each level begins with a contiguous block of decisions
(undefined clause reason) followed by implications. Existing solver support
includes out-of-order lower-level assignments and retaining them on
backtracking; the parallel design must explicitly address or restrict this.

The current Lit encoding is a signed DIMACS literal, with `UNDEF_LIT` equal to
zero. Earlier conversation used a different encoding and is not authoritative.
ClauseArena uses in-arena headers and metadata; read the current README and
source rather than relying on early design discussions.

## Parallel propagation: tentative contracts

- Use a persistent thread pool. Workers poll only during a `propagate()` call;
  they sleep outside it.
- Trail storage has fixed capacity during propagation, sufficient for its
  variables, and can grow when variables are added between propagation
  phases. Each variable can have at most one committed assignment at a time.
- Workers claim unique positions with a shared atomic `head`. A `size` counter
  can include reserved but not yet published positions. Each reserved slot
  has an atomic publication state or sentinel; a worker claiming it waits for
  publication. Slot publication must make assignment metadata visible.
- A writer claims a variable and prepares its value, reason and level before
  publishing its literal in the trail. Opposite simultaneous implications
  must report a conflict, and duplicate implications must not add duplicates.
  Exactly how other workers access an assignment while publication is in
  progress is unresolved.
- A worker can claim the last position while still processing it. Consequently
  `head == size` alone is not a fixpoint test while workers are active.
  The agreed direction is a worker barrier: finish claimed work and
  publications, rendezvous, then check `head == size`; continue if there is
  more work. No completion counter is needed if the barrier and producer
  lifecycle satisfy this contract. Conflict and reserved-slot cancellation
  still need a precise protocol.
- Within a level, decisions precede implications. Implications may appear in
  any order, including after implications depending on them. Future conflict
  analysis must not assume reverse trail order is a reason-graph order.
- Watch lists use a per-literal atomic state: 0 free, 1 append, 2 propagate.
  State 2 is entered only for the false complement of a published trail
  literal. An appender seeing 2 skips that candidate as false; after acquiring
  state 1, it rechecks the atomic value before appending. Propagation and
  appending must synchronize through this same state. Backtracking is not
  concurrent with propagation.
- Proposed clause lock: atomically replace the first watched literal with
  `UNDEF_LIT` via CAS, and restore an appropriate first watch on release.
  Real clauses must not contain the sentinel. Waiting workers must reread the
  watches after acquiring the lock; release it on all paths. Unit clauses need
  separate handling. All concurrent accesses to the lock slot must comply
  with the C++ memory model, including `atomic_ref` requirements if used.
- Prevent deadlock with an explicit lock/retry protocol. In particular,
  appenders holding state 1 must not wait for a clause lock. Specify how a
  propagator holding state 2 interacts with a clause lock and how a lock owner
  handles busy destination lists.
- No backtracking, resizing, clause relocation or other incompatible mutation
  while workers are active. Define attach/detach and conflict cleanup too.

These are a design direction, not a proof of correctness. Define
linearization points, atomic memory orders, progress under stalled workers,
barrier behavior and reason-graph semantics before implementation.

## Prepared vector experiments

The user's reference implementations and stress harness are locally in
`propal/slur/lockfree/`, which is ignored by Git and **will not appear in a
fresh clone**. Preserve or separately copy them before moving to another
machine. The user reports overcommit tests and favorable comparisons with
Intel TBB; reproduce and validate these before drawing conclusions.

Review candidate implementations for their C++ memory-model and publication
guarantees before reuse. The existing harness is useful prior art but should
be adapted to assert exact-once delivery and distinguish preconstructed
elements from unpublished entries. Preserve the MIT copyright/license when
importing code. Consider an optional oneTBB backend with the same explicitly
defined publication contract; TBB must remain optional.

## Next steps

1. Agree on a precise Trail and assignment publication contract, including
   slot holes, reasons, cancellation and barrier-based termination.
2. Specify the watch/assignment/clause locking protocol and prove that watch
   moves cannot be missed or deadlock.
3. Evaluate the prepared fixed-capacity approaches and the optional TBB
   backend; retain the sequential Trail's intended behavior and API.
4. Implement incrementally with approval. Add focused concurrent and
   differential tests, including overcommit, stalled writers, opposite
   implications, fixpoint and conflict; use ThreadSanitizer and stress tests.
5. Benchmark equivalent workloads after correctness is established.
