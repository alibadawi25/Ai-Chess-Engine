#ifndef SEARCH_H
#define SEARCH_H

#include "core/Board.h"
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include <vector>
#include <cstdint>
#include <cstring>

// ============================================================
// ZOBRIST HASHING
// ============================================================
class ZobristHash {
private:
    uint64_t pieceKeys[6][2][64];  // [pieceType][color][square]
    uint64_t sideToMoveKey;
    uint64_t castlingKeys[16];     // keyed by 4-bit castling-rights mask
    uint64_t epFileKeys[8];        // keyed by en-passant file

public:
    ZobristHash();
    uint64_t computeHash(const Board* board) const;
    uint64_t getPieceKey(int type, int color, int square) const { return pieceKeys[type][color][square]; }
    uint64_t getSideKey() const { return sideToMoveKey; }
    uint64_t getCastlingKey(int mask) const { return castlingKeys[mask & 0xF]; }
    uint64_t getEpFileKey(int file) const { return epFileKeys[file & 7]; }
};

// ============================================================
// TRANSPOSITION TABLE
// ============================================================
enum class TTFlag : uint8_t {
    EXACT = 0,
    ALPHA = 1,   // Upper bound (failed low)
    BETA  = 2    // Lower bound (failed high)
};

// Packed TT entry: exactly 16 bytes (4 entries per cache line)
struct alignas(16) TTEntry {
    uint32_t hashKey;      // Upper 32 bits of Zobrist (index provides lower bits)
    int32_t  score;        // Centipawns (supports ±2 billion, plenty for mate scores)
    uint8_t  depth;        // Search depth (0-255)
    uint8_t  flagAndGen;   // bits 0-1: TTFlag, bits 2-7: generation (64 generations)
    uint8_t  bestFromSq;   // row*8+col (0-63), 0xFF = invalid
    uint8_t  bestToSq;     // row*8+col (0-63), 0xFF = invalid
    uint8_t  padding[4];   // Pad to 16 bytes

    TTEntry() : hashKey(0), score(0), depth(0), flagAndGen(0),
                bestFromSq(0xFF), bestToSq(0xFF) { padding[0]=padding[1]=padding[2]=padding[3]=0; }

    // Accessors
    TTFlag   getFlag() const       { return static_cast<TTFlag>(flagAndGen & 0x03); }
    uint8_t  getGeneration() const { return flagAndGen >> 2; }
    void     setFlagAndGen(TTFlag f, uint8_t gen) {
        flagAndGen = static_cast<uint8_t>(f) | (gen << 2);
    }

    Position getBestFrom() const {
        return (bestFromSq == 0xFF) ? Position(-1, -1) : Position(bestFromSq / 8, bestFromSq % 8);
    }
    Position getBestTo() const {
        return (bestToSq == 0xFF) ? Position(-1, -1) : Position(bestToSq / 8, bestToSq % 8);
    }
    static uint8_t posToSq(const Position& p) {
        return p.isValid() ? (uint8_t)(p.row * 8 + p.col) : 0xFF;
    }
};

class TranspositionTable {
private:
    std::vector<TTEntry> table;
    size_t numEntries;
    size_t mask;          // numEntries - 1 for power-of-2 indexing
    uint64_t hits;
    uint64_t probes;
    uint8_t currentGeneration;  // Only lower 6 bits used (matches flagAndGen encoding)

public:
    TranspositionTable(size_t sizeInMB = 512);
    void store(uint64_t hash, int score, int depth, TTFlag flag, Position bestFrom, Position bestTo);
    TTEntry* probe(uint64_t hash);
    void clear();
    void newGeneration() { currentGeneration = (currentGeneration + 1) & 0x3F; }
    void prefetch(uint64_t hash) const {
        #ifdef __GNUC__
        __builtin_prefetch(&table[hash & mask], 0, 1);
        #endif
    }
    uint64_t getHits() const { return hits; }
    uint64_t getProbes() const { return probes; }
};

// ============================================================
// SCORED MOVE (for move ordering)
// ============================================================
struct ScoredMove {
    Position from;
    Position to;
    int score;
    
    ScoredMove() : from(-1, -1), to(-1, -1), score(0) {}
    ScoredMove(Position f, Position t, int s) : from(f), to(t), score(s) {}
    bool operator>(const ScoredMove& other) const { return score > other.score; }
};

// ============================================================
// SEARCH RESULT
// ============================================================
struct SearchResult {
    Position bestMoveFrom;
    Position bestMoveTo;
    int score;
    int depth;
    uint64_t nodesSearched;
    uint64_t ttHits;
    
    SearchResult() : bestMoveFrom(-1, -1), bestMoveTo(-1, -1), 
                     score(0), depth(0), nodesSearched(0), ttHits(0) {}
};

