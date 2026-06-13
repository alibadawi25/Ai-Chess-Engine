// ============================================================
// UCI front-end for the AI Chess Engine.
//
// Speaks the Universal Chess Interface on stdin/stdout so the engine can be
// used by any UCI GUI (Arena, Cute Chess, BanksiaGUI, En Croissant, Nibbler)
// and by the lichess-bot bridge to play online and earn a rating.
//
// The engine's internal search logging goes to std::cout, which would corrupt
// the protocol — so std::cout is muted and all UCI replies are written through
// a saved handle to the real stdout.
//
// Build:  bash tools/build_uci.sh   ->  build_uci/chess_uci
// ============================================================
#include "core/Board.h"
#include "core/Piece.h"
#include "search/Search.h"
#include "search/OpeningBook.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <cctype>

static const char* ENGINE_NAME   = "AI Powers";
static const char* ENGINE_AUTHOR = "alibadawi25";

// Real stdout (engine logging is muted on std::cout to keep the protocol clean).
static std::ostream* g_out = nullptr;
static inline void send(const std::string& s) { (*g_out) << s << "\n"; g_out->flush(); }

// ---- move <-> string ---------------------------------------------------
static Position uciToPos(const std::string& sq) {
    return Position(8 - (sq[1] - '0'), sq[0] - 'a');
}
static std::string moveToUci(const Board& b, Position from, Position to) {
    std::string s;
    s += char('a' + from.col); s += char('0' + (8 - from.row));
    s += char('a' + to.col);   s += char('0' + (8 - to.row));
    // Engine auto-promotes to queen; advertise that in the move string.
    Piece* p = b.getPiece(from);
    if (p && p->getType() == PieceType::PAWN && (to.row == 0 || to.row == 7))
        s += 'q';
    return s;
}

// Apply a list of UCI move tokens to the board (promotion suffix ignored —
// the engine queens automatically).
static void applyMoves(Board& b, std::istringstream& iss) {
    std::string mv;
    while (iss >> mv) {
        if (mv.size() < 4) continue;
        b.movePiece(uciToPos(mv.substr(0, 2)), uciToPos(mv.substr(2, 2)));
    }
}

// ---- "go" parameters ---------------------------------------------------
struct GoParams {
    int movetime = 0, depth = 0;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    bool infinite = false;
};

static int pickMoveTimeMs(const GoParams& g, PieceColor side) {
    if (g.movetime > 0) return g.movetime;
    int t = (side == PieceColor::WHITE) ? g.wtime : g.btime;
    int inc = (side == PieceColor::WHITE) ? g.winc : g.binc;
    if (t > 0) {
        int movesToGo = (g.movestogo > 0) ? g.movestogo : 30;
        int budget = t / movesToGo + (int)(inc * 0.7);
        // Never spend more than ~40% of the remaining clock on one move.
        budget = std::min(budget, t * 2 / 5);
        return std::max(20, budget);
    }
    return 0; // no time info
}

int main() {
    // Mute engine logging on std::cout; keep a handle to the real stdout.
    std::ostream realOut(std::cout.rdbuf());
    g_out = &realOut;
    std::cout.rdbuf(nullptr);   // engine's internal logging goes nowhere

    Board board;
    board.initialize();
    int ttMB = 256;
    Search* search = new Search(&board, ttMB);
    search->setThreadCount(std::max(1, (int)std::thread::hardware_concurrency()));
    search->setImproved(true);   // ship the strongest configuration

    OpeningBook book;
    bool haveBook = book.loadBook("resources/Book.txt"); // optional; ignored if missing
    bool useBook = true;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "uci") {
            send(std::string("id name ") + ENGINE_NAME);
            send(std::string("id author ") + ENGINE_AUTHOR);
            send("option name Hash type spin default 256 min 1 max 4096");
            send("option name Threads type spin default 1 min 1 max 16");
            send("option name OwnBook type check default true");
            send("uciok");
        }
        else if (cmd == "isready") {
            send("readyok");
        }
        else if (cmd == "setoption") {
            // setoption name <Name> value <Value>
            std::string tok, name, value;
            while (iss >> tok) {
                if (tok == "name") { iss >> name; }
                else if (tok == "value") { iss >> value; }
            }
            if (name == "Threads" && !value.empty())
                search->setThreadCount(std::stoi(value));
            else if (name == "Hash" && !value.empty()) {
                ttMB = std::stoi(value);
                delete search;
                search = new Search(&board, ttMB);
                search->setImproved(true);
            }
            else if (name == "OwnBook" && !value.empty())
                useBook = (value == "true" || value == "1");
        }
        else if (cmd == "ucinewgame") {
            board.initialize();
            useBook = haveBook;
        }
        else if (cmd == "position") {
            std::string sub; iss >> sub;
            if (sub == "startpos") {
                board.initialize();
                std::string mv; iss >> mv; // consume optional "moves"
                if (mv == "moves") applyMoves(board, iss);
            } else if (sub == "fen") {
                std::string fen, part; int parts = 0;
                while (parts < 6 && iss >> part) {
                    if (part == "moves") break;
                    fen += (parts ? " " : "") + part; parts++;
                }
                board.loadFEN(fen);
                if (part == "moves") applyMoves(board, iss);
                else { std::string mv; if (iss >> mv && mv == "moves") applyMoves(board, iss); }
            }
        }
        else if (cmd == "go") {
            GoParams g;
            std::string t;
            while (iss >> t) {
                if      (t == "movetime")  iss >> g.movetime;
                else if (t == "depth")     iss >> g.depth;
                else if (t == "wtime")     iss >> g.wtime;
                else if (t == "btime")     iss >> g.btime;
                else if (t == "winc")      iss >> g.winc;
                else if (t == "binc")      iss >> g.binc;
                else if (t == "movestogo") iss >> g.movestogo;
                else if (t == "infinite")  g.infinite = true;
            }

            // Opening book first (fast, varied, strong early play).
            Position bf, bt;
            if (useBook && haveBook && book.probeBook(&board, bf, bt)) {
                send("info string book move");
                send("bestmove " + moveToUci(board, bf, bt));
                continue;
            }

            SearchResult r;
            int mt = pickMoveTimeMs(g, board.getCurrentTurn());
            if (g.depth > 0)        r = search->getBestMove(g.depth);
            else if (mt > 0)        r = search->getBestMoveTimed(mt);
            else if (g.infinite)    r = search->getBestMoveTimed(10000);
            else                    r = search->getBestMove(8); // sane default

            if (r.bestMoveFrom.isValid() && r.bestMoveTo.isValid()) {
                std::ostringstream info;
                info << "info depth " << r.depth << " score cp " << r.score
                     << " nodes " << r.nodesSearched
                     << " pv " << moveToUci(board, r.bestMoveFrom, r.bestMoveTo);
                send(info.str());
                send("bestmove " + moveToUci(board, r.bestMoveFrom, r.bestMoveTo));
            } else {
                send("bestmove 0000"); // no legal move
            }
        }
        else if (cmd == "stop") {
            search->stopSearch();
        }
        else if (cmd == "quit") {
            break;
        }
        // unknown commands are ignored per UCI spec
    }

    delete search;
    return 0;
}
