#include "search/Search.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <cmath>
#include <cstring>
#include <cstdlib>

// IMPROVED hash self-check (enable with env var HASHCHECK=1): after every legal
// move in the tree, verify the incrementally-updated hash matches a full
// recompute of the resulting position. Used to validate castling/EP hashing.
static bool s_hashCheck = (std::getenv("HASHCHECK") != nullptr);
static std::atomic<unsigned long long> s_hashChecked{0};
static std::atomic<unsigned long long> s_hashMismatch{0};

// ============================================================
// ZOBRIST HASHING IMPLEMENTATION
// ============================================================

ZobristHash::ZobristHash() {
    std::mt19937_64 rng(0xDEADBEEF12345678ULL);
    
    for (int pt = 0; pt < 6; pt++)
        for (int c = 0; c < 2; c++)
            for (int sq = 0; sq < 64; sq++)
                pieceKeys[pt][c][sq] = rng();

    sideToMoveKey = rng();
    for (int i = 0; i < 16; i++) castlingKeys[i] = rng();
    for (int i = 0; i < 8; i++)  epFileKeys[i]   = rng();
}

uint64_t ZobristHash::computeHash(const Board* board) const {
    uint64_t hash = 0;
    
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = board->getPiece(row, col);
            if (piece) {
                int pt = static_cast<int>(piece->getType());
                int c = (piece->getColor() == PieceColor::WHITE) ? 0 : 1;
                int sq = row * 8 + col;
                hash ^= pieceKeys[pt][c][sq];
            }
        }
    }
    
    if (board->getCurrentTurn() == PieceColor::BLACK)
        hash ^= sideToMoveKey;
    
    return hash;
}

// ============================================================
// TRANSPOSITION TABLE IMPLEMENTATION
// ============================================================

TranspositionTable::TranspositionTable(size_t sizeInMB) : hits(0), probes(0), currentGeneration(0) {
    // Round down to power of 2 for bitmask indexing (avoids expensive modulo)
    size_t rawEntries = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);
    numEntries = 1;
    while (numEntries * 2 <= rawEntries) numEntries *= 2;
    mask = numEntries - 1;
    table.resize(numEntries);
    size_t actualMB = (numEntries * sizeof(TTEntry)) / (1024 * 1024);
    std::cout << "[TT] " << actualMB << "MB — " << numEntries << " entries (power-of-2)\n";
}

void TranspositionTable::store(uint64_t hash, int score, int depth, TTFlag flag,
                                Position bestFrom, Position bestTo) {
    size_t index = hash & mask;
    TTEntry& entry = table[index];
    uint32_t key = (uint32_t)(hash >> 32);

    // Replace if: empty, stale generation, same position, or current search deeper
    bool stale = (entry.getGeneration() != currentGeneration);
    if (entry.hashKey == 0 || stale || entry.hashKey == key || depth >= (int)entry.depth) {
        entry.hashKey    = key;
        entry.score      = score;
        entry.depth      = (uint8_t)std::min(depth, 255);
        entry.setFlagAndGen(flag, currentGeneration);
        entry.bestFromSq = TTEntry::posToSq(bestFrom);
        entry.bestToSq   = TTEntry::posToSq(bestTo);
    }
}

TTEntry* TranspositionTable::probe(uint64_t hash) {
    probes++;
    size_t index = hash & mask;
    TTEntry& entry = table[index];
    uint32_t key = (uint32_t)(hash >> 32);
    
    if (entry.hashKey == key) {
        hits++;
        return &entry;
    }
    return nullptr;
}

void TranspositionTable::clear() {
    std::memset(table.data(), 0, table.size() * sizeof(TTEntry));
    hits = 0;
    probes = 0;
}

// ============================================================
// SEARCH ENGINE IMPLEMENTATION
// ============================================================

// Pruning constants
static constexpr int NULL_MOVE_REDUCTION = 2;       // R = 2 (reduce depth by 2 for null move)
static constexpr int FUTILITY_MARGIN_D1 = 200;      // Futility margin at depth 1
static constexpr int FUTILITY_MARGIN_D2 = 500;      // Extended futility at depth 2
static constexpr int RAZORING_MARGIN_D1 = 300;      // Razoring margin at depth 1
static constexpr int RAZORING_MARGIN_D2 = 600;      // Razoring margin at depth 2
static constexpr int LMR_FULL_DEPTH_MOVES = 3;      // Search first N moves at full depth
static constexpr int LMR_REDUCTION_LIMIT = 2;       // Only reduce at depth >= 2
static constexpr int ASPIRATION_WINDOW = 50;         // Initial aspiration window size
static constexpr int IID_DEPTH_THRESHOLD = 4;       // IID when depth >= this and no TT move
static constexpr int IID_REDUCTION = 2;             // Reduce by this for IID

// Late Move Pruning thresholds (moves to search at depth 1, 2, 3, 4, 5)
static constexpr int LMP_THRESHOLD[6] = { 0, 5, 10, 16, 22, 28 };

// Evaluation bonuses
static constexpr int BISHOP_PAIR_BONUS = 50;
static constexpr int ROOK_OPEN_FILE_BONUS = 25;
static constexpr int ROOK_SEMI_OPEN_BONUS = 15;
static constexpr int PASSED_PAWN_BONUS[8] = { 0, 10, 20, 40, 60, 100, 150, 0 }; // by rank advancement
static constexpr int DOUBLED_PAWN_PENALTY = -15;
static constexpr int ISOLATED_PAWN_PENALTY = -20;
static constexpr int MOBILITY_WEIGHT = 3;           // centipawns per legal move
static constexpr int KING_SHIELD_BONUS = 10;         // per pawn in front of king
static constexpr int KNIGHT_OUTPOST_BONUS = 25;      // knight on outpost square
static constexpr int TEMPO_BONUS = 10;               // bonus for side to move
static constexpr int CONTEMPT = 12;                  // IMPROVED: draw aversion (cp)
static constexpr int KING_ATTACK_WEIGHT = 4;         // per attacker in king zone
static constexpr int CONNECTED_ROOKS_BONUS = 15;     // rooks on same rank/file with nothing between

// Game phase weights (total = 24 for starting position)
static constexpr int PHASE_KNIGHT = 1;
static constexpr int PHASE_BISHOP = 1;
static constexpr int PHASE_ROOK = 2;
static constexpr int PHASE_QUEEN = 4;
static constexpr int TOTAL_PHASE = 24; // 4*1 + 4*1 + 4*2 + 2*4

Search::Search(Board* boardPtr, int ttSizeMB)
    : searching(false), stopRequested(false), board(boardPtr),
      nodesSearched(0), qNodesSearched(0), tt(ttSizeMB), timeLimitMs(0),
      numThreads(std::max(1, (int)std::thread::hardware_concurrency())) {
    clearHeuristics();
    // Cap SMP threads to MAX_THREADS
    if (numThreads > MAX_THREADS) numThreads = MAX_THREADS;
    std::cout << "[Search] Initialized with " << numThreads << " threads\n";
}

Search::~Search() {
    stopSearch();
    if (searchThread.joinable()) {
        searchThread.join();
    }
}

void Search::clearHeuristics() {
    memset(historyTable, 0, sizeof(historyTable));
    for (int i = 0; i < MAX_PLY; i++) {
        killerFrom[i][0] = Position(-1, -1);
        killerFrom[i][1] = Position(-1, -1);
        killerTo[i][0] = Position(-1, -1);
        killerTo[i][1] = Position(-1, -1);
    }
    // Clear countermove table
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            countermoveFrom[i][j] = Position(-1, -1);
            countermoveTo[i][j] = Position(-1, -1);
        }
    }
}

void Search::storeCountermove(Position prevFrom, Position prevTo, Position from, Position to) {
    if (!prevFrom.isValid() || !prevTo.isValid()) return;
    int prevFromSq = prevFrom.row * 8 + prevFrom.col;
    int prevToSq = prevTo.row * 8 + prevTo.col;
    countermoveFrom[prevFromSq][prevToSq] = from;
    countermoveTo[prevFromSq][prevToSq] = to;
}

// ============================================================
// TIME MANAGEMENT
// ============================================================
bool Search::checkTimeLimit() {
    if (timeLimitMs <= 0) return false;
    // Only check every 4096 nodes to avoid clock overhead
    uint64_t total = nodesSearched.load(std::memory_order_relaxed) + 
                     qNodesSearched.load(std::memory_order_relaxed);
    if ((total & 4095) != 0) return false;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStartTime).count();
    if (elapsed >= timeLimitMs) {
        stopRequested.store(true);
        return true;
    }
    return false;
}

// ============================================================
// INCREMENTAL ZOBRIST HASH UPDATE
// Computes the hash of the position AFTER making a move, without
// actually making the move. Much faster than full recompute.
// Note: This must match exactly what makeMove does to the board.
// ============================================================
// ============================================================
// CASTLING-RIGHTS + EN-PASSANT AWARE HASHING (IMPROVED)
// ============================================================
// 4-bit castling-rights mask: bit0=WK(O-O), bit1=WQ(O-O-O), bit2=BK, bit3=BQ.
// A right is present only when both king and the relevant rook are on their
// home squares and neither has moved (matches toFEN()).
int Search::boardRights(const Board* b) {
    int mask = 0;
    Piece* wk = b->getPiece(7, 4);
    if (wk && wk->getType() == PieceType::KING && !wk->hasMovedBefore()) {
        Piece* r = b->getPiece(7, 7);
        if (r && r->getType() == PieceType::ROOK && !r->hasMovedBefore()) mask |= 1;
        r = b->getPiece(7, 0);
        if (r && r->getType() == PieceType::ROOK && !r->hasMovedBefore()) mask |= 2;
    }
    Piece* bk = b->getPiece(0, 4);
    if (bk && bk->getType() == PieceType::KING && !bk->hasMovedBefore()) {
        Piece* r = b->getPiece(0, 7);
        if (r && r->getType() == PieceType::ROOK && !r->hasMovedBefore()) mask |= 4;
        r = b->getPiece(0, 0);
        if (r && r->getType() == PieceType::ROOK && !r->hasMovedBefore()) mask |= 8;
    }
    return mask;
}

uint64_t Search::castlingEpContribution(const Board* b) const {
    uint64_t h = zobrist.getCastlingKey(boardRights(b));
    Position ep = b->getEnPassantTarget();
    if (ep.isValid()) h ^= zobrist.getEpFileKey(ep.col);
    return h;
}

uint64_t Search::rootHash(const Board* b) const {
    uint64_t h = zobrist.computeHash(b);
    if (improved) h ^= castlingEpContribution(b);
    return h;
}

