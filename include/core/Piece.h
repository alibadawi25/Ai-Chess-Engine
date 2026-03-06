#ifndef PIECE_H
#define PIECE_H

#include <string>
#include <vector>

enum class PieceType {
    PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING, NONE
};

enum class PieceColor {
    WHITE, BLACK, NONE
};

struct Position {
    int row;
    int col;
    
    Position(int r = 0, int c = 0) : row(r), col(c) {}
    
    bool operator==(const Position& other) const {
        return row == other.row && col == other.col;
    }
    
    bool isValid() const {
        return row >= 0 && row < 8 && col >= 0 && col < 8;
    }
};

class Piece {
protected:
    PieceType type;
    PieceColor color;
    Position position;
    bool hasMoved;
    
public:
    Piece(PieceType t, PieceColor c, Position pos);
    virtual ~Piece() = default;
    
    PieceType getType() const { return type; }
    PieceColor getColor() const { return color; }
    Position getPosition() const { return position; }
    bool hasMovedBefore() const { return hasMoved; }
    
    void setPosition(Position pos) { position = pos; }
    void setMoved(bool moved) { hasMoved = moved; }
    
    virtual std::vector<Position> getPossibleMoves(class Board* board) const = 0;
    virtual char getSymbol() const = 0;
    virtual std::string getName() const;
};

class Pawn : public Piece {
public:
    Pawn(PieceColor c, Position pos) : Piece(PieceType::PAWN, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'P' : 'p'; }
};

class Rook : public Piece {
public:
    Rook(PieceColor c, Position pos) : Piece(PieceType::ROOK, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'R' : 'r'; }
};

class Knight : public Piece {
public:
    Knight(PieceColor c, Position pos) : Piece(PieceType::KNIGHT, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'N' : 'n'; }
};

class Bishop : public Piece {
public:
    Bishop(PieceColor c, Position pos) : Piece(PieceType::BISHOP, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'B' : 'b'; }
};

class Queen : public Piece {
public:
    Queen(PieceColor c, Position pos) : Piece(PieceType::QUEEN, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'Q' : 'q'; }
};

class King : public Piece {
public:
    King(PieceColor c, Position pos) : Piece(PieceType::KING, c, pos) {}
    std::vector<Position> getPossibleMoves(Board* board) const override;
    char getSymbol() const override { return (color == PieceColor::WHITE) ? 'K' : 'k'; }
};

#endif
