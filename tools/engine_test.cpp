// ============================================================
// Headless engine test harness (Linux) for the AI Chess Engine.
//
//   perft <depth>            - move-generation correctness check
//   bench <depth>            - single-thread node/second benchmark
//   selfplay <depth> <games> - baseline vs improved match, prints W/D/L + Elo
//
// Only the portable engine core (core/, search/) is used; the Windows GUI
// is not compiled. Self-play pits two Search instances (baseline vs the
// `improved` code paths) against each other from a set of opening lines,
// each played with both colors, and reports an Elo estimate.
// ============================================================
#include "core/Board.h"
#include "core/Piece.h"
#include "search/Search.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <unordered_map>

// ---- cout silencer (the engine prints verbose search logs) -------------
struct CoutMute {
    std::streambuf* old;
    CoutMute()  { old = std::cout.rdbuf(nullptr); }
    ~CoutMute() { std::cout.rdbuf(old); }
};

// ---- UCI-ish coordinate <-> Position helpers ---------------------------
static Position uciToPos(const std::string& sq) {
    int col = sq[0] - 'a';
    int row = 8 - (sq[1] - '0');
    return Position(row, col);
}
static std::string posToUci(Position p) {
    std::string s;
    s += char('a' + p.col);
    s += char('0' + (8 - p.row));
    return s;
}

// ---- perft -------------------------------------------------------------
// Counts leaf nodes using the same pseudo-legal generation + legality test
// the search relies on (auto-queen promotion, so deep promotion-heavy
// positions undercount; startpos perft(1..5) is unaffected).
static uint64_t perft(Board* b, int depth) {
    if (depth == 0) return 1;
    uint64_t nodes = 0;
    PieceColor side = b->getCurrentTurn();
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* p = b->getPiece(r, c);
            if (!p || p->getColor() != side) continue;
            Position from(r, c);
            std::vector<Position> moves = b->getPseudoLegalMoves(from);
            for (Position to : moves) {
                UndoInfo u = b->makeMove(from, to);
                PieceColor us = (b->getCurrentTurn() == PieceColor::WHITE)
                                ? PieceColor::BLACK : PieceColor::WHITE;
                if (!b->isKingInCheck(us))
                    nodes += perft(b, depth - 1);
                b->unmakeMove(u);
            }
        }
    }
    return nodes;
}

static int cmd_perft(int depth) {
    Board b; b.initialize();
    const uint64_t expected[] = {1,20,400,8902,197281,4865609,119060324};
    std::cout << "Perft from startpos (auto-queen promotion):\n";
    for (int d = 1; d <= depth; d++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t n = perft(&b, d);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  depth " << d << ": " << n;
        if (d <= 6) {
            std::cout << (n == expected[d] ? "  [OK]" : "  [MISMATCH expected "
                          + std::to_string(expected[d]) + "]");
        }
        std::cout << "   (" << ms << " ms)\n";
    }
    return 0;
}

// ---- bench -------------------------------------------------------------
static int cmd_bench(int depth, bool improved) {
    Board b; b.initialize();
    Search s(&b, 256);
    s.setThreadCount(1);
    s.setImproved(improved);
    SearchResult r;
    auto t0 = std::chrono::high_resolution_clock::now();
    { CoutMute m; r = s.getBestMove(depth); }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double nps = r.nodesSearched * 1000.0 / std::max(1.0, ms);
    std::cout << "Bench depth " << depth << " (startpos, 1 thread, "
              << (improved?"IMPROVED":"BASELINE") << "):\n";
    std::cout << "  best   " << posToUci(r.bestMoveFrom) << posToUci(r.bestMoveTo) << "\n";
    std::cout << "  score  " << r.score << " cp\n";
    std::cout << "  nodes  " << r.nodesSearched << "\n";
    std::cout << "  time   " << (long long)ms << " ms\n";
    std::cout << "  nps    " << (long long)nps << "\n";
    return 0;
}

// ---- self-play ---------------------------------------------------------
static const char* OPENINGS[] = {
    "",                                  // start position
    "e2e4 e7e5",
    "e2e4 c7c5",
    "e2e4 e7e6",
    "e2e4 c7c6",
    "d2d4 d7d5",
    "d2d4 g8f6",
    "c2c4 e7e5",
    "g1f3 d7d5",
    "e2e4 d7d5",
    "d2d4 f7f5",
    "e2e4 g8f6",
    "c2c4 c7c5",
    "d2d4 e7e6 c2c4 f8b4",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6",
    "d2d4 d7d5 c2c4 c7c6",
};

