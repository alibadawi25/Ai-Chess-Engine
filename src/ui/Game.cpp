#include "ui/Game.h"
#include <iostream>
#include <sstream>
#include <algorithm>

Game::Game() : state(GameState::PLAYING), moveCount(0) {
    board.initialize();
}

void Game::start() {
    std::cout << "========================================\n";
    std::cout << "       CHESS GAME - C++ EDITION\n";
    std::cout << "========================================\n";
    std::cout << "\nWelcome to Chess!\n";
    std::cout << "\nHow to play:\n";
    std::cout << "  - Enter moves in algebraic notation (e.g., 'e2 e4')\n";
    std::cout << "  - White pieces: P(Pawn) R(Rook) N(Knight) B(Bishop) Q(Queen) K(King)\n";
    std::cout << "  - Black pieces: p(pawn) r(rook) n(knight) b(bishop) q(queen) k(king)\n";
    std::cout << "  - Type 'quit' to exit the game\n";
    std::cout << "  - Type 'help' for move suggestions\n\n";
    
    play();
}

Position Game::parsePosition(const std::string& posStr) {
    if (posStr.length() != 2) return Position(-1, -1);
    
    char colChar = tolower(posStr[0]);
    char rowChar = posStr[1];
    
    if (colChar < 'a' || colChar > 'h' || rowChar < '1' || rowChar > '8') {
        return Position(-1, -1);
    }
    
    int col = colChar - 'a';
    int row = '8' - rowChar;  // Convert to 0-based from top
    
    return Position(row, col);
}

bool Game::isValidPositionString(const std::string& posStr) {
    Position pos = parsePosition(posStr);
    return pos.isValid();
}

bool Game::processMove(const std::string& moveStr) {
    std::istringstream iss(moveStr);
    std::string fromStr, toStr;
    
    iss >> fromStr >> toStr;
    
    if (!isValidPositionString(fromStr) || !isValidPositionString(toStr)) {
        std::cout << "Invalid move format! Use format like 'e2 e4'\n";
        return false;
    }
    
    Position from = parsePosition(fromStr);
    Position to = parsePosition(toStr);
    
    Piece* piece = board.getPiece(from);
    if (!piece) {
        std::cout << "No piece at " << fromStr << "!\n";
        return false;
    }
    
    if (piece->getColor() != board.getCurrentTurn()) {
        std::cout << "It's not your turn!\n";
        return false;
    }
    
    if (board.movePiece(from, to)) {
        moveCount++;
        
        // Check game state
        PieceColor nextColor = board.getCurrentTurn();
        
        if (board.isCheckmate(nextColor)) {
            state = GameState::CHECKMATE;
            std::cout << "\n*** CHECKMATE! ";
            std::cout << ((nextColor == PieceColor::WHITE) ? "Black" : "White");
            std::cout << " wins! ***\n";
        } else if (board.isStalemate(nextColor)) {
            state = GameState::STALEMATE;
            std::cout << "\n*** STALEMATE! It's a draw! ***\n";
        } else if (board.isKingInCheck(nextColor)) {
            std::cout << "\n*** CHECK! ***\n";
        }
        
        return true;
    } else {
        std::cout << "Invalid move! ";
        
        auto validMoves = board.getAllValidMoves(from);
        if (validMoves.empty()) {
            std::cout << "This piece has no legal moves.\n";
        } else {
            std::cout << "Try one of these positions: ";
            for (size_t i = 0; i < validMoves.size() && i < 5; i++) {
                char col = 'a' + validMoves[i].col;
                char row = '8' - validMoves[i].row;
                std::cout << col << row;
                if (i < validMoves.size() - 1 && i < 4) std::cout << ", ";
            }
            if (validMoves.size() > 5) std::cout << "...";
            std::cout << "\n";
        }
        
        return false;
    }
}

void Game::displayStatus() {
    board.display();
    
    std::cout << "Move #" << (moveCount + 1) << " - ";
    std::cout << ((board.getCurrentTurn() == PieceColor::WHITE) ? "White" : "Black");
    std::cout << " to move\n";
    
    if (board.isKingInCheck(board.getCurrentTurn())) {
        std::cout << "*** You are in CHECK! ***\n";
    }
    
    std::cout << "\n";
}

void Game::play() {
    while (state == GameState::PLAYING) {
        displayStatus();
        
        std::cout << "Enter move: ";
        std::string input;
        std::getline(std::cin, input);
        
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);
        
        // Convert to lowercase for commands
        std::string command = input;
        std::transform(command.begin(), command.end(), command.begin(), ::tolower);
        
        if (command == "quit" || command == "exit" || command == "q") {
            std::cout << "\nThanks for playing!\n";
            break;
        } else if (command == "help" || command == "h") {
            std::cout << "\nShowing all legal moves:\n";
            bool foundMoves = false;
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    Piece* piece = board.getPiece(row, col);
                    if (piece && piece->getColor() == board.getCurrentTurn()) {
                        auto moves = board.getAllValidMoves(Position(row, col));
                        if (!moves.empty()) {
                            foundMoves = true;
                            char fromCol = 'a' + col;
                            char fromRow = '8' - row;
                            std::cout << "  " << piece->getSymbol() << " at " 
                                     << fromCol << fromRow << " can move to: ";
                            for (size_t i = 0; i < moves.size(); i++) {
                                char toCol = 'a' + moves[i].col;
                                char toRow = '8' - moves[i].row;
                                std::cout << toCol << toRow;
                                if (i < moves.size() - 1) std::cout << ", ";
                            }
                            std::cout << "\n";
                        }
                    }
                }
            }
            if (!foundMoves) {
                std::cout << "  No legal moves available!\n";
            }
            std::cout << "\n";
        } else if (command == "board" || command == "show") {
            // Just redisplay the board on next iteration
            continue;
        } else {
            processMove(input);
        }
    }
    
    if (state == GameState::CHECKMATE || state == GameState::STALEMATE) {
        board.display();
        std::cout << "\nGame Over! Total moves: " << moveCount << "\n";
    }
}

