#include "core/Piece.h"
#include "core/Board.h"

Piece::Piece(PieceType t, PieceColor c, Position pos) 
    : type(t), color(c), position(pos), hasMoved(false) {}

std::string Piece::getName() const {
    switch(type) {
        case PieceType::PAWN: return "Pawn";
        case PieceType::ROOK: return "Rook";
        case PieceType::KNIGHT: return "Knight";
        case PieceType::BISHOP: return "Bishop";
        case PieceType::QUEEN: return "Queen";
        case PieceType::KING: return "King";
        default: return "None";
    }
}

// Pawn movement logic
std::vector<Position> Pawn::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    int direction = (color == PieceColor::WHITE) ? -1 : 1;
    int startRow = (color == PieceColor::WHITE) ? 6 : 1;
    
    // Forward move
    Position forward(position.row + direction, position.col);
    if (forward.isValid() && !board->getPiece(forward)) {
        moves.push_back(forward);
        
        // Double forward move from starting position
        if (position.row == startRow) {
            Position doubleForward(position.row + 2 * direction, position.col);
            if (!board->getPiece(doubleForward)) {
                moves.push_back(doubleForward);
            }
        }
    }
    
    // Capture diagonally
    for (int dcol : {-1, 1}) {
        Position diagonal(position.row + direction, position.col + dcol);
        if (diagonal.isValid()) {
            Piece* target = board->getPiece(diagonal);
            if (target && target->getColor() != color) {
                moves.push_back(diagonal);
            }
            // En passant
            if (diagonal == board->getEnPassantTarget()) {
                moves.push_back(diagonal);
            }
        }
    }
    
    return moves;
}

// Rook movement logic
std::vector<Position> Rook::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    
    // Horizontal and vertical directions
    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    
    for (auto& dir : directions) {
        for (int step = 1; step < 8; step++) {
            Position newPos(position.row + step * dir[0], 
                          position.col + step * dir[1]);
            
            if (!newPos.isValid()) break;
            
            Piece* target = board->getPiece(newPos);
            if (!target) {
                moves.push_back(newPos);
            } else {
                if (target->getColor() != color) {
                    moves.push_back(newPos);
                }
                break;
            }
        }
    }
    
    return moves;
}

// Knight movement logic
std::vector<Position> Knight::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    
    int knightMoves[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    
    for (auto& move : knightMoves) {
        Position newPos(position.row + move[0], position.col + move[1]);
        
        if (newPos.isValid()) {
            Piece* target = board->getPiece(newPos);
            if (!target || target->getColor() != color) {
                moves.push_back(newPos);
            }
        }
    }
    
    return moves;
}

// Bishop movement logic
std::vector<Position> Bishop::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    
    // Diagonal directions
    int directions[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    
    for (auto& dir : directions) {
        for (int step = 1; step < 8; step++) {
            Position newPos(position.row + step * dir[0], 
                          position.col + step * dir[1]);
            
            if (!newPos.isValid()) break;
            
            Piece* target = board->getPiece(newPos);
            if (!target) {
                moves.push_back(newPos);
            } else {
                if (target->getColor() != color) {
                    moves.push_back(newPos);
                }
                break;
            }
        }
    }
    
    return moves;
}

// Queen movement logic (combines Rook and Bishop)
std::vector<Position> Queen::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    
    // All 8 directions
    int directions[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
    };
    
    for (auto& dir : directions) {
        for (int step = 1; step < 8; step++) {
            Position newPos(position.row + step * dir[0], 
                          position.col + step * dir[1]);
            
            if (!newPos.isValid()) break;
            
            Piece* target = board->getPiece(newPos);
            if (!target) {
                moves.push_back(newPos);
            } else {
                if (target->getColor() != color) {
                    moves.push_back(newPos);
                }
                break;
            }
        }
    }
    
    return moves;
}

// King movement logic
std::vector<Position> King::getPossibleMoves(Board* board) const {
    std::vector<Position> moves;
    
    // All adjacent squares
    int directions[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1},
        {0, -1},           {0, 1},
        {1, -1},  {1, 0},  {1, 1}
    };
    
    for (auto& dir : directions) {
        Position newPos(position.row + dir[0], position.col + dir[1]);
        
        if (newPos.isValid()) {
            Piece* target = board->getPiece(newPos);
            if (!target || target->getColor() != color) {
                moves.push_back(newPos);
            }
        }
    }
    
    // Castling
    if (!hasMoved) {
        int row = position.row;
        
        // Kingside castling
        Piece* kingsideRook = board->getPiece(row, 7);
        if (kingsideRook && kingsideRook->getType() == PieceType::ROOK && 
            !kingsideRook->hasMovedBefore()) {
            if (!board->getPiece(row, 5) && !board->getPiece(row, 6)) {
                PieceColor enemyColor = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
                if (!board->isSquareUnderAttack(Position(row, 4), enemyColor) &&
                    !board->isSquareUnderAttack(Position(row, 5), enemyColor) &&
                    !board->isSquareUnderAttack(Position(row, 6), enemyColor)) {
                    moves.push_back(Position(row, 6));
                }
            }
        }
        
        // Queenside castling
        Piece* queensideRook = board->getPiece(row, 0);
        if (queensideRook && queensideRook->getType() == PieceType::ROOK && 
            !queensideRook->hasMovedBefore()) {
            if (!board->getPiece(row, 1) && !board->getPiece(row, 2) && 
                !board->getPiece(row, 3)) {
                PieceColor enemyColor = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
                if (!board->isSquareUnderAttack(Position(row, 4), enemyColor) &&
                    !board->isSquareUnderAttack(Position(row, 3), enemyColor) &&
                    !board->isSquareUnderAttack(Position(row, 2), enemyColor)) {
                    moves.push_back(Position(row, 2));
                }
            }
        }
    }
    
    return moves;
}