uint64_t Search::hashAfterMove(uint64_t hash, Board* pos, Position from, Position to) const {
    Piece* piece = pos->getPiece(from);
    if (!piece) return hash ^ zobrist.getSideKey(); // fallback
    
    int pt = static_cast<int>(piece->getType());
    int c = (piece->getColor() == PieceColor::WHITE) ? 0 : 1;
    int fromSq = from.row * 8 + from.col;
    int toSq = to.row * 8 + to.col;
    
    // Remove piece from origin
    hash ^= zobrist.getPieceKey(pt, c, fromSq);
    
    // Handle capture at destination
    Piece* captured = pos->getPiece(to);
    if (captured) {
        int cpt = static_cast<int>(captured->getType());
        int cc = (captured->getColor() == PieceColor::WHITE) ? 0 : 1;
        hash ^= zobrist.getPieceKey(cpt, cc, toSq);
    }
    
    // Handle en passant capture
    Position ep = pos->getEnPassantTarget();
    if (piece->getType() == PieceType::PAWN && to == ep && ep.row >= 0) {
        int captureRow = (piece->getColor() == PieceColor::WHITE) ? to.row + 1 : to.row - 1;
        Piece* epPawn = pos->getPiece(captureRow, to.col);
        if (epPawn) {
            int epSq = captureRow * 8 + to.col;
            int epc = (epPawn->getColor() == PieceColor::WHITE) ? 0 : 1;
            hash ^= zobrist.getPieceKey(static_cast<int>(PieceType::PAWN), epc, epSq);
        }
    }
    
    // Handle promotion (pawn -> queen)
    if (piece->getType() == PieceType::PAWN &&
        ((piece->getColor() == PieceColor::WHITE && to.row == 0) ||
         (piece->getColor() == PieceColor::BLACK && to.row == 7))) {
        // Place queen instead of pawn at destination
        hash ^= zobrist.getPieceKey(static_cast<int>(PieceType::QUEEN), c, toSq);
    } else {
        // Place piece at destination
        hash ^= zobrist.getPieceKey(pt, c, toSq);
    }
    
    // Handle castling rook
    if (piece->getType() == PieceType::KING && std::abs(to.col - from.col) == 2) {
        int rookCol = (to.col > from.col) ? 7 : 0;
        int newRookCol = (to.col > from.col) ? 5 : 3;
        int rookFromSq = from.row * 8 + rookCol;
        int rookToSq = from.row * 8 + newRookCol;
        int rookPt = static_cast<int>(PieceType::ROOK);
        hash ^= zobrist.getPieceKey(rookPt, c, rookFromSq);
        hash ^= zobrist.getPieceKey(rookPt, c, rookToSq);
    }
    
    // IMPROVED: update castling-rights + en-passant component incrementally.
    // The parent hash already carries the old castling/EP keys; XOR out the old
    // component and XOR in the new one derived from the move.
    if (improved) {
        int oldRights = boardRights(pos);
        Position oldEp = pos->getEnPassantTarget();
        uint64_t oldContrib = zobrist.getCastlingKey(oldRights);
        if (oldEp.isValid()) oldContrib ^= zobrist.getEpFileKey(oldEp.col);

        // Rights lost by this move (clearing an already-absent bit is harmless).
        int clear = 0;
        if (piece->getType() == PieceType::KING) {
            if (piece->getColor() == PieceColor::WHITE) clear |= (1 | 2);
            else clear |= (4 | 8);
        }
        // A rook leaving — or anything capturing on — a corner kills that right.
        auto cornerBit = [](Position p) -> int {
            if (p.row == 7 && p.col == 7) return 1;
            if (p.row == 7 && p.col == 0) return 2;
            if (p.row == 0 && p.col == 7) return 4;
            if (p.row == 0 && p.col == 0) return 8;
            return 0;
        };
        clear |= cornerBit(from);
        clear |= cornerBit(to);
        int newRights = oldRights & ~clear;

        // New en-passant square: only a pawn double-push creates one.
        bool newEpValid = (piece->getType() == PieceType::PAWN &&
                           std::abs(to.row - from.row) == 2);
        uint64_t newContrib = zobrist.getCastlingKey(newRights);
        if (newEpValid) newContrib ^= zobrist.getEpFileKey(from.col);

        hash ^= oldContrib ^ newContrib;
    }

    // Flip side to move
    hash ^= zobrist.getSideKey();

    return hash;
}

int Search::pieceValue(PieceType t) {
    switch (t) {
        case PieceType::PAWN:   return 100;
        case PieceType::KNIGHT: return 320;
        case PieceType::BISHOP: return 330;
        case PieceType::ROOK:   return 500;
        case PieceType::QUEEN:  return 900;
        case PieceType::KING:   return 20000;
        default: return 0;
    }
}

// ============================================================
// STATIC EXCHANGE EVALUATION (SEE)
// ============================================================

// Piece values for SEE, indexed by PieceType enum order:
// PAWN=0, ROOK=1, KNIGHT=2, BISHOP=3, QUEEN=4, KING=5, NONE=6
static constexpr int SEE_VALUES[] = { 100, 500, 320, 330, 900, 20000, 0 };

// Find the least valuable attacker of 'color' to 'sq', skipping removed squares
// (which enables x-ray discovery through captured/removed pieces).
// Returns the attacker position and sets outType; returns (-1,-1) if none found.
static Position findLVA(Board* pos, Position sq, PieceColor color,
                        bool removed[8][8], PieceType& outType) {
    int r = sq.row, c = sq.col;
    int bestValue = 99999;
    Position bestSq(-1, -1);
    PieceType bestType = PieceType::NONE;

    // 1. Pawns (100) — cheapest, return immediately if found
    int pawnRow = (color == PieceColor::WHITE) ? r + 1 : r - 1;
    if (pawnRow >= 0 && pawnRow < 8) {
        for (int dc : {-1, 1}) {
            int nc = c + dc;
            if (nc >= 0 && nc < 8 && !removed[pawnRow][nc]) {
                Piece* p = pos->getPiece(pawnRow, nc);
                if (p && p->getColor() == color && p->getType() == PieceType::PAWN) {
                    outType = PieceType::PAWN;
                    return Position(pawnRow, nc);
                }
            }
        }
    }

    // 2. Knights (320)
    static constexpr int km[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (const auto& k : km) {
        int nr = r + k[0], nc = c + k[1];
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && !removed[nr][nc]) {
            Piece* p = pos->getPiece(nr, nc);
            if (p && p->getColor() == color && p->getType() == PieceType::KNIGHT) {
                bestValue = 320; bestSq = Position(nr, nc); bestType = PieceType::KNIGHT;
                break; // all knights are same value
            }
        }
    }
    if (bestValue <= 320) { outType = bestType; return bestSq; }

    // 3. Diagonal rays — bishop (330) or queen (900)
    static constexpr int diags[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (const auto& d : diags) {
        for (int s = 1; s < 8; s++) {
            int nr = r + s * d[0], nc = c + s * d[1];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
            if (removed[nr][nc]) continue; // x-ray through removed piece
            Piece* p = pos->getPiece(nr, nc);
            if (p) {
                if (p->getColor() == color) {
                    if (p->getType() == PieceType::BISHOP && 330 < bestValue) {
                        bestValue = 330; bestSq = Position(nr, nc); bestType = PieceType::BISHOP;
                    } else if (p->getType() == PieceType::QUEEN && 900 < bestValue) {
                        bestValue = 900; bestSq = Position(nr, nc); bestType = PieceType::QUEEN;
                    }
                }
                break; // blocked by any non-removed piece
            }
        }
    }
    if (bestValue <= 330) { outType = bestType; return bestSq; }

    // 4. Straight rays — rook (500) or queen (900)
    static constexpr int straights[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (const auto& d : straights) {
        for (int s = 1; s < 8; s++) {
            int nr = r + s * d[0], nc = c + s * d[1];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
            if (removed[nr][nc]) continue;
            Piece* p = pos->getPiece(nr, nc);
            if (p) {
                if (p->getColor() == color) {
                    if (p->getType() == PieceType::ROOK && 500 < bestValue) {
                        bestValue = 500; bestSq = Position(nr, nc); bestType = PieceType::ROOK;
                    } else if (p->getType() == PieceType::QUEEN && 900 < bestValue) {
                        bestValue = 900; bestSq = Position(nr, nc); bestType = PieceType::QUEEN;
                    }
                }
                break;
            }
        }
    }
    if (bestValue <= 900) { outType = bestType; return bestSq; }

    // 5. King (20000)
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && !removed[nr][nc]) {
                Piece* p = pos->getPiece(nr, nc);
                if (p && p->getColor() == color && p->getType() == PieceType::KING) {
                    outType = PieceType::KING;
                    return Position(nr, nc);
                }
            }
        }
    }

    outType = PieceType::NONE;
    return Position(-1, -1);
}

int Search::see(Board* pos, Position from, Position to) {
    Piece* target = pos->getPiece(to);
    Piece* attacker = pos->getPiece(from);
    if (!attacker) return 0;

    int gain[32];
    bool removed[8][8] = {};
    int d = 0;

    // Initial capture
    gain[0] = target ? SEE_VALUES[(int)target->getType()] : 0;
    PieceType aPiece = attacker->getType();
    removed[from.row][from.col] = true;

    PieceColor sideToMove = (attacker->getColor() == PieceColor::WHITE)
                            ? PieceColor::BLACK : PieceColor::WHITE;

    while (d < 31) {
        d++;
        gain[d] = SEE_VALUES[(int)aPiece] - gain[d - 1];

        // Find current side's least valuable attacker
        PieceType foundType;
        Position aSq = findLVA(pos, to, sideToMove, removed, foundType);
        if (!aSq.isValid()) break; // no more attackers

        aPiece = foundType;
        removed[aSq.row][aSq.col] = true;

        sideToMove = (sideToMove == PieceColor::WHITE)
                     ? PieceColor::BLACK : PieceColor::WHITE;
    }

    // Negamax: each side can choose to stand pat or continue capturing
    while (--d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    }

    return gain[0];
}

