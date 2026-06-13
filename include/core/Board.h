#ifndef BOARD_H
#define BOARD_H

#include "core/Piece.h"
#include <memory>
#include <vector>

struct Move {
    Position from;
    Position to;
    PieceType promotionType;
    bool isCapture;
    bool isCastling;
    bool isEnPassant;
    
    Move(Position f, Position t) 
        : from(f), to(t), promotionType(PieceType::NONE), 
          isCapture(false), isCastling(false), isEnPassant(false) {}
};

// Undo information for make/unmake move pattern (eliminates per-node board copying)
struct UndoInfo {
    Position from;
    Position to;
    std::unique_ptr<Piece> capturedPiece;
    Position oldEnPassant;
    Position oldWhiteKingPos;
    Position oldBlackKingPos;
    bool movedPieceWasMoved;
    bool isCastling;
    bool isEnPassant;
    bool isPromotion;
    Position rookFrom;
    Position rookTo;
    bool rookWasMoved;
    std::unique_ptr<Piece> promotedPawn;  // original pawn saved during promotion
    uint64_t oldHash;  // hash before this move (for incremental restore)
    
    UndoInfo() : from(-1,-1), to(-1,-1), oldEnPassant(-1,-1),
                 oldWhiteKingPos(-1,-1), oldBlackKingPos(-1,-1),
                 movedPieceWasMoved(false), isCastling(false),
                 isEnPassant(false), isPromotion(false),
                 rookFrom(-1,-1), rookTo(-1,-1), rookWasMoved(false),
                 oldHash(0) {}
};

class Board {
private:
    std::unique_ptr<Piece> squares[8][8];
    PieceColor currentTurn;
    Position enPassantTarget;
    std::vector<Move> moveHistory;
    Position whiteKingPos;
    Position blackKingPos;
    bool inCheckTest;  // Flag to prevent recursive wouldBeInCheck calls
    uint64_t positionHash;  // Incrementally updated Zobrist hash
    int whiteNonPawn;      // Count of non-pawn, non-king white pieces (fast NMP check)
    int blackNonPawn;      // Count of non-pawn, non-king black pieces
    
    void addLinearMoves(std::vector<Position>& moves, Position start, 
                       int deltaRow, int deltaCol, PieceColor pieceColor) const;
    bool wouldBeInCheck(PieceColor color, Position from, Position to);
    
public:
    Board();
    
    // Create a copy of the board (needed for search)
    Board* copyBoard() const;
    
    void initialize();
    void display() const;
    
    Piece* getPiece(Position pos) const;
    Piece* getPiece(int row, int col) const;
    
    bool movePiece(Position from, Position to);
    bool isValidMove(Position from, Position to);
    
    bool isSquareUnderAttack(Position pos, PieceColor attackingColor) const;
    bool isKingInCheck(PieceColor color) const;
    bool isCheckmate(PieceColor color);
    bool isStalemate(PieceColor color);
    
    PieceColor getCurrentTurn() const { return currentTurn; }
    Position getEnPassantTarget() const { return enPassantTarget; }
    Position getKingPosition(PieceColor color) const;
    
    std::vector<Position> getAllValidMoves(Position from);
    bool hasAnyLegalMoves(PieceColor color);
    
    // O(1) non-pawn material check (incrementally maintained)
    bool hasNonPawnMaterial(PieceColor c) const {
        return (c == PieceColor::WHITE) ? whiteNonPawn > 0 : blackNonPawn > 0;
    }
    
    void switchTurn();
    
    // Make/unmake for search (no validation, avoids per-node board copy)
    UndoInfo makeMove(Position from, Position to);
    void unmakeMove(UndoInfo& undo);
    void setEnPassantTarget(Position pos) { enPassantTarget = pos; }
    
    // Incremental hash access
    uint64_t getHash() const { return positionHash; }
    void setHash(uint64_t h) { positionHash = h; }
    
    // Generate pseudo-legal moves for a piece (no check test)
    std::vector<Position> getPseudoLegalMoves(Position from) const;
    
    // Generate FEN string for opening book lookup
    std::string toFEN() const;

    // Parse a FEN string and set up the board (placement, side, castling, EP).
    // Returns false on malformed input. Used by the UCI front-end.
    bool loadFEN(const std::string& fen);
};

#endif
