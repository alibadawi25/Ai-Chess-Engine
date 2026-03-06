#ifndef GAME_H
#define GAME_H

#include "core/Board.h"
#include <string>

enum class GameState {
    PLAYING, CHECKMATE, STALEMATE, DRAW
};

class Game {
private:
    Board board;
    GameState state;
    int moveCount;
    
    Position parsePosition(const std::string& posStr);
    bool isValidPositionString(const std::string& posStr);
    
public:
    Game();
    
    void start();
    void play();
    bool processMove(const std::string& moveStr);
    void displayStatus();
    
    GameState getState() const { return state; }
    void setState(GameState s) { state = s; }
};

#endif