// ============================================================
// GAME PHASE COMPUTATION (for tapered eval)
// ============================================================
int Search::computeGamePhase(Board* pos) {
    int phase = TOTAL_PHASE;
    
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (!piece) continue;
            switch (piece->getType()) {
                case PieceType::KNIGHT: phase -= PHASE_KNIGHT; break;
                case PieceType::BISHOP: phase -= PHASE_BISHOP; break;
                case PieceType::ROOK:   phase -= PHASE_ROOK;   break;
                case PieceType::QUEEN:  phase -= PHASE_QUEEN;  break;
                default: break;
            }
        }
    }
    
    // phase = 0 means all pieces on board (opening), = 24 means endgame
    // Normalize: 0 = endgame, 256 = opening
    return (phase * 256 + TOTAL_PHASE / 2) / TOTAL_PHASE;
}

// ============================================================
// PAWN STRUCTURE EVALUATION
// ============================================================
int Search::evaluatePawnStructure(Board* pos, PieceColor color) {
    int score = 0;
    int pawnCols[8] = {0}; // Count pawns per file
    
    int direction = (color == PieceColor::WHITE) ? -1 : 1;
    int startRank = (color == PieceColor::WHITE) ? 6 : 1;
    
    // Count pawns per file
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (piece && piece->getType() == PieceType::PAWN && piece->getColor() == color) {
                pawnCols[col]++;
                
                // Passed pawn check: no enemy pawns blocking or on adjacent files ahead
                bool passed = true;
                PieceColor enemy = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
                int checkStart = (color == PieceColor::WHITE) ? 0 : row + 1;
                int checkEnd = (color == PieceColor::WHITE) ? row : 8;
                
                for (int r = checkStart; r < checkEnd && passed; r++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        int c = col + dc;
                        if (c < 0 || c > 7) continue;
                        Piece* blocker = pos->getPiece(r, c);
                        if (blocker && blocker->getType() == PieceType::PAWN && blocker->getColor() == enemy) {
                            passed = false;
                            break;
                        }
                    }
                }
                
                if (passed) {
                    int advancement = (color == PieceColor::WHITE) ? (7 - row) : row;
                    score += PASSED_PAWN_BONUS[advancement];
                }
            }
        }
    }
    
    // Doubled and isolated pawns
    for (int col = 0; col < 8; col++) {
        if (pawnCols[col] > 1) {
            score += DOUBLED_PAWN_PENALTY * (pawnCols[col] - 1);
        }
        if (pawnCols[col] > 0) {
            bool hasNeighbor = (col > 0 && pawnCols[col - 1] > 0) || 
                               (col < 7 && pawnCols[col + 1] > 0);
            if (!hasNeighbor) {
                score += ISOLATED_PAWN_PENALTY * pawnCols[col];
            }
        }
    }
    
    return score;
}

// ============================================================
// KING SAFETY EVALUATION
// ============================================================
int Search::evaluateKingSafety(Board* pos, PieceColor color) {
    int score = 0;
    Position kingPos = pos->getKingPosition(color);
    
    if (!kingPos.isValid()) return 0;
    
    // Pawn shield: check pawns in front of king
    int direction = (color == PieceColor::WHITE) ? -1 : 1;
    
    for (int dc = -1; dc <= 1; dc++) {
        int shieldCol = kingPos.col + dc;
        if (shieldCol < 0 || shieldCol > 7) continue;
        
        // Check 1-2 ranks in front of king
        for (int dist = 1; dist <= 2; dist++) {
            int shieldRow = kingPos.row + direction * dist;
            if (shieldRow < 0 || shieldRow > 7) continue;
            
            Piece* piece = pos->getPiece(shieldRow, shieldCol);
            if (piece && piece->getType() == PieceType::PAWN && piece->getColor() == color) {
                score += KING_SHIELD_BONUS;
                break; // Only count closest pawn per file
            }
        }
    }
    
    return score;
}

// ============================================================
// MOBILITY EVALUATION (lightweight: piece centrality proxy)
// Avoids expensive move generation — uses piece position only.
// Central pieces have more mobility; PST already covers most of this
// but a small bonus for piece activity helps.
// ============================================================
int Search::evaluateMobility(Board* pos, PieceColor color) {
    int mobility = 0;
    // Central files/ranks get bonus (0-indexed: 0..7)
    // Bonus for being in the center: max at (3,3)/(3,4)/(4,3)/(4,4)
    static const int centerBonus[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (!piece || piece->getColor() != color) continue;
            
            PieceType type = piece->getType();
            if (type == PieceType::PAWN || type == PieceType::KING) continue;
            
            // Lightweight mobility proxy: centrality bonus
            int bonus = centerBonus[row] + centerBonus[col];
            mobility += bonus;
        }
    }
    
    return mobility;
}

// ============================================================
// KNIGHT OUTPOST EVALUATION
// A knight is on an outpost if: on rank 4-6 (for white), 
// supported by own pawn, and no enemy pawns can attack it
// ============================================================
int Search::evaluateKnightOutposts(Board* pos, PieceColor color) {
    int score = 0;
    PieceColor enemy = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (!piece || piece->getType() != PieceType::KNIGHT || piece->getColor() != color)
                continue;
            
            // Check if knight is on an advanced rank
            int advancement = (color == PieceColor::WHITE) ? (7 - row) : row;
            if (advancement < 3 || advancement > 5) continue; // Ranks 4-6 only
            
            // Check if supported by own pawn
            bool supportedByPawn = false;
            int supportRow = (color == PieceColor::WHITE) ? row + 1 : row - 1;
            if (supportRow >= 0 && supportRow < 8) {
                for (int dc = -1; dc <= 1; dc += 2) {
                    int sc = col + dc;
                    if (sc < 0 || sc > 7) continue;
                    Piece* p = pos->getPiece(supportRow, sc);
                    if (p && p->getType() == PieceType::PAWN && p->getColor() == color) {
                        supportedByPawn = true;
                        break;
                    }
                }
            }
            
            if (!supportedByPawn) continue;
            
            // Check no enemy pawns can attack this square
            bool canBeAttacked = false;
            int direction = (color == PieceColor::WHITE) ? 1 : -1; // Enemy pawns advance towards us
            for (int r = row + direction; r >= 0 && r < 8; r += direction) {
                for (int dc = -1; dc <= 1; dc += 2) {
                    int c = col + dc;
                    if (c < 0 || c > 7) continue;
                    Piece* p = pos->getPiece(r, c);
                    if (p && p->getType() == PieceType::PAWN && p->getColor() == enemy) {
                        canBeAttacked = true;
                        break;
                    }
                }
                if (canBeAttacked) break;
            }
            
            if (!canBeAttacked) {
                score += KNIGHT_OUTPOST_BONUS;
                // Bonus for central outposts
                if (col >= 2 && col <= 5) score += KNIGHT_OUTPOST_BONUS / 2;
            }
        }
    }
    
    return score;
}

// ============================================================
// DRAW DETECTION
// ============================================================
bool Search::isLikelyDraw(Board* pos) {
    int whitePieces = 0, blackPieces = 0;
    bool whiteHasBishop = false, whiteHasKnight = false;
    bool blackHasBishop = false, blackHasKnight = false;
    
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (!piece || piece->getType() == PieceType::KING) continue;
            
            if (piece->getColor() == PieceColor::WHITE) {
                whitePieces++;
                if (piece->getType() == PieceType::BISHOP) whiteHasBishop = true;
                if (piece->getType() == PieceType::KNIGHT) whiteHasKnight = true;
            } else {
                blackPieces++;
                if (piece->getType() == PieceType::BISHOP) blackHasBishop = true;
                if (piece->getType() == PieceType::KNIGHT) blackHasKnight = true;
            }
        }
    }
    
    // K vs K
    if (whitePieces == 0 && blackPieces == 0) return true;
    // K+B vs K or K+N vs K
    if (whitePieces == 0 && blackPieces == 1 && (blackHasBishop || blackHasKnight)) return true;
    if (blackPieces == 0 && whitePieces == 1 && (whiteHasBishop || whiteHasKnight)) return true;
    
    return false;
}

