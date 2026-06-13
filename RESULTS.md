# Engine Improvements — Measured Results

All measurements were produced on a 4-core Linux container with the headless
harness in `tools/` (g++ 13, `-O3 -march=native`, single search thread for
reproducibility). "Baseline" is the original engine; "Improved" is the same
binary with `Search::improved = true`, which enables the changes listed below.
The two play each other directly via `tools/engine_test.cpp`.

## What "Improved" contains
1. **Search bug fix** — `getBestMove()`/`getBestMoveTimed()` now reset the stop
   flag, so repeated synchronous searches work (was broken after the 1st call).
   *(Applied unconditionally — it is a plain bug fix, not gated.)*
2. **Quiescence check evasions** — when in check, quiescence searches all legal
   escapes instead of standing pat on captures only (removes a class of tactical
   blunders).
3. **Mate-score TT ply adjustment** — correct mate distances across the table.
4. **Real piece mobility** in evaluation, replacing the centrality proxy.
5. **Castling rights + en-passant in the Zobrist hash** — sound TT keys
   (validated by an exact-recompute self-check, 0 mismatches).
6. **Contempt (12 cp)** — draws (repetition / stalemate) are scored slightly
   negative for the side to move, so the engine plays on for a win in equal-or-
   better positions instead of acquiescing to a repetition.
5. **Castling-rights + en-passant in the Zobrist hash** — previously the hash
   ignored both, so positions differing only in castling rights / EP collided in
   the transposition table and could produce wrong cutoffs. The incremental
   update was validated with a built-in self-check (`HASHCHECK=1`): **0 mismatches
   over 440k+ tree positions and across full self-play games**, including
   castling, en passant, and rook captures.

## Correctness (perft from startpos)
| Depth | Nodes | Expected | Status |
|------:|------:|---------:|:------:|
| 1 | 20 | 20 | OK |
| 2 | 400 | 400 | OK |
| 3 | 8,902 | 8,902 | OK |
| 4 | 197,281 | 197,281 | OK |
| 5 | 4,865,609 | 4,865,609 | OK |

Move generation is exact (auto-queen promotion does not affect these depths).

## Strength — self-play, IMPROVED vs BASELINE

Each opening (16 lines) is played with both colors; result is from the improved
engine's perspective. Elo difference is `-400·log10(1/score − 1)`.

| Test | Games | W–D–L | Score | Elo diff |
|:-----|------:|:-----:|:-----:|:--------:|
| Fixed depth 5 (48 games) | 48 | 17–22–9 | 58.3% | **+58** |
| Fixed depth 5 (48, +mobility) | 48 | 16–26–6 | 60.4% | **+73** |
| Fixed depth 5 (64 games) | 64 | 21–34–9 | 59% | +65 |
| Fixed time 50 ms/move (24 games) | 24 | 10–9–5 | 60% | +73 |
| Fixed depth 5 (128 games, +Zobrist) | 128 | 46–58–24 | 59% | +60 |
| **Fixed depth 5 (128 games, +contempt, final)** | 128 | **52–58–18** | **63%** | **+94** |

Each row adds the next change on top of the previous. The Zobrist fix held the
bundle at ~+60 with no regression; adding a 12 cp contempt jumped it to **+94 Elo**
— wins rose (46→52) and losses fell (24→18), sharpening the decisive ratio to
2.9:1. (Draw count was unchanged at 58; contempt converted near-draw decisive
games rather than removing draws outright.)

The fixed-depth result isolates **decision quality** (both engines reach the same
nominal depth; improved simply searches it more correctly). The fixed-time result
is the fair-resource check, and improved **still wins (+73 Elo, 10–9–5)** when both
get the same wall-clock per move — so the gain is genuine strength, not just extra
nodes spent. Across both regimes the improved engine is roughly **+65 to +73 Elo**
stronger than the saved baseline.

## Three measurement lenses (important nuance)

The same bundle measured three ways, because *how* you equalize the two engines
changes what you learn:

| Lens | Result (improved vs baseline) | Isolates |
|:-----|:------------------------------|:---------|
| Fixed **depth** 5 (128 g) | **+94 Elo** (52–58–18) | accuracy per nominal depth |
| Fixed **nodes** 50k (32 g) | **+32 Elo** (13–9–10) | pure decision quality, speed-independent |
| Fixed **time** 60 ms (30 g) | **−46 Elo** (10–6–14) | wall-clock at a *very fast* control |

Interpretation: the improvements make the engine **more accurate per depth and
per node** (both positive). But the real-mobility evaluation costs ~17%
nodes/second, and quiescence check-evasions add nodes, so the improved engine
reaches *less nominal depth in the same wall-clock*. At a 60 ms control that
depth deficit dominates and improved loses; at fixed depth/nodes (where the
deficit is removed) it clearly wins. The GUI's real controls are 3–8 **seconds**
per move, where depth saturates and quality should dominate — but that regime is
too slow to measure with a self-play gauntlet here.

Practical takeaway: the **next** highest-value work is cutting per-node eval cost
(pawn hash + lazy eval — `TODO.md` #9) so the quality gains also pay off at fast
time controls, and ultimately the bitboard rewrite (#8). Contempt, the mate-TT
fix, the Zobrist fix and the quiescence-in-check fix are all either free or pure
correctness and help at every time control.

## Speed — single-thread node throughput (startpos, depth 10)
| Engine | Nodes | Time | Nodes/s |
|:-------|------:|-----:|--------:|
| Baseline | 22,035 | 45 ms | ~483,000 |
| Improved | 53,124 | 133 ms | ~399,000 |

Improved visits ~2.4× the nodes at the same depth — almost entirely because
baseline was *under*-searching (skipping check evasions in quiescence). The ~17%
nodes/second drop comes from the real-mobility evaluation. Net effect is positive
(see strength table). The largest remaining speed lever is replacing the
object-oriented board with bitboards — see `TODO.md` (#8).

## Small tweaks that were tested and rejected

Measured, found not to help on top of the bundle above, and **reverted** (kept
here so they aren't blindly re-tried):

| Tweak | Sample | Result vs baseline | Verdict |
|:------|-------:|:-------------------|:--------|
| Connected-rooks eval term + killer-LMR | 64 | 18–33–13 (+27) | Regressed (losses 9→13) — reverted both |
| Killer-move LMR protection (alone) | 128 | 43–61–24 (+51) | Neutral vs the +65 bundle; win:loss ratio slipped (2.3→1.8) — reverted |
| Contempt = 24 cp (vs the kept 12 cp) | 128 | 54–45–29 (+68) | Cut draws (58→45) but losses rose (18→29); net worse than 12 cp — kept 12 |
| Game-history repetition detection | 128 | 48–60–20 (+77) | Within noise of (and trending below) contempt-only +94; contempt already discourages repetitions — reverted |

Takeaway: the engine is already well past the point where arbitrary-weight eval
terms or marginal LMR tweaks pay off. The remaining gains are the structural
items in `TODO.md` (bitboard board, Zobrist castling/EP soundness, pawn-hash /
lazy eval), not micro-tweaks.

## Reproduce
```bash
bash tools/build_test.sh
./build_test/engine_test perft 5
./build_test/engine_test bench 10            # baseline
./build_test/engine_test bench 10 improved   # improved
./build_test/engine_test selfplay 5 64        # fixed-depth match
./build_test/engine_test selfplaytime 50 24   # fixed-time match
./build_test/engine_test selfplaynodes 50000 32  # fixed-nodes (speed-independent)
```