// ============================================================
// PIECE-SQUARE TABLES (from White's perspective, row 0 = rank 8)
// ============================================================
namespace PST {
    constexpr int PAWN_MG[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    };
    constexpr int PAWN_EG[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
        80, 80, 80, 80, 80, 80, 80, 80,
        50, 50, 50, 50, 50, 50, 50, 50,
        30, 30, 30, 30, 30, 30, 30, 30,
        20, 20, 20, 20, 20, 20, 20, 20,
        10, 10, 10, 10, 10, 10, 10, 10,
        10, 10, 10, 10, 10, 10, 10, 10,
         0,  0,  0,  0,  0,  0,  0,  0
    };
    constexpr int KNIGHT_TABLE[64] = {
        -50,-40,-30,-30,-30,-30,-40,-50,
        -40,-20,  0,  0,  0,  0,-20,-40,
        -30,  0, 10, 15, 15, 10,  0,-30,
        -30,  5, 15, 20, 20, 15,  5,-30,
        -30,  0, 15, 20, 20, 15,  0,-30,
        -30,  5, 10, 15, 15, 10,  5,-30,
        -40,-20,  0,  5,  5,  0,-20,-40,
        -50,-40,-30,-30,-30,-30,-40,-50
    };
    constexpr int BISHOP_TABLE[64] = {
        -20,-10,-10,-10,-10,-10,-10,-20,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -10,  0,  5, 10, 10,  5,  0,-10,
        -10,  5,  5, 10, 10,  5,  5,-10,
        -10,  0, 10, 10, 10, 10,  0,-10,
        -10, 10, 10, 10, 10, 10, 10,-10,
        -10,  5,  0,  0,  0,  0,  5,-10,
        -20,-10,-10,-10,-10,-10,-10,-20
    };
    constexpr int ROOK_TABLE[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10, 10, 10, 10, 10,  5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         0,  0,  0,  5,  5,  0,  0,  0
    };
    constexpr int QUEEN_TABLE[64] = {
        -20,-10,-10, -5, -5,-10,-10,-20,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -10,  0,  5,  5,  5,  5,  0,-10,
         -5,  0,  5,  5,  5,  5,  0, -5,
          0,  0,  5,  5,  5,  5,  0, -5,
        -10,  5,  5,  5,  5,  5,  0,-10,
        -10,  0,  5,  0,  0,  0,  0,-10,
        -20,-10,-10, -5, -5,-10,-10,-20
    };
    constexpr int KING_MG[64] = {
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -20,-30,-30,-40,-40,-30,-30,-20,
        -10,-20,-20,-20,-20,-20,-20,-10,
         20, 20,  0,  0,  0,  0, 20, 20,
         20, 30, 10,  0,  0, 10, 30, 20
    };
    constexpr int KING_EG[64] = {
        -50,-40,-30,-20,-20,-30,-40,-50,
        -30,-20,-10,  0,  0,-10,-20,-30,
        -30,-10, 20, 30, 30, 20,-10,-30,
        -30,-10, 30, 40, 40, 30,-10,-30,
        -30,-10, 30, 40, 40, 30,-10,-30,
        -30,-10, 20, 30, 30, 20,-10,-30,
        -30,-30,  0,  0,  0,  0,-30,-30,
        -50,-30,-30,-30,-30,-30,-30,-50
    };
    
    // Middlegame PST
    inline int getMG(PieceType type, PieceColor color, int row, int col) {
        int idx = (color == PieceColor::WHITE) ? (row * 8 + col) : ((7 - row) * 8 + col);
        switch (type) {
            case PieceType::PAWN:   return PAWN_MG[idx];
            case PieceType::KNIGHT: return KNIGHT_TABLE[idx];
            case PieceType::BISHOP: return BISHOP_TABLE[idx];
            case PieceType::ROOK:   return ROOK_TABLE[idx];
            case PieceType::QUEEN:  return QUEEN_TABLE[idx];
            case PieceType::KING:   return KING_MG[idx];
            default: return 0;
        }
    }
    
    // Endgame PST (only differs for pawns and king)
    inline int getEG(PieceType type, PieceColor color, int row, int col) {
        int idx = (color == PieceColor::WHITE) ? (row * 8 + col) : ((7 - row) * 8 + col);
        switch (type) {
            case PieceType::PAWN:   return PAWN_EG[idx];
            case PieceType::KNIGHT: return KNIGHT_TABLE[idx];
            case PieceType::BISHOP: return BISHOP_TABLE[idx];
            case PieceType::ROOK:   return ROOK_TABLE[idx];
            case PieceType::QUEEN:  return QUEEN_TABLE[idx];
            case PieceType::KING:   return KING_EG[idx];
            default: return 0;
        }
    }
}

// ============================================================
// SEARCH ENGINE (with Lazy SMP + Time Management)
// ============================================================
class Search {
private:
    std::atomic<bool> searching;
    std::atomic<bool> stopRequested;
    std::thread searchThread;
    
    Board* board;
    std::atomic<uint64_t> nodesSearched;
    std::atomic<uint64_t> qNodesSearched;
    