// ============================================================
// SINGLE-PASS TAPERED EVALUATION
// All features collected in one 8×8 scan and two small O(8)
// post-processing loops. Replaces ~10 separate board traversals.
// ============================================================
int Search::evaluatePosition(Board* pos) {
    int mgScore = 0, egScore = 0;
    int phase   = TOTAL_PHASE;

    // Per-file pawn/rook tracking
    int  wPawnCols[8] = {}, bPawnCols[8] = {};
    int  wMinRow[8], wMaxRow[8], bMinRow[8], bMaxRow[8];
    for (int i = 0; i < 8; i++) {
        wMinRow[i] = 8;  wMaxRow[i] = -1;
        bMinRow[i] = 8;  bMaxRow[i] = -1;
    }
    bool wRookFile[8] = {}, bRookFile[8] = {};

    // Bishop pair
    int wBish = 0, bBish = 0;

    // Draw detection
    int  wPawnTotal = 0, bPawnTotal = 0;
    int  wNonPK = 0,    bNonPK = 0;   // non-pawn, non-king
    bool wHasBishop = false, wHasKnight = false;
    bool bHasBishop = false, bHasKnight = false;

    // King positions
    Position wKing = pos->getKingPosition(PieceColor::WHITE);
    Position bKing = pos->getKingPosition(PieceColor::BLACK);

    // King safety
    int wShield = 0, bShield = 0;
    int wDanger = 0, bDanger = 0, wAttackers = 0, bAttackers = 0;

    // Mobility proxy (piece centrality)
    int wMob = 0, bMob = 0;
    static constexpr int cB[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };

    // IMPROVED: real (pseudo-legal) mobility counts — far more discriminating
    // than the centrality proxy. Counted inline during the board scan.
    int wMobReal = 0, bMobReal = 0;

    // Pawn grids + knight positions for outpost evaluation
    bool wPawn[8][8] = {}, bPawn[8][8] = {};
    Position wKnts[4] = { Position(-1,-1),Position(-1,-1),Position(-1,-1),Position(-1,-1) };
    Position bKnts[4] = { Position(-1,-1),Position(-1,-1),Position(-1,-1),Position(-1,-1) };
    int wKntCount = 0, bKntCount = 0;

    // ==================================================
    // SINGLE 8×8 PASS: collect all evaluation features
    // ==================================================
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* p = pos->getPiece(row, col);
            if (!p) continue;
            PieceType t = p->getType();
            bool isW    = (p->getColor() == PieceColor::WHITE);
            int  sign   = isW ? 1 : -1;

            // Phase
            switch (t) {
                case PieceType::KNIGHT: phase -= PHASE_KNIGHT; break;
                case PieceType::BISHOP: phase -= PHASE_BISHOP; break;
                case PieceType::ROOK:   phase -= PHASE_ROOK;   break;
                case PieceType::QUEEN:  phase -= PHASE_QUEEN;  break;
                default: break;
            }

            // Draw-detection counts
            if (t != PieceType::PAWN && t != PieceType::KING) {
                if (isW) { wNonPK++; if (t==PieceType::BISHOP){wBish++;wHasBishop=true;} if(t==PieceType::KNIGHT) wHasKnight=true; }
                else     { bNonPK++; if (t==PieceType::BISHOP){bBish++;bHasBishop=true;} if(t==PieceType::KNIGHT) bHasKnight=true; }
            }

            // Material + PST (tapered)
            int mat = (t == PieceType::KING) ? 0 : pieceValue(t);
            mgScore += sign * (mat + PST::getMG(t, p->getColor(), row, col));
            egScore += sign * (mat + PST::getEG(t, p->getColor(), row, col));

            // Rook on 7th rank
            if (t == PieceType::ROOK) {
                if ( isW && row == 1) { mgScore += 20; egScore += 40; }
                if (!isW && row == 6) { mgScore -= 20; egScore -= 40; }
                (isW ? wRookFile : bRookFile)[col] = true;
            }

            // Pawn tracking + pawn-shield for king
            if (t == PieceType::PAWN) {
                if (isW) {
                    wPawnCols[col]++; wPawnTotal++;
                    if (row < wMinRow[col]) wMinRow[col] = row;
                    if (row > wMaxRow[col]) wMaxRow[col] = row;
                    wPawn[row][col] = true;
                    if (wKing.isValid()) {
                        int dc = std::abs(col - wKing.col), dr = wKing.row - row;
                        if (dc <= 1 && dr >= 1 && dr <= 2) wShield += KING_SHIELD_BONUS;
                    }
                } else {
                    bPawnCols[col]++; bPawnTotal++;
                    if (row < bMinRow[col]) bMinRow[col] = row;
                    if (row > bMaxRow[col]) bMaxRow[col] = row;
                    bPawn[row][col] = true;
                    if (bKing.isValid()) {
                        int dc = std::abs(col - bKing.col), dr = row - bKing.row;
                        if (dc <= 1 && dr >= 1 && dr <= 2) bShield += KING_SHIELD_BONUS;
                    }
                }
            }

            // Knight positions for outpost evaluation
            if (t == PieceType::KNIGHT) {
                if ( isW && wKntCount < 4) wKnts[wKntCount++] = Position(row, col);
                if (!isW && bKntCount < 4) bKnts[bKntCount++] = Position(row, col);
            }

            // Mobility proxy (centralised non-pawn/non-king pieces)
            if (t != PieceType::PAWN && t != PieceType::KING) {
                int mob = cB[row] + cB[col];
                if (isW) wMob += mob; else bMob += mob;
            }

            // IMPROVED: count real pseudo-legal destinations (empty or enemy).
            if (improved && t != PieceType::PAWN && t != PieceType::KING) {
                int mc = 0;
                if (t == PieceType::KNIGHT) {
                    static constexpr int km[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
                    for (const auto& k : km) {
                        int nr = row+k[0], nc = col+k[1];
                        if (nr<0||nr>7||nc<0||nc>7) continue;
                        Piece* q = pos->getPiece(nr, nc);
                        if (!q || (q->getColor()==PieceColor::WHITE) != isW) mc++;
                    }
                } else {
                    // sliders: bishop/rook/queen share ray logic
                    static constexpr int diag[4][2]  = {{-1,-1},{-1,1},{1,-1},{1,1}};
                    static constexpr int orth[4][2]  = {{-1,0},{1,0},{0,-1},{0,1}};
                    bool useDiag = (t==PieceType::BISHOP || t==PieceType::QUEEN);
                    bool useOrth = (t==PieceType::ROOK   || t==PieceType::QUEEN);
                    for (int pass=0; pass<2; pass++) {
                        if (pass==0 && !useDiag) continue;
                        if (pass==1 && !useOrth) continue;
                        const int (*dirs)[2] = (pass==0)? diag : orth;
                        for (int di=0; di<4; di++) {
                            for (int s=1; s<8; s++) {
                                int nr=row+s*dirs[di][0], nc=col+s*dirs[di][1];
                                if (nr<0||nr>7||nc<0||nc>7) break;
                                Piece* q = pos->getPiece(nr, nc);
                                if (!q) { mc++; continue; }
                                if ((q->getColor()==PieceColor::WHITE) != isW) mc++;
                                break;
                            }
                        }
                    }
                }
                if (isW) wMobReal += mc; else bMobReal += mc;
            }

            // King attack zone (tropism: enemy pieces near our king)
            if (t != PieceType::KING && t != PieceType::PAWN) {
                if (!isW && wKing.isValid()) {
                    int dist = std::abs(row - wKing.row) + std::abs(col - wKing.col);
                    int w = (t==PieceType::QUEEN)?6:(t==PieceType::ROOK)?3:2;
                    if      (dist <= 4) { wDanger += w*(5-dist); wAttackers++; }
                    else if (dist <= 6) { wDanger += w; }
                }
                if (isW && bKing.isValid()) {
                    int dist = std::abs(row - bKing.row) + std::abs(col - bKing.col);
                    int w = (t==PieceType::QUEEN)?6:(t==PieceType::ROOK)?3:2;
                    if      (dist <= 4) { bDanger += w*(5-dist); bAttackers++; }
                    else if (dist <= 6) { bDanger += w; }
                }
            }
        }
    }

    // Draw detection (K vs K, K+minor vs K)
    int wAll = wNonPK + wPawnTotal, bAll = bNonPK + bPawnTotal;
    if (wAll == 0 && bAll == 0) return 0;
    if (wAll == 0 && bAll == 1 && (bHasBishop || bHasKnight)) return 0;
    if (bAll == 0 && wAll == 1 && (wHasBishop || wHasKnight)) return 0;

    // Phase normalisation (0 = full endgame, 256 = opening with all pieces)
    phase = std::max(0, phase);
    int phaseNorm = (phase * 256 + TOTAL_PHASE / 2) / TOTAL_PHASE;

    // Bishop pair
    if (wBish >= 2) { mgScore += BISHOP_PAIR_BONUS; egScore += BISHOP_PAIR_BONUS; }
    if (bBish >= 2) { mgScore -= BISHOP_PAIR_BONUS; egScore -= BISHOP_PAIR_BONUS; }

    // Mobility — IMPROVED uses real pseudo-legal mobility (weight 2),
    // baseline uses the cheap centrality proxy (weight 3).
    if (improved) {
        mgScore += (wMobReal - bMobReal) * 2;
        egScore += (wMobReal - bMobReal) * 2;
    } else {
        mgScore += (wMob - bMob) * MOBILITY_WEIGHT;
        egScore += (wMob - bMob) * MOBILITY_WEIGHT;
    }

    // King shield (middlegame only)
    mgScore += wShield - bShield;

    // King attack zone
    mgScore -= wDanger * (wAttackers >= 3 ? 2 : 1);
    mgScore += bDanger * (bAttackers >= 3 ? 2 : 1);

    // O(8) per-file pass: doubled/isolated/passed pawns + rook files
    for (int c = 0; c < 8; c++) {
        if (wPawnCols[c] > 1) { int p=DOUBLED_PAWN_PENALTY*(wPawnCols[c]-1); mgScore+=p; egScore+=p; }
        if (bPawnCols[c] > 1) { int p=DOUBLED_PAWN_PENALTY*(bPawnCols[c]-1); mgScore-=p; egScore-=p; }

        if (wPawnCols[c] > 0) {
            bool nb = (c>0 && wPawnCols[c-1]>0) || (c<7 && wPawnCols[c+1]>0);
            if (!nb) { mgScore += ISOLATED_PAWN_PENALTY; egScore += ISOLATED_PAWN_PENALTY; }
        }
        if (bPawnCols[c] > 0) {
            bool nb = (c>0 && bPawnCols[c-1]>0) || (c<7 && bPawnCols[c+1]>0);
            if (!nb) { mgScore -= ISOLATED_PAWN_PENALTY; egScore -= ISOLATED_PAWN_PENALTY; }
        }

        // White passed pawn: no black pawn is AHEAD (lower row) on adjacent files
        if (wMinRow[c] < 8) {
            bool passed = true;
            for (int ac = std::max(0,c-1); ac <= std::min(7,c+1); ac++)
                if (bMinRow[ac] < wMinRow[c]) { passed = false; break; }
            if (passed) {
                int adv = 7 - wMinRow[c];
                mgScore += PASSED_PAWN_BONUS[adv]; egScore += PASSED_PAWN_BONUS[adv];
            }
        }
        // Black passed pawn: no white pawn is AHEAD (higher row) on adjacent files
        if (bMaxRow[c] > -1) {
            bool passed = true;
            for (int ac = std::max(0,c-1); ac <= std::min(7,c+1); ac++)
                if (wMaxRow[ac] > bMaxRow[c]) { passed = false; break; }
            if (passed) {
                int adv = bMaxRow[c];
                mgScore -= PASSED_PAWN_BONUS[adv]; egScore -= PASSED_PAWN_BONUS[adv];
            }
        }

        // Rook open / semi-open file
        if (wRookFile[c]) {
            if (!wPawnCols[c] && !bPawnCols[c]) { mgScore+=ROOK_OPEN_FILE_BONUS;  egScore+=ROOK_OPEN_FILE_BONUS;  }
            else if (!wPawnCols[c])              { mgScore+=ROOK_SEMI_OPEN_BONUS;  egScore+=ROOK_SEMI_OPEN_BONUS;  }
        }
        if (bRookFile[c]) {
            if (!wPawnCols[c] && !bPawnCols[c]) { mgScore-=ROOK_OPEN_FILE_BONUS;  egScore-=ROOK_OPEN_FILE_BONUS;  }
            else if (!bPawnCols[c])              { mgScore-=ROOK_SEMI_OPEN_BONUS;  egScore-=ROOK_SEMI_OPEN_BONUS;  }
        }
    }

    // Knight outposts (O(knights), not O(64))
    for (int i = 0; i < wKntCount; i++) {
        int kr = wKnts[i].row, kc = wKnts[i].col;
        int adv = 7 - kr;
        if (adv < 3 || adv > 5) continue;
        bool sup = (kr+1<8) && ((kc>0 && wPawn[kr+1][kc-1]) || (kc<7 && wPawn[kr+1][kc+1]));
        if (!sup) continue;
        bool thr = false;
        for (int r = kr+1; r < 8 && !thr; r++) {
            if (kc>0 && bPawn[r][kc-1]) thr=true;
            if (kc<7 && bPawn[r][kc+1]) thr=true;
        }
        if (!thr) {
            int bns = KNIGHT_OUTPOST_BONUS + ((kc>=2&&kc<=5) ? KNIGHT_OUTPOST_BONUS/2 : 0);
            mgScore += bns; egScore += bns;
        }
    }
    for (int i = 0; i < bKntCount; i++) {
        int kr = bKnts[i].row, kc = bKnts[i].col;
        int adv = kr;   // black advances toward row 7
        if (adv < 3 || adv > 5) continue;
        bool sup = (kr-1>=0) && ((kc>0 && bPawn[kr-1][kc-1]) || (kc<7 && bPawn[kr-1][kc+1]));
        if (!sup) continue;
        bool thr = false;
        for (int r = kr-1; r >= 0 && !thr; r--) {
            if (kc>0 && wPawn[r][kc-1]) thr=true;
            if (kc<7 && wPawn[r][kc+1]) thr=true;
        }
        if (!thr) {
            int bns = KNIGHT_OUTPOST_BONUS + ((kc>=2&&kc<=5) ? KNIGHT_OUTPOST_BONUS/2 : 0);
            mgScore -= bns; egScore -= bns;
        }
    }

    // Tapered merge
    int score = ((mgScore * (256 - phaseNorm)) + (egScore * phaseNorm)) / 256;

    // Tempo bonus
    if (pos->getCurrentTurn() == PieceColor::WHITE) score += TEMPO_BONUS;
    else score -= TEMPO_BONUS;

    return score;
}

