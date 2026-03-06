#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include "core/Board.h"
#include "core/Piece.h"
#include "search/AIPlayer.h"

using namespace Gdiplus;

// Custom window message for AI move completion
#define WM_AI_MOVE (WM_USER + 1)

// Global layout constants (used by WndProc and main_gui.cpp as well)
const int WINDOW_W  = 970;
const int WINDOW_H  = 720;
const int SIDEBAR_X = 720;
const int SIDEBAR_W = 250;

class ChessGraphics {
private:
    // Core handles
    HWND hwnd;
    ULONG_PTR gdiplusToken;
    Board* board;
    AIPlayer* aiPlayer;

    // Board layout
    const int SQUARE_SIZE    = 80;
    const int BOARD_OFFSET_X = 40;
    const int BOARD_OFFSET_Y = 40;

    // Selection state
    Position selectedSquare;
    bool hasSelection;
    std::vector<Position> highlightedSquares;

    // Last-move highlight
    Position lastMoveFrom;
    Position lastMoveTo;
    bool hasLastMove;

    // Game tracking
    int halfMoveCount;
    int lastEvalCentipawns;
    std::vector<PieceType> capturedByWhite;
    std::vector<PieceType> capturedByBlack;
    std::vector<std::wstring> moveHistory;

    // AI state
    bool aiMode;
    PieceColor playerColor;
    bool waitingForAI;
    AIStrength currentStrength;
    Position pendingAIFrom;
    Position pendingAITo;

    // Piece images
    bool piecesLoaded;
    Gdiplus::Bitmap* pieceImages[6][2];

    // Colours
    Gdiplus::Color lightSquareColor;
    Gdiplus::Color darkSquareColor;
    Gdiplus::Color selectedColor;
    Gdiplus::Color highlightColor;
    Gdiplus::Color lastMoveColor;

    // --- Private drawing helpers ---
    void LoadPieceImages();
    void DrawBoard(Graphics* graphics);
    void DrawPieces(Graphics* graphics);
    void DrawPiece(Graphics* graphics, Piece* piece, int x, int y);
    void DrawHighlights(Graphics* graphics);
    void DrawCoordinates(Graphics* graphics);
    void DrawSidebar(Graphics* graphics);
    void DrawEvalBar(Graphics* graphics, int px, int py);
    void DrawCapturedPieces(Graphics* graphics, int px, int py);
    void DrawMiniPiece(Graphics* graphics, PieceType type, bool isWhite, int cx, int cy, int size);
    void DrawDifficultyPanel(Graphics* graphics, int px, int py);
    void DrawMoveHistory(Graphics* graphics, int px, int py);

    // Vector-drawn fallback pieces
    void DrawKing(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);
    void DrawQueen(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);
    void DrawRook(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);
    void DrawBishop(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);
    void DrawKnight(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);
    void DrawPawn(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite);

    // Utility
    std::wstring PieceSymbol(PieceType t);
    bool ClickedDifficultyButton(int x, int y, int panelX, int panelY, AIStrength& out);
    void PrintBoardState();

    // AI helpers
    void RequestAIMove();
    void OnAIMove(Position from, Position to);

public:
    ChessGraphics(HWND window, Board* gameBoard);
    ~ChessGraphics();

    void Initialize();
    void Draw(HDC hdc);
    void HandleClick(int x, int y);

    Position ScreenToBoard(int screenX, int screenY);
    void GetSquareCoords(Position pos, int& x, int& y);

    // AI control
    void SetAIMode(bool enabled, PieceColor humanColor = PieceColor::WHITE,
                   AIStrength strength = AIStrength::MEDIUM);
    void SetAIStrength(AIStrength strength);
    bool IsAIThinking() const;
    void ProcessAIMove();
    void ResetOpeningBook();
    void ResetGame();
};

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global pointers for window procedure
extern ChessGraphics* g_graphics;
extern Board* g_board;

#endif