// ---- depth benchmark: how deep in a given time budget --------------------
static void applyLine(Board& b, const std::string& line) {
    std::istringstream iss(line); std::string mv;
    while (iss >> mv) { CoutMute m; b.movePiece(uciToPos(mv.substr(0,2)), uciToPos(mv.substr(2,2))); }
}
static int cmd_depthbench(int ms) {
    struct Pos { const char* name; const char* line; };
    Pos positions[] = {
        { "startpos", "" },
        { "Ruy Lopez middlegame", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8" },
    };
    int threadOpts[] = { 1, 4 };
    std::cout << "Depth reached in " << ms << " ms/move (Lazy SMP):\n\n";
    for (auto& P : positions) {
        std::cout << "  " << P.name << ":\n";
        for (int improved = 0; improved <= 1; improved++) {
            for (int th : threadOpts) {
                Board b; b.initialize(); applyLine(b, P.line);
                Search s(&b, 256);
                s.setThreadCount(th);
                s.setImproved(improved != 0);
                SearchResult r;
                { CoutMute m; r = s.getBestMoveTimed(ms); }
                std::cout << "    " << (improved?"IMPROVED":"BASELINE")
                          << "  " << th << " thread" << (th>1?"s":" ")
                          << "  ->  depth " << r.depth
                          << "   (" << r.nodesSearched << " main nodes, best "
                          << posToUci(r.bestMoveFrom) << posToUci(r.bestMoveTo) << ")\n";
            }
        }
        std::cout << "\n";
    }
    return 0;
}

static int cmd_selfplay(int depth, int games);

int g_lastPlies = 0; const char* g_lastReason = "?";
bool g_useTime = false; bool g_useNodes = false;
int g_budget = 5;  // g_budget = depth (plies) | time (ms) | node count
// Returns: 1 = white mated black, -1 = black mated white, 0 = draw.
static int playGame(const std::string& opening, int depth,
                    Search& whiteEng, Search& blackEng, Board& b) {
    b.initialize();
    // Apply opening line (assumed legal).
    std::istringstream iss(opening);
    std::string mv;
    while (iss >> mv) {
        Position from = uciToPos(mv.substr(0,2));
        Position to   = uciToPos(mv.substr(2,2));
        CoutMute m; b.movePiece(from, to);
    }

    std::unordered_map<std::string,int> seen;   // threefold by position
    int halfmove = 0;                            // 50-move (plies w/o pawn move/capture)
    const int MAX_PLIES = 400;

    extern int g_lastPlies; extern const char* g_lastReason;
    for (int ply = 0; ply < MAX_PLIES; ply++) {
        g_lastPlies = ply;
        PieceColor side = b.getCurrentTurn();
        if (!b.hasAnyLegalMoves(side)) {
            if (b.isKingInCheck(side)) { g_lastReason="mate";
                return (side == PieceColor::WHITE) ? -1 : 1; } // side to move is mated
            g_lastReason="stalemate"; return 0; // stalemate
        }
        // Draw conditions
        if (halfmove >= 100) { g_lastReason="50-move"; return 0; }
        std::string fen = b.toFEN();
        std::string posKey = fen; // full fen (no clocks) — good enough for repetition
        if (++seen[posKey] >= 3) { g_lastReason="3-fold"; return 0; }

        Search& eng = (side == PieceColor::WHITE) ? whiteEng : blackEng;
        SearchResult r;
        { CoutMute m; r = g_useNodes ? eng.getBestMoveNodes((uint64_t)g_budget)
                                     : g_useTime ? eng.getBestMoveTimed(g_budget)
                                                 : eng.getBestMove(depth); }
        if (!r.bestMoveFrom.isValid() || !r.bestMoveTo.isValid()) {
            g_lastReason = "no-move"; return 0; // safety
        }

        Piece* mover = b.getPiece(r.bestMoveFrom);
        Piece* victim = b.getPiece(r.bestMoveTo);
        bool reset = (mover && mover->getType() == PieceType::PAWN) || victim != nullptr;
        halfmove = reset ? 0 : halfmove + 1;

        bool ok;
        { CoutMute m; ok = b.movePiece(r.bestMoveFrom, r.bestMoveTo); }
        if (!ok) {
            std::cerr << "    [illegal] side=" << (side==PieceColor::WHITE?"W":"B")
                      << " move=" << posToUci(r.bestMoveFrom) << posToUci(r.bestMoveTo)
                      << " fen=" << b.toFEN() << "\n";
            g_lastReason = "illegal-move"; return 0;
        }
    }
    return 0; // ply cap => draw
}

static int cmd_selfplay(int depth, int games) {
    Board b;
    // Two engines bound to the same game board; each searches a private copy.
    Search engA(&b, 256);  // will play "improved"
    Search engB(&b, 256);  // baseline
    engA.setThreadCount(1);
    engB.setThreadCount(1);
    engA.setImproved(true);
    engB.setImproved(false);

    int nOpen = sizeof(OPENINGS)/sizeof(OPENINGS[0]);
    int impWins = 0, baseWins = 0, draws = 0;
    int played = 0;

    std::cout << "Self-play: IMPROVED vs BASELINE, "
              << (g_useNodes ? (std::to_string(g_budget)+" nodes/move")
                 : g_useTime ? (std::to_string(g_budget)+"ms/move")
                             : ("depth "+std::to_string(depth)))
              << ", up to " << games << " games\n";
    std::cout << "Opening lines: " << nOpen << " (each played both colors)\n\n";

    for (int g = 0; g < games; g++) {
        const std::string opening = OPENINGS[(g/2) % nOpen];
        bool impIsWhite = (g % 2 == 0);
        int res;
        if (impIsWhite)
            res = playGame(opening, depth, engA, engB, b);
        else
            res = playGame(opening, depth, engB, engA, b);

        // Convert to improved-perspective: +1 improved win, -1 base win, 0 draw
        int impResult;
        if (res == 0) impResult = 0;
        else if (impIsWhite) impResult = (res == 1) ? 1 : -1;
        else impResult = (res == -1) ? 1 : -1;

        if (impResult > 0) impWins++;
        else if (impResult < 0) baseWins++;
        else draws++;
        played++;

        const char* tag = impResult>0 ? "IMPROVED" : impResult<0 ? "BASELINE" : "draw";
        std::cout << "  game " << (g+1) << "/" << games
                  << "  imp=" << (impIsWhite?"W":"B")
                  << "  plies=" << g_lastPlies << " (" << g_lastReason << ")"
                  << "  -> " << tag
                  << "   [I:" << impWins << " B:" << baseWins << " D:" << draws << "]\n";
        std::cout.flush();
    }

    double score = (double)(impWins + 0.5*draws) / std::max(1,played);
    double elo = 0.0;
    if (score > 0.0 && score < 1.0) elo = -400.0 * std::log10(1.0/score - 1.0);
    std::cout << "\n=== RESULT (improved perspective) ===\n";
    std::cout << "  games    " << played << "\n";
    std::cout << "  W-D-L    " << impWins << "-" << draws << "-" << baseWins << "\n";
    std::cout << "  score    " << (score*100.0) << "%\n";
    std::cout << "  Elo diff " << (elo>=0?"+":"") << (long long)elo << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: engine_test <perft|bench|selfplay> [args]\n";
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "perft")    return cmd_perft(argc>2?std::stoi(argv[2]):5);
    if (cmd == "depthbench") return cmd_depthbench(argc>2?std::stoi(argv[2]):3000);
    if (cmd == "bench")    return cmd_bench(argc>2?std::stoi(argv[2]):8,
                                            argc>3?(std::string(argv[3])=="improved"):false);
    if (cmd == "selfplay") return cmd_selfplay(argc>2?std::stoi(argv[2]):4,
                                               argc>3?std::stoi(argv[3]):32);
    if (cmd == "selfplaytime") {
        g_useTime = true;
        g_budget = argc>2?std::stoi(argv[2]):100; // ms per move
        return cmd_selfplay(64, argc>3?std::stoi(argv[3]):32);
    }
    if (cmd == "selfplaynodes") {
        g_useNodes = true;
        g_budget = argc>2?std::stoi(argv[2]):200000; // nodes per move
        return cmd_selfplay(64, argc>3?std::stoi(argv[3]):64);
    }
    std::cerr << "unknown command: " << cmd << "\n";
    return 1;
}