// ============================================================
// MOVE ORDERING (critical for alpha-beta performance)
// ============================================================

void Search::storeKiller(int ply, Position from, Position to) {
    if (ply >= MAX_PLY) return;
    if (killerFrom[ply][0].row == from.row && killerFrom[ply][0].col == from.col &&
        killerTo[ply][0].row == to.row && killerTo[ply][0].col == to.col) return;
    killerFrom[ply][1] = killerFrom[ply][0];
    killerTo[ply][1] = killerTo[ply][0];
    killerFrom[ply][0] = from;
    killerTo[ply][0] = to;
}

int Search::scoreMove(Board* pos, Position from, Position to, int ply,
                       Position ttFrom, Position ttTo,
                       Position prevFrom, Position prevTo) {
    // 1. TT move: highest priority
    if (ttFrom.row == from.row && ttFrom.col == from.col &&
        ttTo.row == to.row && ttTo.col == to.col) {
        return 10000000;
    }
    
    Piece* mover = pos->getPiece(from);
    
    // Check for promotion
    bool isPromotion = mover && mover->getType() == PieceType::PAWN &&
        ((mover->getColor() == PieceColor::WHITE && to.row == 0) ||
         (mover->getColor() == PieceColor::BLACK && to.row == 7));
    
    // Promotions: always scored very high (queen promotion is ~always winning)
    if (isPromotion) {
        Piece* captured = pos->getPiece(to);
        return captured ? 6000000 + pieceValue(captured->getType()) : 5500000;
    }
    
    Piece* captured = pos->getPiece(to);
    
    // En passant detection: pawn moves to en passant target (square is empty)
    if (!captured && mover && mover->getType() == PieceType::PAWN) {
        Position ep = pos->getEnPassantTarget();
        if (ep.isValid() && ep.row == to.row && ep.col == to.col) {
            return 5000100; // PxP is always a good capture (SEE = 100)
        }
    }
    
    if (captured) {
        int victimVal = pieceValue(captured->getType());
        int attackerVal = mover ? pieceValue(mover->getType()) : 0;
        int mvvlva = victimVal * 100 - attackerVal;
        
        // 2. Good captures (SEE >= 0): if victim >= attacker, almost always good
        if (victimVal >= attackerVal) {
            return 5000000 + mvvlva;
        }
        
        // Attacker more valuable than victim: call full SEE
        int seeValue = see(pos, from, to);
        if (seeValue >= 0) {
            return 5000000 + mvvlva; // good capture
        } else {
            // 7. Bad captures (SEE < 0): below all quiet moves
            return -5000000 + mvvlva;
        }
    }
    
    // --- Quiet moves ---
    
    // 3. Killer moves
    if (ply < MAX_PLY) {
        if (killerFrom[ply][0].row == from.row && killerFrom[ply][0].col == from.col &&
            killerTo[ply][0].row == to.row && killerTo[ply][0].col == to.col) {
            return 900000;
        }
        if (killerFrom[ply][1].row == from.row && killerFrom[ply][1].col == from.col &&
            killerTo[ply][1].row == to.row && killerTo[ply][1].col == to.col) {
            return 800000;
        }
    }
    
    // 4. Countermove heuristic
    if (prevFrom.isValid() && prevTo.isValid()) {
        int prevFromSq = prevFrom.row * 8 + prevFrom.col;
        int prevToSq = prevTo.row * 8 + prevTo.col;
        if (countermoveFrom[prevFromSq][prevToSq].row == from.row &&
            countermoveFrom[prevFromSq][prevToSq].col == from.col &&
            countermoveTo[prevFromSq][prevToSq].row == to.row &&
            countermoveTo[prevFromSq][prevToSq].col == to.col) {
            return 700000;
        }
    }
    
    // 5/6. History heuristic for remaining quiet moves
    int fromSq = from.row * 8 + from.col;
    int toSq = to.row * 8 + to.col;
    return historyTable[fromSq][toSq];
}

int Search::generateOrderedMoves(Board* pos, int ply,
                                  Position ttFrom, Position ttTo,
                                  bool capturesOnly,
                                  Position prevFrom, Position prevTo,
                                  ScoredMove* outMoves) {
    int count = 0;
    PieceColor color = pos->getCurrentTurn();
    PieceColor enemy = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;

    // Fully inlined move generation — eliminates per-piece std::vector<Position>
    // heap allocations and virtual getPossibleMoves() dispatch.
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = pos->getPiece(row, col);
            if (!piece || piece->getColor() != color) continue;

            Position from(row, col);
            PieceType type = piece->getType();

            // Macro for emitting a scored move into the output buffer
            #define EMIT_MOVE(toPos) do { \
                if (count < 254) \
                    outMoves[count++] = ScoredMove(from, toPos, \
                        scoreMove(pos, from, toPos, ply, ttFrom, ttTo, prevFrom, prevTo)); \
            } while(0)

            switch (type) {

            case PieceType::PAWN: {
                int dir = (color == PieceColor::WHITE) ? -1 : 1;
                int startRank = (color == PieceColor::WHITE) ? 6 : 1;
                int promoRank = (color == PieceColor::WHITE) ? 0 : 7;
                int frow = row + dir;

                if (frow >= 0 && frow < 8) {
                    // Forward push
                    if (!pos->getPiece(frow, col)) {
                        bool isPromo = (frow == promoRank);
                        if (!capturesOnly || isPromo) {
                            Position to(frow, col);
                            EMIT_MOVE(to);
                        }
                        // Double push
                        if (!capturesOnly && row == startRank) {
                            int drow = row + 2 * dir;
                            if (!pos->getPiece(drow, col)) {
                                Position to(drow, col);
                                EMIT_MOVE(to);
                            }
                        }
                    }
                    // Captures (always included, even in capturesOnly)
                    Position ep = pos->getEnPassantTarget();
                    for (int dc = -1; dc <= 1; dc += 2) {
                        int tc = col + dc;
                        if (tc < 0 || tc > 7) continue;
                        Position to(frow, tc);
                        Piece* target = pos->getPiece(to);
                        if ((target && target->getColor() != color) || to == ep) {
                            EMIT_MOVE(to);
                        }
                    }
                }
                break;
            }

            case PieceType::KNIGHT: {
                static constexpr int km[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
                for (const auto& k : km) {
                    int tr = row + k[0], tc = col + k[1];
                    if (tr < 0 || tr > 7 || tc < 0 || tc > 7) continue;
                    Piece* target = pos->getPiece(tr, tc);
                    if (target && target->getColor() == color) continue;
                    if (capturesOnly && !target) continue;
                    Position to(tr, tc);
                    EMIT_MOVE(to);
                }
                break;
            }

            case PieceType::BISHOP: {
                static constexpr int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
                for (const auto& d : dirs) {
                    for (int s = 1; s < 8; s++) {
                        int tr = row + s*d[0], tc = col + s*d[1];
                        if (tr < 0 || tr > 7 || tc < 0 || tc > 7) break;
                        Piece* target = pos->getPiece(tr, tc);
                        if (target) {
                            if (target->getColor() != color) {
                                Position to(tr, tc);
                                EMIT_MOVE(to);
                            }
                            break;
                        }
                        if (!capturesOnly) {
                            Position to(tr, tc);
                            EMIT_MOVE(to);
                        }
                    }
                }
                break;
            }

            case PieceType::ROOK: {
                static constexpr int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& d : dirs) {
                    for (int s = 1; s < 8; s++) {
                        int tr = row + s*d[0], tc = col + s*d[1];
                        if (tr < 0 || tr > 7 || tc < 0 || tc > 7) break;
                        Piece* target = pos->getPiece(tr, tc);
                        if (target) {
                            if (target->getColor() != color) {
                                Position to(tr, tc);
                                EMIT_MOVE(to);
                            }
                            break;
                        }
                        if (!capturesOnly) {
                            Position to(tr, tc);
                            EMIT_MOVE(to);
                        }
                    }
                }
                break;
            }

            case PieceType::QUEEN: {
                static constexpr int dirs[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
                for (const auto& d : dirs) {
                    for (int s = 1; s < 8; s++) {
                        int tr = row + s*d[0], tc = col + s*d[1];
                        if (tr < 0 || tr > 7 || tc < 0 || tc > 7) break;
                        Piece* target = pos->getPiece(tr, tc);
                        if (target) {
                            if (target->getColor() != color) {
                                Position to(tr, tc);
                                EMIT_MOVE(to);
                            }
                            break;
                        }
                        if (!capturesOnly) {
                            Position to(tr, tc);
                            EMIT_MOVE(to);
                        }
                    }
                }
                break;
            }

            case PieceType::KING: {
                static constexpr int dirs[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
                for (const auto& d : dirs) {
                    int tr = row + d[0], tc = col + d[1];
                    if (tr < 0 || tr > 7 || tc < 0 || tc > 7) continue;
                    Piece* target = pos->getPiece(tr, tc);
                    if (target && target->getColor() == color) continue;
                    if (capturesOnly && !target) continue;
                    Position to(tr, tc);
                    EMIT_MOVE(to);
                }
                // Castling (includes attack checks — can't castle through check)
                if (!piece->hasMovedBefore() && !capturesOnly) {
                    // Kingside
                    Piece* kr = pos->getPiece(row, 7);
                    if (kr && kr->getType() == PieceType::ROOK && !kr->hasMovedBefore() &&
                        !pos->getPiece(row, 5) && !pos->getPiece(row, 6)) {
                        if (!pos->isSquareUnderAttack(Position(row, 4), enemy) &&
                            !pos->isSquareUnderAttack(Position(row, 5), enemy) &&
                            !pos->isSquareUnderAttack(Position(row, 6), enemy)) {
                            Position to(row, 6);
                            EMIT_MOVE(to);
                        }
                    }
                    // Queenside
                    Piece* qr = pos->getPiece(row, 0);
                    if (qr && qr->getType() == PieceType::ROOK && !qr->hasMovedBefore() &&
                        !pos->getPiece(row, 1) && !pos->getPiece(row, 2) && !pos->getPiece(row, 3)) {
                        if (!pos->isSquareUnderAttack(Position(row, 4), enemy) &&
                            !pos->isSquareUnderAttack(Position(row, 3), enemy) &&
                            !pos->isSquareUnderAttack(Position(row, 2), enemy)) {
                            Position to(row, 2);
                            EMIT_MOVE(to);
                        }
                    }
                }
                break;
            }

            default: break;
            }

            #undef EMIT_MOVE
        }
    }
    // Caller does lazy selection sort during move iteration
    return count;
}

