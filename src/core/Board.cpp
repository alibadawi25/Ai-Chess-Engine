#include "core/Board.h"
#include <iostream>
#include <iomanip>
#include <cmath>

Board::Board() : currentTurn(PieceColor::WHITE), enPassantTarget(-1, -1),
                 whiteKingPos(7, 4), blackKingPos(0, 4), inCheckTest(false), positionHash(0),
                 whiteNonPawn(0), blackNonPawn(0) {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            squares[i][j] = nullptr;
        }
    }
}

Board* Board::copyBoard() const {
    Board* newBoard = new Board();
    
    // Copy all pieces
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (squares[row][col]) {
                Piece* originalPiece = squares[row][col].get();
                Position pos(row, col);
                
                // Create new piece based on type
                switch (originalPiece->getType()) {
                    case PieceType::PAWN:
                        newBoard->squares[row][col] = std::make_unique<Pawn>(
                            originalPiece->getColor(), pos);
                        break;
                    case PieceType::ROOK:
                        newBoard->squares[row][col] = std::make_unique<Rook>(
                            originalPiece->getColor(), pos);
                        if (static_cast<Rook*>(originalPiece)->hasMovedBefore()) {
                            static_cast<Rook*>(newBoard->squares[row][col].get())->setMoved(true);
                        }
                        break;
                    case PieceType::KNIGHT:
                        newBoard->squares[row][col] = std::make_unique<Knight>(
                            originalPiece->getColor(), pos);
                        break;
                    case PieceType::BISHOP:
                        newBoard->squares[row][col] = std::make_unique<Bishop>(
                            originalPiece->getColor(), pos);
                        break;
                    case PieceType::QUEEN:
                        newBoard->squares[row][col] = std::make_unique<Queen>(
                            originalPiece->getColor(), pos);
                        break;
                    case PieceType::KING:
                        newBoard->squares[row][col] = std::make_unique<King>(
                            originalPiece->getColor(), pos);
                        if (static_cast<King*>(originalPiece)->hasMovedBefore()) {
                            static_cast<King*>(newBoard->squares[row][col].get())->setMoved(true);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    // Copy game state
    newBoard->currentTurn = currentTurn;
    newBoard->enPassantTarget = enPassantTarget;
    newBoard->inCheckTest = false;  // Reset flag for copied board
    newBoard->whiteKingPos = whiteKingPos;
    newBoard->blackKingPos = blackKingPos;
    newBoard->moveHistory = moveHistory;
    newBoard->positionHash = positionHash;
    newBoard->whiteNonPawn = whiteNonPawn;
    newBoard->blackNonPawn = blackNonPawn;
    
    return newBoard;
}

void Board::initialize() {
    // Full reset — clear all squares and game state before placing pieces
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            squares[i][j] = nullptr;
    currentTurn    = PieceColor::WHITE;
    enPassantTarget = Position(-1, -1);
    positionHash   = 0;
    inCheckTest    = false;
    moveHistory.clear();

    // Initialize pawns
    for (int col = 0; col < 8; col++) {
        squares[1][col] = std::make_unique<Pawn>(PieceColor::BLACK, Position(1, col));
        squares[6][col] = std::make_unique<Pawn>(PieceColor::WHITE, Position(6, col));
    }
    
    // Initialize rooks
    squares[0][0] = std::make_unique<Rook>(PieceColor::BLACK, Position(0, 0));
    squares[0][7] = std::make_unique<Rook>(PieceColor::BLACK, Position(0, 7));
    squares[7][0] = std::make_unique<Rook>(PieceColor::WHITE, Position(7, 0));
    squares[7][7] = std::make_unique<Rook>(PieceColor::WHITE, Position(7, 7));
    
    // Initialize knights
    squares[0][1] = std::make_unique<Knight>(PieceColor::BLACK, Position(0, 1));
    squares[0][6] = std::make_unique<Knight>(PieceColor::BLACK, Position(0, 6));
    squares[7][1] = std::make_unique<Knight>(PieceColor::WHITE, Position(7, 1));
    squares[7][6] = std::make_unique<Knight>(PieceColor::WHITE, Position(7, 6));
    
    // Initialize bishops
    squares[0][2] = std::make_unique<Bishop>(PieceColor::BLACK, Position(0, 2));
    squares[0][5] = std::make_unique<Bishop>(PieceColor::BLACK, Position(0, 5));
    squares[7][2] = std::make_unique<Bishop>(PieceColor::WHITE, Position(7, 2));
    squares[7][5] = std::make_unique<Bishop>(PieceColor::WHITE, Position(7, 5));
    
    // Initialize queens
    squares[0][3] = std::make_unique<Queen>(PieceColor::BLACK, Position(0, 3));
    squares[7][3] = std::make_unique<Queen>(PieceColor::WHITE, Position(7, 3));
    
    // Initialize kings
    squares[0][4] = std::make_unique<King>(PieceColor::BLACK, Position(0, 4));
    squares[7][4] = std::make_unique<King>(PieceColor::WHITE, Position(7, 4));
    
    blackKingPos = Position(0, 4);
    whiteKingPos = Position(7, 4);
    whiteNonPawn = 7;  // 2R + 2N + 2B + 1Q
    blackNonPawn = 7;
}

void Board::display() const {
    std::cout << "\n    a   b   c   d   e   f   g   h\n";
    std::cout << "  +---+---+---+---+---+---+---+---+\n";
    
    for (int row = 0; row < 8; row++) {
        std::cout << (8 - row) << " |";
        
        for (int col = 0; col < 8; col++) {
            if (squares[row][col]) {
                std::cout << " " << squares[row][col]->getSymbol() << " |";
            } else {
                std::cout << "   |";
            }
        }
        
        std::cout << " " << (8 - row) << "\n";
        std::cout << "  +---+---+---+---+---+---+---+---+\n";
    }
    
    std::cout << "    a   b   c   d   e   f   g   h\n\n";
}

Piece* Board::getPiece(Position pos) const {
    if (!pos.isValid()) return nullptr;
    return squares[pos.row][pos.col].get();
}

Piece* Board::getPiece(int row, int col) const {
    if (row < 0 || row >= 8 || col < 0 || col >= 8) return nullptr;
    return squares[row][col].get();
}

Position Board::getKingPosition(PieceColor color) const {
    return (color == PieceColor::WHITE) ? whiteKingPos : blackKingPos;
}

bool Board::movePiece(Position from, Position to) {
    if (!isValidMove(from, to)) {
        return false;
    }
    
    Piece* piece = getPiece(from);
    if (!piece) return false;
    
    Move move(from, to);
    
    // Check for en passant capture
    if (piece->getType() == PieceType::PAWN && to == enPassantTarget) {
        move.isEnPassant = true;
        int captureRow = (piece->getColor() == PieceColor::WHITE) ? to.row + 1 : to.row - 1;
        squares[captureRow][to.col] = nullptr;
    }
    
    // Check for castling
    if (piece->getType() == PieceType::KING && abs(to.col - from.col) == 2) {
        move.isCastling = true;
        int rookCol = (to.col > from.col) ? 7 : 0;
        int newRookCol = (to.col > from.col) ? 5 : 3;
        
        auto& rook = squares[from.row][rookCol];
        rook->setPosition(Position(from.row, newRookCol));
        squares[from.row][newRookCol] = std::move(rook);
        squares[from.row][rookCol] = nullptr;
    }
    
    // Update en passant target
    enPassantTarget = Position(-1, -1);
    if (piece->getType() == PieceType::PAWN && abs(to.row - from.row) == 2) {
        enPassantTarget = Position((from.row + to.row) / 2, from.col);
    }
    
    // Check for capture
    if (squares[to.row][to.col]) {
        move.isCapture = true;
    }
    
    // Move the piece
    piece->setMoved(true);
    piece->setPosition(to);
    squares[to.row][to.col] = std::move(squares[from.row][from.col]);
    
    // Update king position
    if (piece->getType() == PieceType::KING) {
        if (piece->getColor() == PieceColor::WHITE) {
            whiteKingPos = to;
        } else {
            blackKingPos = to;
        }
    }
    
    // Pawn promotion - auto-promote to Queen
    if (piece->getType() == PieceType::PAWN) {
        if ((piece->getColor() == PieceColor::WHITE && to.row == 0) ||
            (piece->getColor() == PieceColor::BLACK && to.row == 7)) {
            squares[to.row][to.col] = std::make_unique<Queen>(piece->getColor(), to);
            squares[to.row][to.col]->setMoved(true);
            std::cout << "[PAWN PROMOTED TO QUEEN] " << (char)('a' + from.col) << (8 - from.row) 
                      << " -> " << (char)('a' + to.col) << (8 - to.row) << "\n";
        }
    }
    
    moveHistory.push_back(move);
    switchTurn();
    
    return true;
}

bool Board::isSquareUnderAttack(Position pos, PieceColor attackingColor) const {
    // REVERSE RAY-CAST: check outward from target square instead of
    // scanning all 64 squares.  Typically examines ~10-20 squares vs 64.
    int r = pos.row, c = pos.col;

    // 1. Pawn attacks (2 lookups)
    int pawnRow = (attackingColor == PieceColor::WHITE) ? r + 1 : r - 1;
    if (pawnRow >= 0 && pawnRow < 8) {
        if (c > 0) {
            Piece* p = squares[pawnRow][c - 1].get();
            if (p && p->getColor() == attackingColor && p->getType() == PieceType::PAWN) return true;
        }
        if (c < 7) {
            Piece* p = squares[pawnRow][c + 1].get();
            if (p && p->getColor() == attackingColor && p->getType() == PieceType::PAWN) return true;
        }
    }

    // 2. Knight attacks (8 lookups)
    static constexpr int km[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (const auto& k : km) {
        int nr = r + k[0], nc = c + k[1];
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
            Piece* p = squares[nr][nc].get();
            if (p && p->getColor() == attackingColor && p->getType() == PieceType::KNIGHT) return true;
        }
    }

    // 3. King attacks (8 lookups)
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
                Piece* p = squares[nr][nc].get();
                if (p && p->getColor() == attackingColor && p->getType() == PieceType::KING) return true;
            }
        }
    }

    // 4. Diagonal rays — bishop / queen (stops at first blocker)
    static constexpr int diags[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (const auto& d : diags) {
        for (int s = 1; s < 8; s++) {
            int nr = r + s * d[0], nc = c + s * d[1];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
            Piece* p = squares[nr][nc].get();
            if (p) {
                if (p->getColor() == attackingColor &&
                    (p->getType() == PieceType::BISHOP || p->getType() == PieceType::QUEEN))
                    return true;
                break; // blocked
            }
        }
    }

    // 5. Straight rays — rook / queen (stops at first blocker)
    static constexpr int straights[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (const auto& d : straights) {
        for (int s = 1; s < 8; s++) {
            int nr = r + s * d[0], nc = c + s * d[1];
            if (nr < 0 || nr > 7 || nc < 0 || nc > 7) break;
            Piece* p = squares[nr][nc].get();
            if (p) {
                if (p->getColor() == attackingColor &&
                    (p->getType() == PieceType::ROOK || p->getType() == PieceType::QUEEN))
                    return true;
                break; // blocked
            }
        }
    }

    return false;
}

bool Board::isKingInCheck(PieceColor color) const {
    Position kingPos = getKingPosition(color);
    PieceColor enemyColor = (color == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
    return isSquareUnderAttack(kingPos, enemyColor);
}

bool Board::wouldBeInCheck(PieceColor color, Position from, Position to) {
    // Validate positions
    if (from.row < 0 || from.row >= 8 || from.col < 0 || from.col >= 8) return true;
    if (to.row < 0 || to.row >= 8 || to.col < 0 || to.col >= 8) return true;
    
    Piece* movingPiece = getPiece(from);
    if (!movingPiece) return false;
    
    // Save state
    Position originalPos = movingPiece->getPosition();
    bool isKingMove = (movingPiece->getType() == PieceType::KING);
    
    // Temporarily make the move using unique_ptr swaps
    std::unique_ptr<Piece> capturedPiece = std::move(squares[to.row][to.col]);
    squares[to.row][to.col] = std::move(squares[from.row][from.col]);
    squares[to.row][to.col]->setPosition(to);
    
    // Update king position if needed
    Position savedKingPos;
    if (isKingMove) {
        if (color == PieceColor::WHITE) {
            savedKingPos = whiteKingPos;
            whiteKingPos = to;
        } else {
            savedKingPos = blackKingPos;
            blackKingPos = to;
        }
    }
    
    // Check if king is in check (uses direct attack detection, no recursion)
    bool inCheck = isKingInCheck(color);
    
    // Restore everything
    squares[to.row][to.col]->setPosition(originalPos);
    squares[from.row][from.col] = std::move(squares[to.row][to.col]);
    squares[to.row][to.col] = std::move(capturedPiece);
    
    if (isKingMove) {
        if (color == PieceColor::WHITE) {
            whiteKingPos = savedKingPos;
        } else {
            blackKingPos = savedKingPos;
        }
    }
    
    return inCheck;
}

bool Board::isValidMove(Position from, Position to) {
    Piece* piece = getPiece(from);
    if (!piece) return false;
    
    if (piece->getColor() != currentTurn) return false;
    
    // Get possible moves for this piece
    auto possibleMoves = piece->getPossibleMoves(this);
    
    bool isLegalMove = false;
    for (const auto& move : possibleMoves) {
        if (move == to) {
            isLegalMove = true;
            break;
        }
    }
    
    if (!isLegalMove) return false;
    
    // Check if move would put own king in check
    if (wouldBeInCheck(piece->getColor(), from, to)) {
        return false;
    }
    
    return true;
}

std::vector<Position> Board::getAllValidMoves(Position from) {
    std::vector<Position> validMoves;
    Piece* piece = getPiece(from);
    
    if (!piece || piece->getColor() != currentTurn) {
        return validMoves;
    }
    
    std::vector<Position> possibleMoves;
    try {
        possibleMoves = piece->getPossibleMoves(this);
    } catch (const std::exception& e) {
        std::cerr << "ERROR in getPossibleMoves: " << e.what() << "\n";
        return validMoves;
    } catch (...) {
        std::cerr << "UNKNOWN ERROR in getPossibleMoves\n";
        return validMoves;
    }
    
    for (const auto& to : possibleMoves) {
        try {
            if (!wouldBeInCheck(piece->getColor(), from, to)) {
                validMoves.push_back(to);
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR checking move validity: " << e.what() << "\n";
            continue;  // Skip this move
        } catch (...) {
            std::cerr << "UNKNOWN ERROR checking move validity\n";
            continue;  // Skip this move
        }
    }
    
    return validMoves;
}

bool Board::hasAnyLegalMoves(PieceColor color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = getPiece(row, col);
            if (piece && piece->getColor() == color) {
                try {
                    auto moves = getAllValidMoves(Position(row, col));
                    if (!moves.empty()) {
                        return true;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "ERROR in hasAnyLegalMoves at (" << row << "," << col << "): " << e.what() << "\n";
                    continue;  // Skip this piece and try others
                } catch (...) {
                    std::cerr << "UNKNOWN ERROR in hasAnyLegalMoves at (" << row << "," << col << ")\n";
                    continue;  // Skip this piece and try others
                }
            }
        }
    }
    return false;
}

bool Board::isCheckmate(PieceColor color) {
    return isKingInCheck(color) && !hasAnyLegalMoves(color);
}

bool Board::isStalemate(PieceColor color) {
    return !isKingInCheck(color) && !hasAnyLegalMoves(color);
}

void Board::switchTurn() {
    currentTurn = (currentTurn == PieceColor::WHITE) ? PieceColor::BLACK : PieceColor::WHITE;
}

// ============================================================
// PSEUDO-LEGAL MOVE GENERATION (for search - skips check test)
// ============================================================
std::vector<Position> Board::getPseudoLegalMoves(Position from) const {
    std::vector<Position> moves;
    Piece* piece = getPiece(from);
    if (!piece) return moves;
    
    // Use existing getPossibleMoves (generates pseudo-legal moves)
    // const_cast is needed because getPossibleMoves takes Board* not const Board* 
    moves = piece->getPossibleMoves(const_cast<Board*>(this));
    return moves;
}

// ============================================================
// MAKE/UNMAKE MOVE (for search - no validation, incremental update)
// Eliminates per-node board copying: ~1000x fewer heap allocations
// ============================================================
UndoInfo Board::makeMove(Position from, Position to) {
    UndoInfo undo;
    undo.from = from;
    undo.to = to;
    undo.oldEnPassant = enPassantTarget;
    undo.oldWhiteKingPos = whiteKingPos;
    undo.oldBlackKingPos = blackKingPos;
    
    Piece* piece = squares[from.row][from.col].get();
    undo.movedPieceWasMoved = piece->hasMovedBefore();
    
    // En passant capture
    if (piece->getType() == PieceType::PAWN && to == enPassantTarget && 
        enPassantTarget.row >= 0 && enPassantTarget.col >= 0) {
        undo.isEnPassant = true;
        int captureRow = (piece->getColor() == PieceColor::WHITE) ? to.row + 1 : to.row - 1;
        undo.capturedPiece = std::move(squares[captureRow][to.col]);
    }
    // Castling
    else if (piece->getType() == PieceType::KING && std::abs(to.col - from.col) == 2) {
        undo.isCastling = true;
        int rookCol = (to.col > from.col) ? 7 : 0;
        int newRookCol = (to.col > from.col) ? 5 : 3;
        undo.rookFrom = Position(from.row, rookCol);
        undo.rookTo = Position(from.row, newRookCol);
        
        Piece* rook = squares[from.row][rookCol].get();
        undo.rookWasMoved = rook->hasMovedBefore();
        rook->setPosition(Position(from.row, newRookCol));
        rook->setMoved(true);
        squares[from.row][newRookCol] = std::move(squares[from.row][rookCol]);
    }
    // Normal capture
    else if (squares[to.row][to.col]) {
        undo.capturedPiece = std::move(squares[to.row][to.col]);
    }
    
    // Update en passant target
    enPassantTarget = Position(-1, -1);
    if (piece->getType() == PieceType::PAWN && std::abs(to.row - from.row) == 2) {
        enPassantTarget = Position((from.row + to.row) / 2, from.col);
    }
    
    // Move the piece
    piece->setMoved(true);
    piece->setPosition(to);
    squares[to.row][to.col] = std::move(squares[from.row][from.col]);
    
    // Update king position
    if (piece->getType() == PieceType::KING) {
        if (piece->getColor() == PieceColor::WHITE) whiteKingPos = to;
        else blackKingPos = to;
    }
    
    // Pawn promotion (auto-queen for search)
    if (piece->getType() == PieceType::PAWN) {
        if ((piece->getColor() == PieceColor::WHITE && to.row == 0) ||
            (piece->getColor() == PieceColor::BLACK && to.row == 7)) {
            undo.isPromotion = true;
            undo.promotedPawn = std::move(squares[to.row][to.col]);
            squares[to.row][to.col] = std::make_unique<Queen>(piece->getColor(), to);
            squares[to.row][to.col]->setMoved(true);
        }
    }
    
    // Update non-pawn material counters
    if (undo.capturedPiece) {
        PieceType ct = undo.capturedPiece->getType();
        if (ct != PieceType::PAWN && ct != PieceType::KING) {
            if (undo.capturedPiece->getColor() == PieceColor::WHITE) whiteNonPawn--;
            else blackNonPawn--;
        }
    }
    if (undo.isPromotion) {
        if (squares[to.row][to.col]->getColor() == PieceColor::WHITE) whiteNonPawn++;
        else blackNonPawn++;
    }
    
    switchTurn();
    return undo;
}

void Board::unmakeMove(UndoInfo& undo) {
    switchTurn();
    
    // Restore non-pawn material counters (reverse of makeMove)
    if (undo.isPromotion) {
        if (squares[undo.to.row][undo.to.col]->getColor() == PieceColor::WHITE) whiteNonPawn--;
        else blackNonPawn--;
    }
    if (undo.capturedPiece) {
        PieceType ct = undo.capturedPiece->getType();
        if (ct != PieceType::PAWN && ct != PieceType::KING) {
            if (undo.capturedPiece->getColor() == PieceColor::WHITE) whiteNonPawn++;
            else blackNonPawn++;
        }
    }
    
    // Undo promotion: replace queen with original pawn
    if (undo.isPromotion) {
        squares[undo.to.row][undo.to.col] = std::move(undo.promotedPawn);
    }
    
    // Move piece back to original square
    Piece* piece = squares[undo.to.row][undo.to.col].get();
    piece->setPosition(undo.from);
    piece->setMoved(undo.movedPieceWasMoved);
    squares[undo.from.row][undo.from.col] = std::move(squares[undo.to.row][undo.to.col]);
    
    // Restore captured piece
    if (undo.isEnPassant) {
        int captureRow = (piece->getColor() == PieceColor::WHITE) ? undo.to.row + 1 : undo.to.row - 1;
        squares[captureRow][undo.to.col] = std::move(undo.capturedPiece);
    } else if (undo.capturedPiece) {
        squares[undo.to.row][undo.to.col] = std::move(undo.capturedPiece);
    }
    
    // Undo castling rook move
    if (undo.isCastling) {
        Piece* rook = squares[undo.rookTo.row][undo.rookTo.col].get();
        rook->setPosition(undo.rookFrom);
        rook->setMoved(undo.rookWasMoved);
        squares[undo.rookFrom.row][undo.rookFrom.col] = std::move(squares[undo.rookTo.row][undo.rookTo.col]);
    }
    
    // Restore board state
    enPassantTarget = undo.oldEnPassant;
    whiteKingPos = undo.oldWhiteKingPos;
    blackKingPos = undo.oldBlackKingPos;
}

std::string Board::toFEN() const {
    std::string fen;
    fen.reserve(80);
    
    // 1. Piece placement (rank 8 to rank 1 = row 0 to row 7)
    for (int row = 0; row < 8; row++) {
        int emptyCount = 0;
        for (int col = 0; col < 8; col++) {
            if (squares[row][col]) {
                if (emptyCount > 0) {
                    fen += std::to_string(emptyCount);
                    emptyCount = 0;
                }
                fen += squares[row][col]->getSymbol();
            } else {
                emptyCount++;
            }
        }
        if (emptyCount > 0) {
            fen += std::to_string(emptyCount);
        }
        if (row < 7) fen += '/';
    }
    
    // 2. Active color
    fen += (currentTurn == PieceColor::WHITE) ? " w " : " b ";
    
    // 3. Castling availability
    std::string castling;
    
    // White kingside: king at (7,4) and rook at (7,7) not moved
    Piece* wk = squares[7][4].get();
    if (wk && wk->getType() == PieceType::KING && !wk->hasMovedBefore()) {
        Piece* wr = squares[7][7].get();
        if (wr && wr->getType() == PieceType::ROOK && !wr->hasMovedBefore()) {
            castling += 'K';
        }
        wr = squares[7][0].get();
        if (wr && wr->getType() == PieceType::ROOK && !wr->hasMovedBefore()) {
            castling += 'Q';
        }
    }
    
    // Black kingside: king at (0,4) and rook at (0,7) not moved
    Piece* bk = squares[0][4].get();
    if (bk && bk->getType() == PieceType::KING && !bk->hasMovedBefore()) {
        Piece* br = squares[0][7].get();
        if (br && br->getType() == PieceType::ROOK && !br->hasMovedBefore()) {
            castling += 'k';
        }
        br = squares[0][0].get();
        if (br && br->getType() == PieceType::ROOK && !br->hasMovedBefore()) {
            castling += 'q';
        }
    }
    
    if (castling.empty()) castling = "-";
    fen += castling;
    
    // 4. En passant target square (only if actually capturable)
    bool hasEP = false;
    if (enPassantTarget.row >= 0 && enPassantTarget.row < 8 &&
        enPassantTarget.col >= 0 && enPassantTarget.col < 8) {
        // Check if an opposing pawn can actually capture on this square
        PieceColor epCapturerColor = (currentTurn == PieceColor::WHITE) 
            ? PieceColor::WHITE : PieceColor::BLACK;
        int capturerRow = (epCapturerColor == PieceColor::WHITE) 
            ? enPassantTarget.row + 1 : enPassantTarget.row - 1;
        
        // Check left and right of the en passant target for an opposing pawn
        for (int dc : {-1, 1}) {
            int cc = enPassantTarget.col + dc;
            if (cc >= 0 && cc < 8 && capturerRow >= 0 && capturerRow < 8) {
                Piece* p = squares[capturerRow][cc].get();
                if (p && p->getType() == PieceType::PAWN && p->getColor() == epCapturerColor) {
                    hasEP = true;
                    break;
                }
            }
        }
    }
    
    if (hasEP) {
        fen += ' ';
        fen += static_cast<char>('a' + enPassantTarget.col);
        fen += static_cast<char>('0' + (8 - enPassantTarget.row));
    } else {
        fen += " -";
    }
    
    // Note: No halfmove clock or fullmove number (matches Book.txt format)
    return fen;
}

