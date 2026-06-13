# Chess Engine — Improvement Roadmap & Status

This document captures the engine review, what was improved in this pass, how it
was measured, and what remains. Work was time-boxed; items left unfinished are
listed under "Remaining" with enough detail to pick up later.

## How improvements are tested

A headless test harness (`tools/engine_test.cpp`, built with `tools/build_test.sh`)
compiles only the **portable engine core** (`core/`, `search/`) — the Windows GUI
is excluded — and provides:

- `perft <d>`    — move-generation correctness vs known node counts.
- `bench <d> [improved]` — single-thread nodes/second benchmark.
- `selfplay <depth> <games>`     — IMPROVED vs BASELINE, fixed depth, prints W/D/L + Elo.
- `selfplaytime <ms> <games>`    — same, but fixed wall-clock per move (fair-time test).

All experimental changes live behind a per-instance `Search::improved` flag, so a
**single binary plays the new engine against the old one** head-to-head. Baseline =
the original engine; Improved = baseline + the changes below.

Build & run:
```bash
bash tools/build_test.sh
./build_test/engine_test perft 5
./build_test/engine_test selfplay 5 64
```

## Weaknesses found in review

### Correctness / strength bugs
1. **[FIXED] `getBestMove()` / `getBestMoveTimed()` broken on repeated calls.**
   `searchMoves()` raises `stopRequested` at the end (to stop SMP helpers) but
   never cleared it, so the *second* synchronous search and every one after it
   bailed out instantly. The GUI hid this by only using the async path. Fix:
   clear `stopRequested` at the start of `searchMoves()`.
2. **[FIXED] Quiescence ignored being in check.** It "stood pat" on the static
   eval and searched only captures, so when the side to move was in check it
   could miss the only (non-capturing) escape and return a garbage score — a
   direct cause of tactical blunders. Fix: when in check, search *all* evasions,
   never stand pat, and report mate if there are none (with a recursion guard).
3. **[FIXED] Mate scores were stored in the TT without ply adjustment.** Mate
   distances retrieved at a different ply were wrong. Fix: store relative to the
   node (`±ply`), translate back on retrieval.
4. **[FIXED — eval] Mobility was a crude centrality proxy.** Replaced with real
   pseudo-legal mobility (knights/bishops/rooks/queens), one of the highest-value
   evaluation terms.
5. **[FIXED] Zobrist hash omitted castling rights and en-passant state.**
   Positions differing only in castling rights / EP collided in the TT. Added
   16 castling-mask keys + 8 EP-file keys, with incremental updates in
   `hashAfterMove` (king/rook moves, rook captures on corners, double-pawn
   pushes). Validated by a self-check (`HASHCHECK=1`): the incrementally-updated
   hash matched a full recompute on every tree position tested (0 mismatches).
6. **[OPEN] Search-tree repetition only, no game-history repetition.** The engine
   cannot tell that a position already occurred earlier in the *actual game*, so
   it may shuffle into a 3-fold draw from a winning position. Pass the game's
   position-hash history into the search and treat a match as a draw.
7. **[OPEN] Promotion is queen-only** in both move generation and make/unmake.
   Underpromotions (notably knight checks/forks and stalemate-avoidance) are never
   considered. Low impact but real.

### Speed / efficiency
8. **[OPEN — biggest lever] Object-oriented board.** `std::unique_ptr<Piece>`
   squares with virtual `getPossibleMoves` returning `std::vector<Position>` caps
   single-thread throughput at ~0.5M nps. A **bitboard rewrite** (or at least a
   flat `Piece*`/enum array board with attack tables) is the single largest
   available speedup — easily 5–20×, which directly converts to search depth and
   Elo. This is a large, high-risk change and is the main recommended next step.
9. **[OPEN] Full evaluation every node**, including every quiescence stand-pat.
   A pawn-structure hash table and/or lazy evaluation (skip expensive terms when
   the material+PST margin already exceeds the window) would cut eval cost.
10. **[OPEN] Lazy SMP shares the heuristic tables** (`historyTable`, killers,
    countermoves) across threads with no synchronization — technically a data
    race. Make these per-thread (the TT is the intended shared state).

## Done this pass
- Headless Linux build + test harness (perft / bench / self-play, fixed-depth and
  fixed-time), with the new engine gated behind `Search::improved`.
- Fixes #1, #2, #3 and improvement #4 above.
- Verified move-gen correctness (perft 1–5 exact) and measured strength via
  self-play. See `RESULTS.md` for the numbers and the comparison table.

## Measurement note (learned this pass)
Fixed-depth self-play flatters changes that spend more time per depth. The
improved bundle is +94 at fixed depth but only +32 at fixed nodes and *negative*
at a 60 ms fixed-time control — because real-mobility eval (~17% slower nps) and
quiescence evasions cost nodes. Always sanity-check strength changes at fixed
**nodes** (`selfplaynodes`), not just fixed depth, and remember the GUI plays at
3–8 s/move where depth saturates. See `RESULTS.md` → "Three measurement lenses".

## Remaining (recommended order)
1. **Pawn hash + lazy eval (#9)** — now the top lever: cutting per-node eval cost
   turns the existing quality gains into wins at fast time controls too (the
   mobility term is the main culprit behind the fixed-time regression).
2. Bitboard board representation (#8) — largest raw-throughput win.
3. Per-thread heuristic tables for SMP (#10).
4. Underpromotion support (#7).
5. Larger self-play gauntlet for eval/search tuning — at fixed nodes/time.

## Experiments tested and rejected (don't re-try blindly)
- Connected-rooks eval term + killer-LMR: regressed (reverted).
- Killer-move LMR protection alone: neutral over 128 games (reverted).
- Contempt 24 cp: worse than 12 cp (kept 12).
- Game-history repetition detection: neutral on top of contempt (reverted).
- Softer LMR divisor 1.9: −34 at fixed time (2.4× node cost; reverted).
See `RESULTS.md` for the per-experiment W–D–L.