// ============================================================
// QUIESCENCE SEARCH - prevents horizon effect
// ============================================================
int Search::quiescence(Board* pos, int alpha, int beta, int ply) {
    qNodesSearched.fetch_add(1, std::memory_order_relaxed);

    if (stopRequested.load(std::memory_order_relaxed)) return 0;
    checkTimeLimit();

    // Hard ply guard: prevents unbounded recursion through forcing check
    // sequences when evasions are searched.
    if (ply >= MAX_PLY + 32) {
        int e = evaluatePosition(pos);
        return (pos->getCurrentTurn() == PieceColor::BLACK) ? -e : e;
    }

    // IMPROVED: when in check, quiescence must resolve the check by searching
    // ALL evasions (not just captures) and must not "stand pat" — a side in
    // check has no right to assume the static eval is a lower bound.
    bool inCheck = improved && pos->isKingInCheck(pos->getCurrentTurn());

    int standPat = 0;
    if (!inCheck) {
        // Stand-pat
        standPat = evaluatePosition(pos);
        if (pos->getCurrentTurn() == PieceColor::BLACK)
            standPat = -standPat;

        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    // In check: generate every move (evasions). Otherwise: captures only.
    ScoredMove movesBuf[256];
    int numMoves = generateOrderedMoves(pos, ply, Position(-1,-1), Position(-1,-1), !inCheck,
                                        Position(-1,-1), Position(-1,-1), movesBuf);

    int legalMoves = 0;
    for (int mi = 0; mi < numMoves; mi++) {
        // Lazy selection sort: pick best remaining capture each step
        for (int mj = mi+1; mj < numMoves; mj++)
            if (movesBuf[mj].score > movesBuf[mi].score) std::swap(movesBuf[mi], movesBuf[mj]);
        const ScoredMove& move = movesBuf[mi];
        if (stopRequested.load()) return 0;

        // Delta / SEE pruning — only when NOT in check (never prune evasions)
        Piece* captured = pos->getPiece(move.to);
        if (!inCheck && captured) {
            int delta = standPat + pieceValue(captured->getType()) + 200;
            if (delta < alpha) continue;

            // SEE pruning: skip losing captures (attacker > victim → call SEE)
            Piece* capMover = pos->getPiece(move.from);
            if (capMover && pieceValue(capMover->getType()) > pieceValue(captured->getType())) {
                if (see(pos, move.from, move.to) < 0) {
                    continue;
                }
            }
        }

        UndoInfo undo = pos->makeMove(move.from, move.to);

        // Legality check: if our king is now in check, this was illegal
        PieceColor us = pos->getCurrentTurn() == PieceColor::WHITE ? PieceColor::BLACK : PieceColor::WHITE;
        if (pos->isKingInCheck(us)) {
            pos->unmakeMove(undo);
            continue;
        }
        legalMoves++;

        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos->unmakeMove(undo);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    // In check with no legal evasion → checkmate.
    if (inCheck && legalMoves == 0) return -100000 + ply;

    return alpha;
}

// ============================================================
// NEGAMAX WITH ALPHA-BETA + PVS + NMP + LMR + FUTILITY + 
//   RAZORING + IID + LMP + COUNTERMOVES
// ============================================================
int Search::negamax(Board* pos, int depth, int alpha, int beta, int ply, 
                     uint64_t posHash, bool allowNullMove,
                     Position prevFrom, Position prevTo,
                     Position excludeFrom, Position excludeTo) {
    nodesSearched.fetch_add(1, std::memory_order_relaxed);
    tt.prefetch(posHash);
    
    if (!pos || stopRequested.load(std::memory_order_relaxed)) return 0;
    checkTimeLimit();
    
    // ================================================================
    // REPETITION DETECTION (thread-local for SMP safety)
    // Checks if current position was seen earlier in this search line.
    // Returns draw score if repeated (prevents infinite cycles).
    // ================================================================
    static thread_local uint64_t searchHashes[MAX_PLY];
    searchHashes[ply] = posHash;
    if (ply >= 2) {
        for (int i = ply - 2; i >= 0; i -= 2) {
            if (searchHashes[i] == posHash)            // Draw by repetition
                return improved ? -CONTEMPT : 0;       // IMPROVED: avoid draws when ~equal
        }
    }
    
    // Prevent excessive ply
    if (ply >= MAX_PLY) return evaluatePosition(pos) * (pos->getCurrentTurn() == PieceColor::WHITE ? 1 : -1);
    
    bool isPV = (beta - alpha > 1); // PV node if not a null window
    
    // TT probe
    Position ttBestFrom(-1, -1), ttBestTo(-1, -1);
    TTEntry* ttEntry = tt.probe(posHash);
    if (ttEntry) {
        if (ttEntry->depth >= depth) {
            int ttScore = ttEntry->score;
            // IMPROVED: mate scores are stored relative to the storing node;
            // translate them back to be relative to the current ply.
            if (improved) {
                if (ttScore > 90000)      ttScore -= ply;
                else if (ttScore < -90000) ttScore += ply;
            }
            // Don't use TT cutoffs in singular extension search or PV nodes
            if (!isPV && !excludeFrom.isValid()) {
                if (ttEntry->getFlag() == TTFlag::EXACT) return ttScore;
                if (ttEntry->getFlag() == TTFlag::ALPHA && ttScore <= alpha) return alpha;
                if (ttEntry->getFlag() == TTFlag::BETA && ttScore >= beta) return beta;
            }
        }
        ttBestFrom = ttEntry->getBestFrom();
        ttBestTo = ttEntry->getBestTo();
    }
    
    // Quiescence at depth 0
    if (depth <= 0) {
        return quiescence(pos, alpha, beta, ply);
    }
    
    PieceColor currentColor = pos->getCurrentTurn();
    bool inCheck = false;
    try { inCheck = pos->isKingInCheck(currentColor); } catch (...) {}
    
    // Check extension: don't reduce depth when in check
    if (inCheck) depth++;
    
    // ================================================================
    // COMPUTE STATIC EVAL ONCE (for all pruning decisions)
    // ================================================================
    int staticEval = 0;
    bool improving = false;
    static thread_local int staticEvalHistory[MAX_PLY];
    
    if (!inCheck) {
        staticEval = evaluatePosition(pos);
        if (currentColor == PieceColor::BLACK) staticEval = -staticEval;
        staticEvalHistory[ply] = staticEval;
        improving = (ply >= 2 && staticEval > staticEvalHistory[ply - 2]);
    } else {
        staticEvalHistory[ply] = -100000; // Unknown when in check
    }
    
    // ================================================================
    // REVERSE FUTILITY PRUNING (static null move pruning)
    // If eval is way above beta at shallow depth, just return eval.
    // ================================================================
    if (!isPV && !inCheck && depth <= 6 && ply > 0) {
        int rfpMargin = improving ? (80 * depth) : (100 * depth);
        if (staticEval - rfpMargin >= beta) {
            return staticEval;
        }
    }
    
    // ================================================================
    // RAZORING: At shallow depths, if static eval is far below alpha,
    // verify with quiescence. If still bad, prune.
    // ================================================================
    if (!isPV && !inCheck && depth <= 2 && ply > 0) {
        int margin = (depth == 1) ? RAZORING_MARGIN_D1 : RAZORING_MARGIN_D2;
        if (staticEval + margin <= alpha) {
            int qScore = quiescence(pos, alpha, beta, ply);
            if (qScore <= alpha) return qScore;
        }
    }
    
    // ================================================================
    // NULL MOVE PRUNING (improved: adaptive R, endgame safety)
    // Skip if we don't have non-pawn material (zugzwang risk)
    // ================================================================
    if (allowNullMove && !inCheck && depth >= 3 && ply > 0 && !isPV
        && pos->hasNonPawnMaterial(currentColor)) {
        pos->switchTurn();
        uint64_t nullHash = posHash ^ zobrist.getSideKey();
        
        // Adaptive R: deeper = more aggressive reduction
        int R = 3 + depth / 3;
        if (staticEval >= beta + 100) R++;
        R = std::min(R, depth - 1);
        
        int nullScore = -negamax(pos, depth - 1 - R, 
                                  -beta, -beta + 1, ply + 1, nullHash, false);
        pos->switchTurn();
        
        if (nullScore >= beta) {
            // Verification at high depth to guard against zugzwang
            if (depth >= 10) {
                int vScore = negamax(pos, depth - 5, beta - 1, beta, ply, posHash, false, prevFrom, prevTo);
                if (vScore >= beta) return beta;
            } else {
                return beta;
            }
        }
    }
    
    // ================================================================
    // INTERNAL ITERATIVE DEEPENING (IID)
    // Also fire in non-PV when depth is high and we have no TT move
    // ================================================================
    if (!ttBestFrom.isValid() && depth >= IID_DEPTH_THRESHOLD) {
        int iidDepth = (isPV) ? depth - IID_REDUCTION : depth / 2;
        negamax(pos, iidDepth, alpha, beta, ply, posHash, false, prevFrom, prevTo);
        
        TTEntry* iidEntry = tt.probe(posHash);
        if (iidEntry) {
            ttBestFrom = iidEntry->getBestFrom();
            ttBestTo = iidEntry->getBestTo();
        }
    }
    
    // ================================================================
    // SINGULAR EXTENSIONS: If TT move is significantly better than
    // all alternatives, extend its search depth by 1 ply.
    // ================================================================
    bool singularExtend = false;
    if (ply > 0 && depth >= 8 && ttEntry && ttEntry->depth >= depth - 3
        && ttEntry->getFlag() != TTFlag::ALPHA && ttBestFrom.isValid()
        && !excludeFrom.isValid()) {
        int sBeta = ttEntry->score - 2 * depth;
        int sScore = negamax(pos, (depth - 1) / 2, sBeta - 1, sBeta, ply, posHash, false,
                              prevFrom, prevTo, ttBestFrom, ttBestTo);
        if (sScore < sBeta) {
            singularExtend = true;
        }
    }
    
    // ================================================================
    // FUTILITY PRUNING (using cached static eval)
    // ================================================================
    bool futilityPrune = false;
    if (!isPV && !inCheck && depth <= 3 && ply > 0) {
        int margin;
        if (depth == 1) margin = FUTILITY_MARGIN_D1;
        else if (depth == 2) margin = FUTILITY_MARGIN_D2;
        else margin = 900; // depth 3
        if (!improving) margin -= 50; // More aggressive when not improving
        if (staticEval + margin <= alpha) {
            futilityPrune = true;
        }
    }
    
    // Generate ordered moves into stack buffer (no heap allocation per node)
    ScoredMove movesBuf[256];
    int numMoves = generateOrderedMoves(pos, ply, ttBestFrom, ttBestTo, false, prevFrom, prevTo, movesBuf);

    Position bestFrom(-1, -1), bestTo(-1, -1);
    TTFlag ttFlag = TTFlag::ALPHA;
    int bestScore = std::numeric_limits<int>::min();
    int movesSearched = 0;  // counts LEGAL moves only
    int moveIndex = 0;      // counts all moves tried

    // Track searched quiet moves for history malus on beta cutoff
    int searchedQuietFrom[64], searchedQuietTo[64];
    int numSearchedQuiets = 0;

    for (int mi = 0; mi < numMoves; mi++) {
        // Lazy selection sort: swap best remaining to front, O(n) per pick
        for (int mj = mi+1; mj < numMoves; mj++)
            if (movesBuf[mj].score > movesBuf[mi].score) std::swap(movesBuf[mi], movesBuf[mj]);
        const ScoredMove& move = movesBuf[mi];

        if (stopRequested.load()) return 0;

        bool isCapture = (pos->getPiece(move.to) != nullptr);
        bool isPromotion = false;
        Piece* mover = pos->getPiece(move.from);
        if (mover && mover->getType() == PieceType::PAWN) {
            if ((mover->getColor() == PieceColor::WHITE && move.to.row == 0) ||
                (mover->getColor() == PieceColor::BLACK && move.to.row == 7)) {
                isPromotion = true;
            }
        }
        
        // Skip excluded move (for singular extension verification)
        if (excludeFrom.isValid() && move.from.row == excludeFrom.row &&
            move.from.col == excludeFrom.col && move.to.row == excludeTo.row &&
            move.to.col == excludeTo.col) {
            moveIndex++;
            continue;
        }
        
        // ================================================================
        // FUTILITY PRUNING: skip quiet moves at low depth
        // ================================================================
        if (futilityPrune && !isCapture && !isPromotion && movesSearched > 0) {
            moveIndex++;
            continue;
        }
        
        // ================================================================
        // LATE MOVE PRUNING (LMP): At low depths, skip quiet moves
        // beyond a threshold count.
        // ================================================================
        if (!isPV && !inCheck && depth <= 5 && !isCapture && !isPromotion && 
            movesSearched >= LMP_THRESHOLD[depth]) {
            moveIndex++;
            continue;
        }
        
        // ================================================================
        // HISTORY PRUNING: At shallow depth, prune quiet moves with
        // very negative history (proven to be bad moves).
        // ================================================================
        if (!isPV && !inCheck && depth <= 4 && !isCapture && !isPromotion && movesSearched > 0) {
            int hfSq = move.from.row * 8 + move.from.col;
            int htSq = move.to.row * 8 + move.to.col;
            if (historyTable[hfSq][htSq] < -1024 * depth) {
                moveIndex++;
                continue;
            }
        }
        
        // Compute child hash BEFORE makeMove (incremental, no full recompute)
        uint64_t childHash = hashAfterMove(posHash, pos, move.from, move.to);
        
        UndoInfo undo = pos->makeMove(move.from, move.to);
        
        // Legality check: if our king is in check after our move, it's illegal
        PieceColor us = (pos->getCurrentTurn() == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
        if (pos->isKingInCheck(us)) {
            pos->unmakeMove(undo);
            moveIndex++;
            continue;
        }

        // Hash self-check: incremental childHash must equal a full recompute.
        if (s_hashCheck) {
            s_hashChecked.fetch_add(1, std::memory_order_relaxed);
            if (rootHash(pos) != childHash)
                s_hashMismatch.fetch_add(1, std::memory_order_relaxed);
        }

        // ================================================================
        // MOVE EXTENSIONS
        // ================================================================
        int ext = 0;
        // Singular extension: TT move is much better than alternatives
        if (singularExtend && move.from.row == ttBestFrom.row && 
            move.from.col == ttBestFrom.col && move.to.row == ttBestTo.row && 
            move.to.col == ttBestTo.col) {
            ext = 1;
        }
        // Passed pawn push to 6th or 7th rank
        if (!ext && mover && mover->getType() == PieceType::PAWN) {
            int advancement = (mover->getColor() == PieceColor::WHITE) ? (7 - move.to.row) : move.to.row;
            if (advancement >= 5) ext = 1;
        }
        // Recapture extension
        if (!ext && isCapture && prevTo.isValid() && 
            move.to.row == prevTo.row && move.to.col == prevTo.col) {
            ext = 1;
        }
        int newDepth = depth - 1 + ext;
        
        int score;
        
        if (movesSearched == 0) {
            // ================================================================
            // FIRST MOVE: always full window search (PV move)
            // ================================================================
            score = -negamax(pos, newDepth, -beta, -alpha, 
                              ply + 1, childHash, true, move.from, move.to);
        } else {
            // ================================================================
            // LATE MOVE REDUCTIONS (LMR) + PVS — logarithmic formula
            // ================================================================
            bool doLMR = (movesSearched >= LMR_FULL_DEPTH_MOVES && 
                          depth >= LMR_REDUCTION_LIMIT && 
                          !isCapture && !isPromotion && !inCheck);
            
            int reduction = 0;
            if (doLMR) {
                // Logarithmic LMR — more aggressive divisor (1.4 vs Stockfish's ~1.75)
                // Produces larger reductions → fewer nodes at high depth
                reduction = (int)(std::log((double)depth) * std::log((double)movesSearched) / 1.4);
                if (reduction < 1) reduction = 1;
                
                // Adjust: reduce less in PV nodes
                if (isPV) reduction--;
                // Adjust: reduce less when position is improving
                if (improving) reduction--;
                // Adjust: reduce more for moves with bad history
                int hFromSq = move.from.row * 8 + move.from.col;
                int hToSq = move.to.row * 8 + move.to.col;
                if (historyTable[hFromSq][hToSq] < -512) reduction++;
                if (historyTable[hFromSq][hToSq] < -4096) reduction++;
                // Adjust: reduce more for late moves
                if (movesSearched >= 8) reduction++;
                
                // Clamp reduction: leave at least 1 ply
                reduction = std::max(0, std::min(reduction, newDepth - 1));
            }
            
            // PVS: null window search first
            score = -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, 
                              ply + 1, childHash, true, move.from, move.to);
            
            // If LMR reduced and score beats alpha, re-search at full depth null window
            if (doLMR && score > alpha) {
                score = -negamax(pos, newDepth, -alpha - 1, -alpha, 
                                  ply + 1, childHash, true, move.from, move.to);
            }
            
            // If null window beats alpha in PV nodes, re-search with full window
            if (isPV && score > alpha && score < beta) {
                score = -negamax(pos, newDepth, -beta, -alpha, 
                                  ply + 1, childHash, true, move.from, move.to);
            }
        }
        
        pos->unmakeMove(undo);
        movesSearched++;
        
        // Track quiet moves for history malus
        if (!isCapture && numSearchedQuiets < 64) {
            searchedQuietFrom[numSearchedQuiets] = move.from.row * 8 + move.from.col;
            searchedQuietTo[numSearchedQuiets] = move.to.row * 8 + move.to.col;
            numSearchedQuiets++;
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestFrom = move.from;
            bestTo = move.to;
        }
        
        if (score > alpha) {
            alpha = score;
            ttFlag = TTFlag::EXACT;
        }
        
        if (alpha >= beta) {
            ttFlag = TTFlag::BETA;
            
            // Killer + countermove + history for quiet moves
            if (!isCapture) {
                storeKiller(ply, move.from, move.to);
                storeCountermove(prevFrom, prevTo, move.from, move.to);
                
                int fromSq = move.from.row * 8 + move.from.col;
                int toSq = move.to.row * 8 + move.to.col;
                int bonus = depth * depth * depth;  // cubic bonus — scales much better at high depth
                historyTable[fromSq][toSq] += bonus - historyTable[fromSq][toSq] * bonus / 32768;
                
                // History malus: penalize all previously searched quiet moves
                // that DIDN'T produce a cutoff
                for (int q = 0; q < numSearchedQuiets - 1; q++) {
                    int& h = historyTable[searchedQuietFrom[q]][searchedQuietTo[q]];
                    h += -bonus - h * bonus / 32768;
                }
            }
            break;
        }
    }
    
    // If no legal moves were found, it's checkmate or stalemate
    if (movesSearched == 0) {
        if (inCheck) return -100000 + ply; // Checkmate
        return improved ? -CONTEMPT : 0; // Stalemate (IMPROVED: slight draw aversion)
    }
    
    // Store in TT
    if (!stopRequested.load() && bestFrom.isValid()) {
        int storeScore = bestScore;
        // IMPROVED: store mate scores relative to this node (add the plies it
        // took to reach here) so retrieval at other depths is distance-correct.
        if (improved) {
            if (storeScore > 90000)      storeScore += ply;
            else if (storeScore < -90000) storeScore -= ply;
        }
        tt.store(posHash, storeScore, depth, ttFlag, bestFrom, bestTo);
    }

    return bestScore;
}

// ============================================================
// ITERATIVE DEEPENING SEARCH
// ============================================================
void Search::searchMoves(Board* pos, int maxDepth, SearchResult& result) {
    // Reset the stop flag: it is raised at the end of every search (to halt SMP
    // helpers). Without this reset the synchronous getBestMove()/getBestMoveTimed()
    // API would bail out immediately on the second and subsequent calls.
    stopRequested.store(false, std::memory_order_relaxed);
    nodesSearched.store(0, std::memory_order_relaxed);
    qNodesSearched.store(0, std::memory_order_relaxed);
    clearHeuristics();
    tt.newGeneration();  // Age out stale TT entries from previous search

    // Single board copy for main thread search (make/unmake avoids per-node heap allocs)
    std::unique_ptr<Board> searchBoard(pos->copyBoard());
    pos = searchBoard.get();
    
    searchStartTime = std::chrono::high_resolution_clock::now();
    
    std::cout << "\n========== AI SEARCH START ==========\n";
    std::cout << "Max depth: " << maxDepth << " plies";
    if (timeLimitMs > 0) std::cout << " | Time limit: " << timeLimitMs << "ms";
    std::cout << "\n";
    std::cout << "Turn: " << (pos->getCurrentTurn() == PieceColor::WHITE ? "WHITE" : "BLACK") << "\n";
    std::cout << "Threads: " << numThreads << " (Lazy SMP)\n";
    std::cout << "Features: ID + PVS + TT + QSearch + NMP + LMR + LMP + Futility + Razoring + IID + Countermoves + MakeUnmake + IncrementalHash + PseudoLegal + PST + LazySMP + TimeMgmt\n";
    std::cout << "====================================\n\n";
    
    // ================================================================
    // LAZY SMP: Launch helper threads that search with different depths
    // They share the TT, so their results help the main thread.
    // ================================================================
    if (numThreads > 1) {
        for (int t = 1; t < numThreads; t++) {
            smpThreads.emplace_back(&Search::smpHelperSearch, this, pos, maxDepth, t);
        }
    }
    
    Position bestFrom(-1, -1), bestTo(-1, -1);
    int bestScore = 0;
    int prevScore = 0;
    int completedDepth = 0;
    
    for (int depth = 1; depth <= maxDepth; depth++) {
        if (stopRequested.load(std::memory_order_relaxed)) break;
        
        auto iterStart = std::chrono::high_resolution_clock::now();
        
        uint64_t posHash = rootHash(pos);
        int score;
        
        // ================================================================
        // ASPIRATION WINDOWS: Use narrow window around previous score
        // for depths > 1. Re-search with wider window on fail.
        // ================================================================
        if (depth >= 4 && !stopRequested.load(std::memory_order_relaxed)) {
            int delta = ASPIRATION_WINDOW;
            int aspAlpha = prevScore - delta;
            int aspBeta = prevScore + delta;
            
            // Progressive aspiration window widening
            while (true) {
                score = negamax(pos, depth, aspAlpha, aspBeta, 0, posHash, true);
                
                if (stopRequested.load(std::memory_order_relaxed)) break;
                
                if (score <= aspAlpha) {
                    // Fail low: widen alpha side
                    aspAlpha = std::max(aspAlpha - delta, -200000);
                    delta *= 2;
                } else if (score >= aspBeta) {
                    // Fail high: widen beta side
                    aspBeta = std::min(aspBeta + delta, 200000);
                    delta *= 2;
                } else {
                    break; // Score within window
                }
                
                // Failsafe: if window has grown too large, use full window
                if (delta > 5000) {
                    score = negamax(pos, depth, -200000, 200000, 0, posHash, true);
                    break;
                }
            }
        } else {
            score = negamax(pos, depth, -200000, 200000, 0, posHash, true);
        }
        
        if (stopRequested.load(std::memory_order_relaxed)) break;
        
        TTEntry* entry = tt.probe(posHash);
        if (entry && entry->getBestFrom().isValid()) {
            bestFrom = entry->getBestFrom();
            bestTo = entry->getBestTo();
            bestScore = score;
        }
        
        prevScore = score;
        completedDepth = depth;
        
        auto iterEnd = std::chrono::high_resolution_clock::now();
        auto iterMs = std::chrono::duration_cast<std::chrono::milliseconds>(iterEnd - iterStart).count();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(iterEnd - searchStartTime).count();
        
        int displayScore = (pos->getCurrentTurn() == PieceColor::WHITE) ? score : -score;
        
        char files[] = "abcdefgh";
        uint64_t nodes = nodesSearched.load(std::memory_order_relaxed);
        uint64_t qnodes = qNodesSearched.load(std::memory_order_relaxed);
        std::cout << "  depth " << std::setw(2) << depth 
                  << "  score " << std::setw(7) << displayScore
                  << "  nodes " << std::setw(10) << nodes
                  << "  qnodes " << std::setw(10) << qnodes
                  << "  time " << std::setw(6) << iterMs << "ms";
        if (bestFrom.isValid()) {
            std::cout << "  pv " << files[bestFrom.col] << (8 - bestFrom.row) 
                      << files[bestTo.col] << (8 - bestTo.row);
        }
        std::cout << "\n";
        
        // Time management: if we've used more than half our time, don't start next depth
        if (timeLimitMs > 0 && totalMs * 2 >= timeLimitMs) {
            std::cout << "  [Time management] Used " << totalMs << "ms / " << timeLimitMs << "ms, stopping.\n";
            break;
        }
    }
    
    // Stop SMP helper threads
    stopRequested.store(true);
    for (auto& t : smpThreads) {
        if (t.joinable()) t.join();
    }
    smpThreads.clear();
    
    if (s_hashCheck) {
        std::cerr << "[hashcheck] mismatches "
                  << s_hashMismatch.load() << " / " << s_hashChecked.load()
                  << " (improved=" << (improved?1:0) << ")\n";
    }

    result.bestMoveFrom = bestFrom;
    result.bestMoveTo = bestTo;
    result.score = (pos->getCurrentTurn() == PieceColor::WHITE) ? bestScore : -bestScore;
    result.depth = completedDepth;
    result.nodesSearched = nodesSearched.load(std::memory_order_relaxed);
    result.ttHits = tt.getHits();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - searchStartTime);
    
    std::cout << "\n========== AI SEARCH COMPLETE ==========\n";
    std::cout << "Best move: ";
    if (bestFrom.isValid() && bestTo.isValid()) {
        char files[] = "abcdefgh";
        std::cout << files[bestFrom.col] << (8 - bestFrom.row) << " -> "
                  << files[bestTo.col] << (8 - bestTo.row) << "\n";
    } else {
        std::cout << "NONE\n";
    }
    std::cout << "Depth reached: " << completedDepth << "\n";
    std::cout << "Evaluation: " << result.score << " centipawns\n";
    uint64_t finalNodes = nodesSearched.load(std::memory_order_relaxed);
    uint64_t finalQNodes = qNodesSearched.load(std::memory_order_relaxed);
    std::cout << "Main nodes:  " << finalNodes << "\n";
    std::cout << "Qsearch nodes: " << finalQNodes << "\n";
    std::cout << "Total nodes: " << (finalNodes + finalQNodes) << "\n";
    std::cout << "TT hits: " << tt.getHits() << " / " << tt.getProbes() << " probes\n";
    std::cout << "Time taken: " << duration.count() << " ms\n";
    uint64_t totalNodes = finalNodes + finalQNodes;
    double nps = totalNodes * 1000.0 / std::max((long long)1, (long long)duration.count());
    std::cout << "Nodes/second: " << std::fixed << std::setprecision(0) << nps << "\n";
    std::cout << "Threads used: " << numThreads << "\n";
    std::cout << "========================================\n\n";
}

// ============================================================
// LAZY SMP HELPER THREAD
// Each helper searches with a slightly different depth pattern
// to diversify and populate the shared TT.
// ============================================================
void Search::smpHelperSearch(Board* pos, int maxDepth, int threadId) {
    // Each helper gets its own board copy
    std::unique_ptr<Board> helperBoard(pos->copyBoard());
    Board* hp = helperBoard.get();
    
    // Per-thread heuristic tables (local - doesn't affect main thread's)
    // Helpers just populate the shared TT
    
    uint64_t posHash = rootHash(hp);
    
    // Odd threads start at depth 1, even threads start at depth 2
    // This creates depth diversity among helpers
    int startDepth = (threadId % 2 == 0) ? 2 : 1;
    
    for (int depth = startDepth; depth <= maxDepth; depth++) {
        if (stopRequested.load(std::memory_order_relaxed)) break;
        
        negamax(hp, depth, -200000, 200000, 0, posHash, true);
    }
}

void Search::startSearch(int maxDepth, std::function<void(SearchResult)> callback) {
    if (searching.load()) return;
    
    stopSearch();
    
    searching.store(true);
    stopRequested.store(false);
    timeLimitMs = 0; // No time limit for depth-based search
    
    searchThread = std::thread([this, maxDepth, callback]() {
        SearchResult result;
        searchMoves(board, maxDepth, result);
        searching.store(false);
        
        // Deliver result if we found a valid move
        if (callback && result.bestMoveFrom.isValid() && result.bestMoveTo.isValid()) {
            callback(result);
        }
    });
    
    searchThread.detach();
}

void Search::startSearchTimed(int timeMs, std::function<void(SearchResult)> callback) {
    if (searching.load()) return;
    
    stopSearch();
    
    searching.store(true);
    stopRequested.store(false);
    timeLimitMs = timeMs;
    
    // With time management, use a very high max depth - time limit will stop us
    int maxDepth = 64;
    
    searchThread = std::thread([this, maxDepth, callback]() {
        SearchResult result;
        searchMoves(board, maxDepth, result);
        searching.store(false);
        
        // Deliver result if we found a valid move
        if (callback && result.bestMoveFrom.isValid() && result.bestMoveTo.isValid()) {
            callback(result);
        }
    });
    
    searchThread.detach();
}

void Search::stopSearch() {
    if (searching.load()) {
        stopRequested.store(true);
        // SMP helper threads are joined inside searchMoves — just wait for main thread
        int timeout = 2000;
        while (searching.load() && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            timeout -= 10;
        }
    }
}

SearchResult Search::getBestMove(int maxDepth) {
    timeLimitMs = 0;
    SearchResult result;
    searchMoves(board, maxDepth, result);
    return result;
}

SearchResult Search::getBestMoveTimed(int timeMs) {
    timeLimitMs = timeMs;
    SearchResult result;
    searchMoves(board, 64, result);
    return result;
}