    // Time management
    std::chrono::high_resolution_clock::time_point searchStartTime;
    int timeLimitMs;       // 0 = no time limit (use maxDepth only)
    uint64_t nodeLimit = 0; // 0 = no node limit (fixed-nodes search when > 0)
    bool checkTimeLimit(); // Returns true if time is up
    
    // Zobrist hashing & transposition table (shared across SMP threads)
    ZobristHash zobrist;
    TranspositionTable tt;
    
    // Lazy SMP: number of helper threads
    int numThreads;
    std::vector<std::thread> smpThreads;
    
    // Per-thread heuristics (thread 0 = main thread)
    static constexpr int MAX_THREADS = 16;
    
    // Killer moves [ply][slot] - quiet moves that caused beta cutoffs
    static constexpr int MAX_PLY = 64;
    Position killerFrom[MAX_PLY][2];
    Position killerTo[MAX_PLY][2];
    
    // History heuristic [from_sq][to_sq]
    int historyTable[64][64];
    
    // Countermove heuristic [previous_from_sq][previous_to_sq] -> refutation move
    Position countermoveFrom[64][64];
    Position countermoveTo[64][64];
    
    // Tapered evaluation: material + PST + pawn structure + king safety + mobility
    int evaluatePosition(Board* pos);
    
    // Game phase (0 = endgame, 256 = opening) based on material
    int computeGamePhase(Board* pos);
    
    // Pawn structure evaluation
    int evaluatePawnStructure(Board* pos, PieceColor color);
    
    // King safety evaluation
    int evaluateKingSafety(Board* pos, PieceColor color);
    
    // Piece mobility (centrality proxy)
    int evaluateMobility(Board* pos, PieceColor color);
    
    // Negamax with alpha-beta + NMP + LMR + Futility + PVS
    int negamax(Board* pos, int depth, int alpha, int beta, int ply, 
                uint64_t posHash, bool allowNullMove,
                Position prevFrom = Position(-1, -1), Position prevTo = Position(-1, -1),
                Position excludeFrom = Position(-1, -1), Position excludeTo = Position(-1, -1));
    
    // Quiescence search - search captures until quiet
    int quiescence(Board* pos, int alpha, int beta, int ply);
    
    // Move ordering — fills caller-provided stack buffer, returns count
    int generateOrderedMoves(Board* pos, int ply,
                             Position ttFrom, Position ttTo,
                             bool capturesOnly,
                             Position prevFrom, Position prevTo,
                             ScoredMove* outMoves);
    int scoreMove(Board* pos, Position from, Position to, int ply,
                  Position ttFrom, Position ttTo,
                  Position prevFrom = Position(-1, -1),
                  Position prevTo = Position(-1, -1));
    
    // Iterative deepening search (main thread)
    void searchMoves(Board* pos, int maxDepth, SearchResult& result);
    
    // Lazy SMP helper thread function
    void smpHelperSearch(Board* pos, int maxDepth, int threadId);
    
    // Store killer move
    void storeKiller(int ply, Position from, Position to);
    
    // Store countermove
    void storeCountermove(Position prevFrom, Position prevTo, Position from, Position to);
    
    // Clear heuristics for new search
    void clearHeuristics();
    
    // Piece value for MVV-LVA
    static int pieceValue(PieceType t);
    
    // Static Exchange Evaluation — returns net material gain for the capturer
    int see(Board* pos, Position from, Position to);
    
    // Check if position is likely a drawn endgame
    bool isLikelyDraw(Board* pos);
    
    // Compute incremental hash for a move (fast XOR update)
    uint64_t hashAfterMove(uint64_t hash, Board* pos, Position from, Position to) const;

    // IMPROVED: castling-rights + en-passant aware hashing.
    // boardRights() derives the 4-bit castling mask; castlingEpContribution()
    // returns the castling+EP component; rootHash() is the full position hash
    // (pieces+side, plus castling/EP when `improved`).
    static int boardRights(const Board* b);
    uint64_t castlingEpContribution(const Board* b) const;
    uint64_t rootHash(const Board* b) const;
    
    // Knight outpost evaluation
    int evaluateKnightOutposts(Board* pos, PieceColor color);
    
public:
    Search(Board* boardPtr, int ttSizeMB = 512);
    ~Search();

    // A/B testing flag: when true, enables experimental strength improvements.
    // Lets a single binary play "baseline" vs "improved" engines head-to-head.
    bool improved = false;
    void setImproved(bool b) { improved = b; }
    
    void startSearch(int maxDepth, std::function<void(SearchResult)> callback);
    void startSearchTimed(int timeLimitMs, std::function<void(SearchResult)> callback);
    void stopSearch();
    bool isSearching() const { return searching.load(); }
    SearchResult getBestMove(int maxDepth);
    SearchResult getBestMoveTimed(int timeLimitMs);
    SearchResult getBestMoveNodes(uint64_t maxNodes);
    void setThreadCount(int n) { numThreads = std::max(1, std::min(n, MAX_THREADS)); }
    int getThreadCount() const { return numThreads; }
};

#endif
