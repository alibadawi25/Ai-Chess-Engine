#ifndef AI_PLAYER_H
#define AI_PLAYER_H

#include "search/Search.h"
#include "search/OpeningBook.h"
#include "core/Board.h"
#include <functional>
#include <atomic>

enum class AIStrength {
    RANDOM = 0,    // Random moves
    EASY = 3,      // Depth 3
    MEDIUM = 6,    // Depth 6
    HARD = 0xF0,   // Time-based: 3 seconds
    EXPERT = 0xF1  // Time-based: 8 seconds
};

class AIPlayer {
private:
    Search* search;
    OpeningBook* book;
    Board* board;
    AIStrength strength;
    std::atomic<bool> thinking;
    bool useBook;  // Whether to consult opening book
    
public:
    AIPlayer(Board* boardPtr, AIStrength level = AIStrength::MEDIUM);
    ~AIPlayer();
    
    // Request AI to make a move (ASYNC - doesn't block!)
    // Callback receives (from, to) positions when move is ready
    void requestMove(std::function<void(Position, Position)> callback);
    
    // Check if AI is currently thinking
    bool isThinking() const { return thinking.load(); }
    
    // Stop any ongoing thinking
    void stopThinking();
    
    // Change AI strength
    void setStrength(AIStrength level) { strength = level; }
    AIStrength getStrength() const { return strength; }
    
    // Re-enable opening book (call on new game)
    void resetBook() { useBook = true; }
};

#endif
