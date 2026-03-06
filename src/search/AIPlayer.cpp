#include "search/AIPlayer.h"
#include <random>
#include <vector>
#include <iostream>

AIPlayer::AIPlayer(Board* boardPtr, AIStrength level) 
    : board(boardPtr), strength(level), thinking(false), useBook(true) {
    search = new Search(boardPtr);
    book = new OpeningBook();
    book->loadBook("resources/Book.txt");
}

AIPlayer::~AIPlayer() {
    stopThinking();
    delete search;
    delete book;
}

void AIPlayer::requestMove(std::function<void(Position, Position)> callback) {
    // Don't start if already thinking
    if (thinking.load()) {
        return;
    }
    
    thinking.store(true);
    
    // Try opening book first (for non-RANDOM strengths)
    if (useBook && strength != AIStrength::RANDOM) {
        Position bookFrom, bookTo;
        if (book->probeBook(board, bookFrom, bookTo)) {
            std::cout << "[AI] Using book move" << std::endl;
            thinking.store(false);
            callback(bookFrom, bookTo);
            return;
        } else {
            // Out of book - disable for rest of game
            useBook = false;
            std::cout << "[AI] Out of book, switching to search" << std::endl;
        }
    }
    
    // For random strength, just pick a random move immediately
    if (strength == AIStrength::RANDOM) {
        // Find all legal moves
        std::vector<std::pair<Position, Position>> allMoves;
        PieceColor currentColor = board->getCurrentTurn();
        
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                Piece* piece = board->getPiece(row, col);
                if (piece && piece->getColor() == currentColor) {
                    Position from(row, col);
                    std::vector<Position> moves = board->getAllValidMoves(from);
                    for (const Position& to : moves) {
                        allMoves.push_back({from, to});
                    }
                }
            }
        }
        
        // Pick random move
        if (!allMoves.empty()) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, allMoves.size() - 1);
            
            auto move = allMoves[dis(gen)];
            thinking.store(false);
            callback(move.first, move.second);
        } else {
            thinking.store(false);
        }
        return;
    }
    
    // For other strengths, use search (ASYNC!)
    int strengthValue = static_cast<int>(strength);
    
    if (strengthValue >= 0xF0) {
        // Time-based search (HARD, EXPERT)
        int timeMs = 3000; // HARD default
        if (strength == AIStrength::EXPERT) timeMs = 8000;
        
        std::cout << "[AI] Starting time-based search: " << timeMs << "ms\n";
        search->startSearchTimed(timeMs, [this, callback](SearchResult result) {
            thinking.store(false);
            if (result.bestMoveFrom.isValid() && result.bestMoveTo.isValid()) {
                callback(result.bestMoveFrom, result.bestMoveTo);
            }
        });
    } else {
        // Depth-based search (EASY, MEDIUM)
        int depth = strengthValue;
        
        search->startSearch(depth, [this, callback](SearchResult result) {
            thinking.store(false);
            
            // Call callback with best move
            if (result.bestMoveFrom.isValid() && result.bestMoveTo.isValid()) {
                callback(result.bestMoveFrom, result.bestMoveTo);
            }
        });
    }
}

void AIPlayer::stopThinking() {
    if (thinking.load()) {
        search->stopSearch();
        thinking.store(false);
    }
}
