#include "ui/Graphics.h"
#include <string>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

ChessGraphics::ChessGraphics(HWND window, Board* gameBoard)
    : hwnd(window), board(gameBoard), aiPlayer(nullptr),
      selectedSquare(-1, -1), hasSelection(false),
      lastMoveFrom(-1,-1), lastMoveTo(-1,-1), hasLastMove(false),
      halfMoveCount(0), lastEvalCentipawns(0),
      aiMode(false), playerColor(PieceColor::WHITE), waitingForAI(false),
      currentStrength(AIStrength::HARD),
      piecesLoaded(false),
      lightSquareColor(240, 217, 181), darkSquareColor(181, 136, 99),
      selectedColor(255, 255, 100, 180), highlightColor(100, 230, 80, 160),
      lastMoveColor(255, 196, 0, 120) {
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 2; j++)
            pieceImages[i][j] = nullptr;
}

ChessGraphics::~ChessGraphics() {
    if (aiPlayer) {
        delete aiPlayer;
    }
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 2; j++) {
            delete pieceImages[i][j];
            pieceImages[i][j] = nullptr;
        }
    GdiplusShutdown(gdiplusToken);
}

void ChessGraphics::Initialize() {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    LoadPieceImages();
}

void ChessGraphics::LoadPieceImages() {
    // type index: 0=King 1=Queen 2=Rook 3=Bishop 4=Knight 5=Pawn
    // color index: 0=White  1=Black
    struct Entry { int t; int c; const wchar_t* name; };
    Entry entries[] = {
        {0,0,L"wK"},{1,0,L"wQ"},{2,0,L"wR"},{3,0,L"wB"},{4,0,L"wN"},{5,0,L"wP"},
        {0,1,L"bK"},{1,1,L"bQ"},{2,1,L"bR"},{3,1,L"bB"},{4,1,L"bN"},{5,1,L"bP"}
    };

    // Locate resources/pieces/ relative to the executable
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    std::wstring dirs[] = {
        std::wstring(exePath) + L"\\..\\resources\\pieces\\",
        std::wstring(exePath) + L"\\resources\\pieces\\",
        L"resources\\pieces\\"
    };

    std::wstring piecesDir;
    for (auto& d : dirs) {
        std::wstring probe = d + L"wK.png";
        if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            piecesDir = d;
            break;
        }
    }
    if (piecesDir.empty()) {
        std::cout << "[Images] Could not find resources/pieces/ — using vector pieces.\n";
        return;
    }

    int loaded = 0;
    for (auto& e : entries) {
        std::wstring path = piecesDir + e.name + L".png";
        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path.c_str());
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            pieceImages[e.t][e.c] = bmp;
            loaded++;
        } else {
            delete bmp;
        }
    }
    if (loaded == 12) {
        piecesLoaded = true;
        std::cout << "[Images] Loaded all 12 piece images.\n";
    } else {
        std::cout << "[Images] Only " << loaded << "/12 loaded — falling back to vector pieces.\n";
    }
}

void ChessGraphics::Draw(HDC hdc) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Dark background
    SolidBrush bgBrush(Gdiplus::Color(30, 30, 35));
    graphics.FillRectangle(&bgBrush, 0, 0, WINDOW_W, WINDOW_H);

    // Board drop shadow
    SolidBrush shadowBrush(Gdiplus::Color(80, 0, 0, 0));
    graphics.FillRectangle(&shadowBrush, BOARD_OFFSET_X + 6, BOARD_OFFSET_Y + 6,
                           8 * SQUARE_SIZE, 8 * SQUARE_SIZE);

    DrawBoard(&graphics);
    DrawHighlights(&graphics);
    DrawPieces(&graphics);
    DrawCoordinates(&graphics);
    DrawSidebar(&graphics);
}

void ChessGraphics::DrawBoard(Graphics* graphics) {
    // Board border frame
    Pen framePen(Gdiplus::Color(100, 80, 60), 3);
    graphics->DrawRectangle(&framePen,
        BOARD_OFFSET_X - 3, BOARD_OFFSET_Y - 3,
        8 * SQUARE_SIZE + 6, 8 * SQUARE_SIZE + 6);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int x = BOARD_OFFSET_X + col * SQUARE_SIZE;
            int y = BOARD_OFFSET_Y + row * SQUARE_SIZE;

            Gdiplus::Color squareColor = ((row + col) % 2 == 0) ? lightSquareColor : darkSquareColor;
            SolidBrush brush(squareColor);
            graphics->FillRectangle(&brush, x, y, SQUARE_SIZE, SQUARE_SIZE);
        }
    }
}

void ChessGraphics::DrawHighlights(Graphics* graphics) {
    // Last move highlight
    if (hasLastMove) {
        int x, y;
        GetSquareCoords(lastMoveFrom, x, y);
        SolidBrush lmBrush(lastMoveColor);
        graphics->FillRectangle(&lmBrush, x, y, SQUARE_SIZE, SQUARE_SIZE);
        GetSquareCoords(lastMoveTo, x, y);
        graphics->FillRectangle(&lmBrush, x, y, SQUARE_SIZE, SQUARE_SIZE);
    }

    // Selected square
    if (hasSelection) {
        int x, y;
        GetSquareCoords(selectedSquare, x, y);
        SolidBrush selectedBrush(selectedColor);
        graphics->FillRectangle(&selectedBrush, x, y, SQUARE_SIZE, SQUARE_SIZE);
    }

    // Legal move dots
    for (const auto& pos : highlightedSquares) {
        int x, y;
        GetSquareCoords(pos, x, y);
        bool isCapture = (board->getPiece(pos) != nullptr);
        if (isCapture) {
            // Ring for captures
            Pen ringPen(Gdiplus::Color(180, 80, 200, 60), 4);
            graphics->DrawEllipse(&ringPen, x + 4, y + 4, SQUARE_SIZE - 8, SQUARE_SIZE - 8);
        } else {
            // Dot for quiet moves
            SolidBrush dotBrush(Gdiplus::Color(140, 80, 200, 60));
            int r = 13;
            graphics->FillEllipse(&dotBrush,
                x + SQUARE_SIZE/2 - r, y + SQUARE_SIZE/2 - r, r*2, r*2);
        }
    }
}

void ChessGraphics::DrawCoordinates(Graphics* graphics) {
    Font font(L"Segoe UI", 11, FontStyleBold);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);

    // Column labels (a–h) — above and below
    for (int col = 0; col < 8; col++) {
        wchar_t label[2] = {(wchar_t)(L'a' + col), 0};
        int cx = BOARD_OFFSET_X + col * SQUARE_SIZE + SQUARE_SIZE / 2;
        bool isDark = (col % 2 == 0); // corner colour pattern
        Gdiplus::Color tc = isDark ? darkSquareColor : lightSquareColor;
        SolidBrush tb(tc);
        RectF r1((float)(cx - 18), (float)(BOARD_OFFSET_Y - 28), 36, 22);
        RectF r2((float)(cx - 18), (float)(BOARD_OFFSET_Y + 8*SQUARE_SIZE + 6), 36, 22);
        graphics->DrawString(label, -1, &font, r1, &fmt, &tb);
        graphics->DrawString(label, -1, &font, r2, &fmt, &tb);
    }

    // Row labels (1–8) — left and right
    for (int row = 0; row < 8; row++) {
        wchar_t label[2] = {(wchar_t)(L'8' - row), 0};
        int cy = BOARD_OFFSET_Y + row * SQUARE_SIZE + SQUARE_SIZE / 2;
        bool isDark = (row % 2 != 0);
        Gdiplus::Color tc = isDark ? darkSquareColor : lightSquareColor;
        SolidBrush tb(tc);
        RectF rL((float)(BOARD_OFFSET_X - 32), (float)(cy - 11), 28, 22);
        RectF rR((float)(BOARD_OFFSET_X + 8*SQUARE_SIZE + 4), (float)(cy - 11), 28, 22);
        graphics->DrawString(label, -1, &font, rL, &fmt, &tb);
        graphics->DrawString(label, -1, &font, rR, &fmt, &tb);
    }
}

// ============================================================
// SIDEBAR
// ============================================================
void ChessGraphics::DrawSidebar(Graphics* graphics) {
    int px = SIDEBAR_X;
    int pw = SIDEBAR_W;

    // Panel background
    SolidBrush panelBg(Gdiplus::Color(42, 42, 50));
    graphics->FillRectangle(&panelBg, px, 0, pw, WINDOW_H);
    Pen panelEdge(Gdiplus::Color(80, 70, 60), 2);
    graphics->DrawLine(&panelEdge, px, 0, px, WINDOW_H);

    // === TITLE ===
    Font titleFont(L"Segoe UI", 18, FontStyleBold);
    SolidBrush white(Gdiplus::Color(255, 255, 255));
    StringFormat ctr;
    ctr.SetAlignment(StringAlignmentCenter);
    ctr.SetLineAlignment(StringAlignmentCenter);
    RectF titleRect((float)px, 12, (float)pw, 32);
    graphics->DrawString(L"♟ Chess AI", -1, &titleFont, titleRect, &ctr, &white);

    // Thin separator
    Pen sep(Gdiplus::Color(70, 70, 80), 1);
    graphics->DrawLine(&sep, px + 10, 50, px + pw - 10, 50);

    // === TURN INDICATOR ===
    int y = 60;
    {
        bool myTurn = !waitingForAI;
        PieceColor turn = board->getCurrentTurn();
        bool inCheck = false;
        try { inCheck = board->isKingInCheck(turn); } catch (...) {}

        // Badge
        Gdiplus::Color badgeColor = waitingForAI ? Gdiplus::Color(200, 130, 0)
            : (turn == PieceColor::WHITE ? Gdiplus::Color(220, 220, 220)
                                         : Gdiplus::Color(60, 60, 70));
        SolidBrush badgeBrush(badgeColor);
        graphics->FillRectangle(&badgeBrush, px + 14, y, pw - 28, 38);
        Pen badgeEdge(inCheck ? Gdiplus::Color(255, 50, 50) : Gdiplus::Color(90, 80, 70), 2);
        graphics->DrawRectangle(&badgeEdge, px + 14, y, pw - 28, 38);

        Font turnFont(L"Segoe UI", 14, FontStyleBold);
        Gdiplus::Color turnTextColor = (turn == PieceColor::WHITE && !waitingForAI)
            ? Gdiplus::Color(30,30,30) : Gdiplus::Color(240,240,240);
        SolidBrush turnBrush(turnTextColor);
        std::wstring turnStr;
        if (waitingForAI) turnStr = L"⏳ AI thinking...";
        else if (inCheck)  turnStr = (turn == PieceColor::WHITE ? L"♔ White — CHECK!" : L"♚ Black — CHECK!");
        else               turnStr = (turn == PieceColor::WHITE ? L"♔ White to move" : L"♚ Black to move");
        RectF tr((float)(px+14), (float)y, (float)(pw-28), 38);
        graphics->DrawString(turnStr.c_str(), -1, &turnFont, tr, &ctr, &turnBrush);
    }

    graphics->DrawLine(&sep, px + 10, y + 48, px + pw - 10, y + 48);

    // === EVAL BAR ===
    DrawEvalBar(graphics, px, y + 58);

    // === CAPTURED PIECES ===
    DrawCapturedPieces(graphics, px, y + 130);

    // === DIFFICULTY PANEL ===
    DrawDifficultyPanel(graphics, px, y + 230);

    // === MOVE HISTORY ===
    DrawMoveHistory(graphics, px, y + 370);
}

void ChessGraphics::DrawEvalBar(Graphics* graphics, int px, int py) {
    Font labelFont(L"Segoe UI", 10);
    SolidBrush grey(Gdiplus::Color(160, 160, 170));
    StringFormat lft; lft.SetAlignment(StringAlignmentNear);

    graphics->DrawString(L"Evaluation", -1, &labelFont,
        RectF((float)(px+14),(float)py, 200, 18), &lft, &grey);
    py += 20;

    int barW = SIDEBAR_W - 28;
    int barH = 22;

    // Background
    SolidBrush darkBg(Gdiplus::Color(20, 20, 20));
    graphics->FillRectangle(&darkBg, px+14, py, barW, barH);

    // Clamp eval to ±800 cp for display
    int clamp = std::max(-800, std::min(800, lastEvalCentipawns));
    // White portion = how much of bar is white
    float whiteFrac = (clamp + 800.0f) / 1600.0f;
    int whiteW = (int)(whiteFrac * barW);

    SolidBrush wBrush(Gdiplus::Color(230, 230, 230));
    SolidBrush bBrush(Gdiplus::Color(40, 40, 40));
    graphics->FillRectangle(&bBrush, px+14, py, barW, barH);
    graphics->FillRectangle(&wBrush, px+14, py, whiteW, barH);

    // Centre line
    Pen centre(Gdiplus::Color(120, 120, 120), 1);
    graphics->DrawLine(&centre, px+14+barW/2, py, px+14+barW/2, py+barH);

    // Eval label
    wchar_t evalStr[32];
    float evalPawns = lastEvalCentipawns / 100.0f;
    if (lastEvalCentipawns > 0)
        swprintf_s(evalStr, L"+%.1f", evalPawns);
    else
        swprintf_s(evalStr, L"%.1f", evalPawns);

    Font evalFont(L"Segoe UI", 11, FontStyleBold);
    SolidBrush evalColor(std::abs(lastEvalCentipawns) > 150 ? Gdiplus::Color(255,200,0) : Gdiplus::Color(200,200,200));
    StringFormat ctr; ctr.SetAlignment(StringAlignmentCenter); ctr.SetLineAlignment(StringAlignmentCenter);
    graphics->DrawString(evalStr, -1, &evalFont,
        RectF((float)(px+14),(float)py,(float)barW,(float)barH), &ctr, &evalColor);
}

void ChessGraphics::DrawCapturedPieces(Graphics* graphics, int px, int py) {
    Font labelFont(L"Segoe UI", 10);
    SolidBrush grey(Gdiplus::Color(160, 160, 170));
    StringFormat lft; lft.SetAlignment(StringAlignmentNear);

    // Captured by White (black pieces white took)
    graphics->DrawString(L"Captured by White", -1, &labelFont,
        RectF((float)(px+14),(float)py, 220, 18), &lft, &grey);
    py += 20;
    int cx = px + 16, cy = py;
    for (auto t : capturedByWhite) {
        DrawMiniPiece(graphics, t, false, cx + 10, cy + 10, 18);
        cx += 22;
        if (cx > px + SIDEBAR_W - 30) { cx = px + 16; cy += 22; }
    }

    py += 30;
    graphics->DrawString(L"Captured by Black", -1, &labelFont,
        RectF((float)(px+14),(float)py, 220, 18), &lft, &grey);
    py += 20;
    cx = px + 16;
    for (auto t : capturedByBlack) {
        DrawMiniPiece(graphics, t, true, cx + 10, cy + 10, 18);
        cx += 22;
        if (cx > px + SIDEBAR_W - 30) { cx = px + 16; cy += 22; }
    }
}

void ChessGraphics::DrawMiniPiece(Graphics* graphics, PieceType type, bool isWhite, int cx, int cy, int size) {
    Gdiplus::Color fc = isWhite ? Gdiplus::Color(230,230,230) : Gdiplus::Color(40,40,40);
    Gdiplus::Color oc = isWhite ? Gdiplus::Color(60,60,60) : Gdiplus::Color(200,200,200);
    SolidBrush fb(fc); Pen op(oc, 1.2f);
    int r = size / 2;
    switch (type) {
        case PieceType::QUEEN:  graphics->FillEllipse(&fb, cx-r, cy-r, size, size); graphics->DrawEllipse(&op, cx-r, cy-r, size, size); break;
        case PieceType::ROOK:   graphics->FillRectangle(&fb, cx-r+2, cy-r+2, size-4, size-4); graphics->DrawRectangle(&op, cx-r+2, cy-r+2, size-4, size-4); break;
        case PieceType::BISHOP: { Point pts[]={{cx,cy-r},{cx-r,cy+r},{cx+r,cy+r}}; graphics->FillPolygon(&fb,pts,3); graphics->DrawPolygon(&op,pts,3); } break;
        case PieceType::KNIGHT: graphics->FillEllipse(&fb, cx-r+1, cy-r+3, size-2, size-6); graphics->DrawEllipse(&op, cx-r+1, cy-r+3, size-2, size-6); break;
        case PieceType::PAWN:   graphics->FillEllipse(&fb, cx-r+3, cy-r+3, size-6, size-6); graphics->DrawEllipse(&op, cx-r+3, cy-r+3, size-6, size-6); break;
        default: graphics->FillEllipse(&fb, cx-r, cy-r, size, size); break;
    }
}

std::wstring ChessGraphics::PieceSymbol(PieceType t) {
    switch (t) {
        case PieceType::KING:   return L"K";
        case PieceType::QUEEN:  return L"Q";
        case PieceType::ROOK:   return L"R";
        case PieceType::BISHOP: return L"B";
        case PieceType::KNIGHT: return L"N";
        case PieceType::PAWN:   return L"";
        default: return L"?";
    }
}

void ChessGraphics::DrawDifficultyPanel(Graphics* graphics, int px, int py) {
    Font labelFont(L"Segoe UI", 10);
    SolidBrush grey(Gdiplus::Color(160,160,170));
    StringFormat lft; lft.SetAlignment(StringAlignmentNear);
    graphics->DrawString(L"Difficulty  (press 1-4)", -1, &labelFont,
        RectF((float)(px+14),(float)py, 250, 18), &lft, &grey);
    py += 22;

    struct DiffBtn { AIStrength s; const wchar_t* label; };
    DiffBtn btns[] = {
        {AIStrength::EASY,   L"1  Easy"},
        {AIStrength::MEDIUM, L"2  Medium"},
        {AIStrength::HARD,   L"3  Hard"},
        {AIStrength::EXPERT, L"4  Expert"},
    };

    Font btnFont(L"Segoe UI", 11, FontStyleBold);
    StringFormat ctr; ctr.SetAlignment(StringAlignmentCenter); ctr.SetLineAlignment(StringAlignmentCenter);

    int bw = (SIDEBAR_W - 28) / 2 - 4;
    int bh = 28;
    for (int i = 0; i < 4; i++) {
        int bx = px + 14 + (i % 2) * (bw + 8);
        int by = py + (i / 2) * (bh + 6);
        bool active = (currentStrength == btns[i].s);
        Gdiplus::Color bg = active ? Gdiplus::Color(180,120,30) : Gdiplus::Color(55,55,65);
        Gdiplus::Color edge = active ? Gdiplus::Color(240,180,60) : Gdiplus::Color(80,80,95);
        SolidBrush bgBrush(bg);
        Pen edgePen(edge, active ? 2.0f : 1.0f);
        graphics->FillRectangle(&bgBrush, bx, by, bw, bh);
        graphics->DrawRectangle(&edgePen, bx, by, bw, bh);
        SolidBrush textBrush(active ? Gdiplus::Color(255,255,255) : Gdiplus::Color(180,180,190));
        graphics->DrawString(btns[i].label, -1, &btnFont,
            RectF((float)bx,(float)by,(float)bw,(float)bh), &ctr, &textBrush);
    }
}

bool ChessGraphics::ClickedDifficultyButton(int x, int y, int panelX, int panelY, AIStrength& out) {
    // panelY is the top of DrawDifficultyPanel section
    int py = panelY + 22; // skip label
    AIStrength btns[] = {AIStrength::EASY, AIStrength::MEDIUM, AIStrength::HARD, AIStrength::EXPERT};
    int bw = (SIDEBAR_W - 28) / 2 - 4;
    int bh = 28;
    for (int i = 0; i < 4; i++) {
        int bx = panelX + 14 + (i % 2) * (bw + 8);
        int by = py + (i / 2) * (bh + 6);
        if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
            out = btns[i];
            return true;
        }
    }
    return false;
}

void ChessGraphics::DrawMoveHistory(Graphics* graphics, int px, int py) {
    Font labelFont(L"Segoe UI", 10);
    SolidBrush grey(Gdiplus::Color(160,160,170));
    StringFormat lft; lft.SetAlignment(StringAlignmentNear);
    graphics->DrawString(L"Move History", -1, &labelFont,
        RectF((float)(px+14),(float)py, 200, 18), &lft, &grey);
    py += 20;

    // Background
    SolidBrush histBg(Gdiplus::Color(25,25,32));
    int histH = WINDOW_H - py - 10;
    graphics->FillRectangle(&histBg, px+14, py, SIDEBAR_W-28, histH);
    Pen histEdge(Gdiplus::Color(60,60,70), 1);
    graphics->DrawRectangle(&histEdge, px+14, py, SIDEBAR_W-28, histH);

    Font moveFont(L"Consolas", 10);
    SolidBrush white(Gdiplus::Color(220,220,220));
    SolidBrush altBg(Gdiplus::Color(32,32,40));
    StringFormat lf2; lf2.SetAlignment(StringAlignmentNear); lf2.SetLineAlignment(StringAlignmentCenter);

    // Show last N pairs that fit
    int lineH = 18;
    int maxLines = histH / lineH - 1;
    int totalPairs = ((int)moveHistory.size() + 1) / 2;
    int startPair = std::max(0, totalPairs - maxLines);

    int ly = py + 3;
    for (int pair = startPair; pair < totalPairs && ly + lineH < py + histH; pair++) {
        int w0 = pair * 2;
        int w1 = pair * 2 + 1;
        std::wstring line;
        wchar_t buf[64];
        swprintf_s(buf, L"%d.", pair + 1);
        line = buf;
        if (w0 < (int)moveHistory.size()) line += L" " + moveHistory[w0];
        if (w1 < (int)moveHistory.size()) line += L"  " + moveHistory[w1];

        // Alternate row shading
        if ((pair - startPair) % 2 == 1) {
            graphics->FillRectangle(&altBg, px+15, ly, SIDEBAR_W-30, lineH);
        }
        graphics->DrawString(line.c_str(), -1, &moveFont,
            RectF((float)(px+18),(float)ly,(float)(SIDEBAR_W-36),(float)lineH), &lf2, &white);
        ly += lineH;
    }
}

void ChessGraphics::DrawPieces(Graphics* graphics) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            Piece* piece = board->getPiece(row, col);
            if (piece) {
                int x = BOARD_OFFSET_X + col * SQUARE_SIZE;
                int y = BOARD_OFFSET_Y + row * SQUARE_SIZE;
                DrawPiece(graphics, piece, x, y);
            }
        }
    }
}

void ChessGraphics::DrawPiece(Graphics* graphics, Piece* piece, int x, int y) {
    bool isWhite = (piece->getColor() == PieceColor::WHITE);
    int colorIdx = isWhite ? 0 : 1;

    int typeIdx = -1;
    switch (piece->getType()) {
        case PieceType::KING:   typeIdx = 0; break;
        case PieceType::QUEEN:  typeIdx = 1; break;
        case PieceType::ROOK:   typeIdx = 2; break;
        case PieceType::BISHOP: typeIdx = 3; break;
        case PieceType::KNIGHT: typeIdx = 4; break;
        case PieceType::PAWN:   typeIdx = 5; break;
        default: return;
    }

    if (piecesLoaded && typeIdx >= 0 && pieceImages[typeIdx][colorIdx]) {
        // Draw image with a small margin so pieces don't touch square edges
        int margin = 4;
        graphics->DrawImage(pieceImages[typeIdx][colorIdx],
            x + margin, y + margin,
            SQUARE_SIZE - 2 * margin, SQUARE_SIZE - 2 * margin);
        return;
    }

    // Fallback: vector-drawn pieces
    Gdiplus::Color pieceColor = isWhite ? Gdiplus::Color(255, 255, 255) : Gdiplus::Color(0, 0, 0);
    int centerX = x + SQUARE_SIZE / 2;
    int centerY = y + SQUARE_SIZE / 2;

    switch (piece->getType()) {
        case PieceType::KING:   DrawKing(graphics, pieceColor, centerX, centerY, isWhite);   break;
        case PieceType::QUEEN:  DrawQueen(graphics, pieceColor, centerX, centerY, isWhite);  break;
        case PieceType::ROOK:   DrawRook(graphics, pieceColor, centerX, centerY, isWhite);   break;
        case PieceType::BISHOP: DrawBishop(graphics, pieceColor, centerX, centerY, isWhite); break;
        case PieceType::KNIGHT: DrawKnight(graphics, pieceColor, centerX, centerY, isWhite); break;
        case PieceType::PAWN:   DrawPawn(graphics, pieceColor, centerX, centerY, isWhite);   break;
        default: break;
    }
}

void ChessGraphics::DrawKing(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillEllipse(&brush, x - 20, y + 10, 40, 15);
    graphics->DrawEllipse(&outline, x - 20, y + 10, 40, 15);
    
    // Body
    graphics->FillRectangle(&brush, x - 15, y - 10, 30, 20);
    graphics->DrawRectangle(&outline, x - 15, y - 10, 30, 20);
    
    // Crown
    Point points[] = {
        {x - 18, y - 10}, {x - 10, y - 20}, {x, y - 15},
        {x + 10, y - 20}, {x + 18, y - 10}
    };
    graphics->FillPolygon(&brush, points, 5);
    graphics->DrawPolygon(&outline, points, 5);
    
    // Cross on top
    graphics->FillRectangle(&brush, x - 2, y - 28, 4, 10);
    graphics->FillRectangle(&brush, x - 6, y - 24, 12, 4);
    graphics->DrawRectangle(&outline, x - 2, y - 28, 4, 10);
    graphics->DrawRectangle(&outline, x - 6, y - 24, 12, 4);
}

void ChessGraphics::DrawQueen(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillEllipse(&brush, x - 20, y + 10, 40, 15);
    graphics->DrawEllipse(&outline, x - 20, y + 10, 40, 15);
    
    // Body
    Point body[] = {{x - 18, y - 10}, {x - 12, y + 10}, {x + 12, y + 10}, {x + 18, y - 10}};
    graphics->FillPolygon(&brush, body, 4);
    graphics->DrawPolygon(&outline, body, 4);
    
    // Crown balls
    for (int i = -2; i <= 2; i++) {
        graphics->FillEllipse(&brush, x + i * 9 - 4, y - 20, 8, 8);
        graphics->DrawEllipse(&outline, x + i * 9 - 4, y - 20, 8, 8);
    }
}

void ChessGraphics::DrawRook(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillRectangle(&brush, x - 20, y + 10, 40, 15);
    graphics->DrawRectangle(&outline, x - 20, y + 10, 40, 15);
    
    // Tower body
    graphics->FillRectangle(&brush, x - 15, y - 15, 30, 25);
    graphics->DrawRectangle(&outline, x - 15, y - 15, 30, 25);
    
    // Battlements
    graphics->FillRectangle(&brush, x - 18, y - 25, 36, 10);
    graphics->DrawRectangle(&outline, x - 18, y - 25, 36, 10);
    
    // Notches
    SolidBrush notchBrush(Gdiplus::Color(100, 100, 100));
    graphics->FillRectangle(&notchBrush, x - 12, y - 25, 6, 6);
    graphics->FillRectangle(&notchBrush, x - 3, y - 25, 6, 6);
    graphics->FillRectangle(&notchBrush, x + 6, y - 25, 6, 6);
}

void ChessGraphics::DrawBishop(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillEllipse(&brush, x - 18, y + 10, 36, 15);
    graphics->DrawEllipse(&outline, x - 18, y + 10, 36, 15);
    
    // Body - teardrop shape
    Point body[] = {{x, y - 20}, {x - 12, y + 10}, {x + 12, y + 10}};
    graphics->FillPolygon(&brush, body, 3);
    graphics->DrawPolygon(&outline, body, 3);
    
    // Top ball
    graphics->FillEllipse(&brush, x - 5, y - 25, 10, 10);
    graphics->DrawEllipse(&outline, x - 5, y - 25, 10, 10);
    
    // Slit in middle
    Pen slitPen(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 3);
    graphics->DrawLine(&slitPen, x - 5, y, x + 5, y);
}

void ChessGraphics::DrawKnight(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillEllipse(&brush, x - 18, y + 10, 36, 15);
    graphics->DrawEllipse(&outline, x - 18, y + 10, 36, 15);
    
    // Horse head profile
    Point head[] = {
        {x - 10, y + 10}, {x - 15, y}, {x - 12, y - 15},
        {x - 5, y - 20}, {x + 5, y - 15}, {x + 8, y - 5},
        {x + 10, y}, {x + 5, y + 10}
    };
    graphics->FillPolygon(&brush, head, 8);
    graphics->DrawPolygon(&outline, head, 8);
    
    // Eye
    SolidBrush eyeBrush(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255));
    graphics->FillEllipse(&eyeBrush, x - 5, y - 10, 3, 3);
    
    // Mane
    for (int i = 0; i < 3; i++) {
        graphics->DrawLine(&outline, x - 8 + i * 3, y - 12 + i * 2, x - 8 + i * 3, y - 5 + i * 2);
    }
}

void ChessGraphics::DrawPawn(Graphics* graphics, Gdiplus::Color color, int x, int y, bool isWhite) {
    SolidBrush brush(color);
    Pen outline(isWhite ? Gdiplus::Color(0, 0, 0) : Gdiplus::Color(255, 255, 255), 2);
    
    // Base
    graphics->FillEllipse(&brush, x - 15, y + 10, 30, 15);
    graphics->DrawEllipse(&outline, x - 15, y + 10, 30, 15);
    
    // Stem
    graphics->FillRectangle(&brush, x - 8, y - 5, 16, 15);
    graphics->DrawRectangle(&outline, x - 8, y - 5, 16, 15);
    
    // Head
    graphics->FillEllipse(&brush, x - 10, y - 18, 20, 20);
    graphics->DrawEllipse(&outline, x - 10, y - 18, 20, 20);
}

void ChessGraphics::HandleClick(int screenX, int screenY) {
    // Check difficulty button clicks in sidebar
    if (screenX >= SIDEBAR_X) {
        // Sidebar click — check difficulty buttons
        // panelY for difficulty section mirrors DrawSidebar layout:
        // y=60 (turn badge top), +48 sep, +58 evalbar, +70 captured → +230 difficulty
        int diffPanelY = 60 + 230;
        AIStrength chosen;
        if (ClickedDifficultyButton(screenX, screenY, SIDEBAR_X, diffPanelY, chosen)) {
            currentStrength = chosen;
            SetAIStrength(chosen);
            const wchar_t* titles[] = {
                L"Chess - AI: Easy", L"Chess - AI: Medium",
                L"Chess - AI: Hard (3s)", L"Chess - AI: Expert (8s)"
            };
            int idx = (chosen==AIStrength::EASY)?0:(chosen==AIStrength::MEDIUM)?1:(chosen==AIStrength::HARD)?2:3;
            SetWindowTextW(hwnd, titles[idx]);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }

    if (waitingForAI) return;
    if (aiMode && board->getCurrentTurn() != playerColor) return;

    Position clickedPos = ScreenToBoard(screenX, screenY);
    if (!clickedPos.isValid()) return;

    if (!hasSelection) {
        Piece* piece = board->getPiece(clickedPos);
        if (piece && piece->getColor() == board->getCurrentTurn()) {
            selectedSquare = clickedPos;
            hasSelection = true;
            highlightedSquares = board->getAllValidMoves(clickedPos);
        }
    } else {
        if (clickedPos == selectedSquare) {
            hasSelection = false;
            highlightedSquares.clear();
        } else {
            // Record capture before move
            Piece* target = board->getPiece(clickedPos);
            PieceColor moverColor = board->getCurrentTurn();

            if (board->movePiece(selectedSquare, clickedPos)) {
                // Track last move highlight
                lastMoveFrom = selectedSquare;
                lastMoveTo = clickedPos;
                hasLastMove = true;

                // Track captured piece
                if (target) {
                    if (moverColor == PieceColor::WHITE)
                        capturedByWhite.push_back(target->getType());
                    else
                        capturedByBlack.push_back(target->getType());
                }

                // Build move string for history
                char files[] = "abcdefgh";
                std::wstring mv = PieceSymbol(board->getPiece(clickedPos) ? board->getPiece(clickedPos)->getType() : PieceType::PAWN);
                wchar_t buf[16];
                swprintf_s(buf, L"%c%d", (wchar_t)(L'a'+selectedSquare.col), 8-selectedSquare.row);
                // simpler: just show squares
                wchar_t movBuf[16];
                swprintf_s(movBuf, L"%c%d-%c%d",
                    (wchar_t)(L'a'+selectedSquare.col), 8-selectedSquare.row,
                    (wchar_t)(L'a'+clickedPos.col), 8-clickedPos.row);
                if (target) { std::wstring s(movBuf); s += L"x"; moveHistory.push_back(s); }
                else moveHistory.push_back(std::wstring(movBuf));

                std::cout << "\n[PLAYER MOVE] "
                    << files[selectedSquare.col] << (8-selectedSquare.row)
                    << " -> "
                    << files[clickedPos.col] << (8-clickedPos.row) << "\n";

                hasSelection = false;
                highlightedSquares.clear();

                PieceColor nextColor = board->getCurrentTurn();
                if (board->isCheckmate(nextColor)) {
                    const wchar_t* winner = (nextColor == PieceColor::WHITE)
                        ? L"Black Wins by Checkmate!\n\nPlay Again?"
                        : L"White Wins by Checkmate!\n\nPlay Again?";
                    int res = MessageBoxW(hwnd, winner, L"Game Over",
                        MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
                    if (res == IDYES) ResetGame();
                } else if (board->isStalemate(nextColor)) {
                    int res = MessageBoxW(hwnd, L"Stalemate — Draw!\n\nPlay Again?", L"Game Over",
                        MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
                    if (res == IDYES) ResetGame();
                } else if (aiMode && nextColor != playerColor) {
                    waitingForAI = true;
                    RequestAIMove();
                }
            } else {
                Piece* piece = board->getPiece(clickedPos);
                if (piece && piece->getColor() == board->getCurrentTurn()) {
                    selectedSquare = clickedPos;
                    highlightedSquares = board->getAllValidMoves(clickedPos);
                } else {
                    hasSelection = false;
                    highlightedSquares.clear();
                }
            }
        }
    }
}

Position ChessGraphics::ScreenToBoard(int screenX, int screenY) {
    int col = (screenX - BOARD_OFFSET_X) / SQUARE_SIZE;
    int row = (screenY - BOARD_OFFSET_Y) / SQUARE_SIZE;
    
    if (col < 0 || col >= 8 || row < 0 || row >= 8) {
        return Position(-1, -1);
    }
    
    return Position(row, col);
}

void ChessGraphics::GetSquareCoords(Position pos, int& x, int& y) {
    x = BOARD_OFFSET_X + pos.col * SQUARE_SIZE;
    y = BOARD_OFFSET_Y + pos.row * SQUARE_SIZE;
}

// AI Control Methods
void ChessGraphics::SetAIMode(bool enabled, PieceColor humanColor, AIStrength strength) {
    aiMode = enabled;
    playerColor = humanColor;
    currentStrength = strength;
    // Clear game state for new game
    capturedByWhite.clear();
    capturedByBlack.clear();
    moveHistory.clear();
    halfMoveCount = 0;
    hasLastMove = false;
    lastEvalCentipawns = 0;
    
    if (enabled) {
        if (!aiPlayer) {
            aiPlayer = new AIPlayer(board, strength);
        } else {
            aiPlayer->setStrength(strength);
        }
        
        // If AI plays first (black if player is white), start AI move
        if (board->getCurrentTurn() != playerColor) {
            RequestAIMove();
        }
    } else {
        if (aiPlayer) {
            delete aiPlayer;
            aiPlayer = nullptr;
        }
        waitingForAI = false;
    }
}

void ChessGraphics::PrintBoardState() {
    std::cout << "\nCurrent Board:\n";
    std::cout << "  a b c d e f g h\n";
    for (int row = 0; row < 8; row++) {
        std::cout << (8 - row) << " ";
        for (int col = 0; col < 8; col++) {
            Piece* p = board->getPiece(row, col);
            if (!p) {
                std::cout << ". ";
            } else {
                char symbol = ' ';
                switch (p->getType()) {
                    case PieceType::PAWN:   symbol = 'P'; break;
                    case PieceType::ROOK:   symbol = 'R'; break;
                    case PieceType::KNIGHT: symbol = 'N'; break;
                    case PieceType::BISHOP: symbol = 'B'; break;
                    case PieceType::QUEEN:  symbol = 'Q'; break;
                    case PieceType::KING:   symbol = 'K'; break;
                }
                if (p->getColor() == PieceColor::BLACK) {
                    symbol = tolower(symbol);
                }
                std::cout << symbol << " ";
            }
        }
        std::cout << (8 - row) << "\n";
    }
    std::cout << "  a b c d e f g h\n\n";
}

void ChessGraphics::SetAIStrength(AIStrength strength) {
    currentStrength = strength;
    if (aiPlayer) {
        aiPlayer->setStrength(strength);
    }
}

bool ChessGraphics::IsAIThinking() const {
    return waitingForAI;
}

void ChessGraphics::RequestAIMove() {
    if (!aiPlayer) {
        return;
    }
    
    // waitingForAI should already be set by the caller to prevent race conditions
    if (!waitingForAI) {
        waitingForAI = true;
    }
    InvalidateRect(hwnd, NULL, FALSE);  // Redraw to show "AI thinking..."
    
    // Request AI move (async - runs on background thread)
    aiPlayer->requestMove([this](Position from, Position to) {
        // This callback runs when AI finishes thinking
        OnAIMove(from, to);
    });
}

void ChessGraphics::OnAIMove(Position from, Position to) {
    // This is called from background thread - don't call GUI functions directly!
    // Store the move and post message to UI thread
    pendingAIFrom = from;
    pendingAITo = to;
    
    // Post message to UI thread to process the move
    PostMessage(hwnd, WM_AI_MOVE, 0, 0);
}

void ChessGraphics::ProcessAIMove() {
    std::cout << "\n[PROCESSING AI MOVE ON UI THREAD]\n";
    waitingForAI = false;

    Position from = pendingAIFrom;
    Position to   = pendingAITo;

    char files[] = "abcdefgh";
    std::cout << "[AI MOVE] "
              << files[from.col] << (8 - from.row)
              << " -> "
              << files[to.col]  << (8 - to.row) << "\n";

    // Track capture before move
    Piece* target = board->getPiece(to);
    PieceColor moverColor = board->getCurrentTurn();

    if (board->movePiece(from, to)) {
        // Last move highlight
        lastMoveFrom = from;
        lastMoveTo   = to;
        hasLastMove  = true;

        // Captured piece tracking
        if (target) {
            if (moverColor == PieceColor::WHITE)
                capturedByWhite.push_back(target->getType());
            else
                capturedByBlack.push_back(target->getType());
        }

        // Move history
        wchar_t movBuf[16];
        swprintf_s(movBuf, L"%c%d-%c%d",
            (wchar_t)(L'a'+from.col), 8-from.row,
            (wchar_t)(L'a'+to.col),  8-to.row);
        std::wstring entry(movBuf);
        if (target) entry += L"x";
        moveHistory.push_back(entry);

        std::cout << "Move executed successfully!\n";
        PrintBoardState();
        InvalidateRect(hwnd, NULL, FALSE);

        PieceColor nextColor = board->getCurrentTurn();
        if (board->isCheckmate(nextColor)) {
            std::cout << "\n*** CHECKMATE! ***\n";
            const wchar_t* winner = (nextColor == PieceColor::WHITE)
                ? L"Black Wins by Checkmate!\n\nPlay Again?"
                : L"White Wins by Checkmate!\n\nPlay Again?";
            int res = MessageBoxW(hwnd, winner, L"Game Over",
                MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
            if (res == IDYES) ResetGame();
        } else if (board->isStalemate(nextColor)) {
            std::cout << "\n*** STALEMATE! ***\n";
            int res = MessageBoxW(hwnd, L"Stalemate — Draw!\n\nPlay Again?", L"Game Over",
                MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
            if (res == IDYES) ResetGame();
        }
    } else {
        std::cout << "ERROR: AI move failed!\n";
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void ChessGraphics::ResetOpeningBook() {
    if (aiPlayer) {
        aiPlayer->resetBook();
    }
}

void ChessGraphics::ResetGame() {
    // Stop any ongoing AI search
    if (aiPlayer) aiPlayer->stopThinking();
    waitingForAI = false;

    // Reset board to starting position
    board->initialize();

    // Clear sidebar state
    capturedByWhite.clear();
    capturedByBlack.clear();
    moveHistory.clear();
    halfMoveCount = 0;
    hasLastMove   = false;
    lastEvalCentipawns = 0;

    // Clear selection
    hasSelection = false;
    highlightedSquares.clear();

    // Re-enable opening book
    ResetOpeningBook();

    // Trigger AI if it moves first
    if (aiMode && board->getCurrentTurn() != playerColor) {
        waitingForAI = true;
        RequestAIMove();
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Double buffering
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, WINDOW_W, WINDOW_H);
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
            
            if (g_graphics) {
                g_graphics->Draw(hdcMem);
            }
            
            BitBlt(hdc, 0, 0, WINDOW_W, WINDOW_H, hdcMem, 0, 0, SRCCOPY);
            
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (g_graphics) {
                g_graphics->HandleClick(x, y);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                PostQuitMessage(0);
            } else if (wParam == 'A' || wParam == 'a') {
                // Toggle AI mode with difficulty selection
                if (g_graphics) {
                    int result = MessageBox(hwnd,
                        TEXT("Enable AI opponent?\n\n"
                             "After enabling, use number keys to set difficulty:\n"
                             "  1 = Easy   (depth 3)\n"
                             "  2 = Medium (depth 6)\n"
                             "  3 = Hard   (3 seconds)\n"
                             "  4 = Expert (8 seconds)"),
                        TEXT("AI Mode"),
                        MB_YESNO | MB_ICONQUESTION);
                    if (result == IDYES) {
                        g_graphics->SetAIMode(true, PieceColor::WHITE, AIStrength::MEDIUM);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            } else if (wParam == 'N' || wParam == 'n') {
                // New game with difficulty selection
                if (g_board && g_graphics) {
                    g_board->initialize();
                    g_graphics->ResetOpeningBook();
                    
                    int result = MessageBox(hwnd,
                        TEXT("Play against AI?\n\n"
                             "After clicking Yes, press a number key to set difficulty:\n"
                             "  1 = Easy   (depth 3)\n"
                             "  2 = Medium (depth 6)\n"
                             "  3 = Hard   (3 seconds)\n"
                             "  4 = Expert (8 seconds)\n\n"
                             "Default: Medium"),
                        TEXT("New Game"),
                        MB_YESNO | MB_ICONQUESTION);
                    if (result == IDYES) {
                        g_graphics->SetAIMode(true, PieceColor::WHITE, AIStrength::MEDIUM);
                    } else {
                        g_graphics->SetAIMode(false);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (wParam == '1') {
                // Easy difficulty
                if (g_graphics) {
                    g_graphics->SetAIStrength(AIStrength::EASY);
                    SetWindowText(hwnd, TEXT("Chess - AI: Easy (depth 3)"));
                    MessageBox(hwnd, TEXT("Difficulty set to EASY (depth 3)"), TEXT("Difficulty"), MB_OK | MB_ICONINFORMATION);
                }
            } else if (wParam == '2') {
                // Medium difficulty
                if (g_graphics) {
                    g_graphics->SetAIStrength(AIStrength::MEDIUM);
                    SetWindowText(hwnd, TEXT("Chess - AI: Medium (depth 6)"));
                    MessageBox(hwnd, TEXT("Difficulty set to MEDIUM (depth 6)"), TEXT("Difficulty"), MB_OK | MB_ICONINFORMATION);
                }
            } else if (wParam == '3') {
                // Hard difficulty
                if (g_graphics) {
                    g_graphics->SetAIStrength(AIStrength::HARD);
                    SetWindowText(hwnd, TEXT("Chess - AI: Hard (3 seconds/move)"));
                    MessageBox(hwnd, TEXT("Difficulty set to HARD (3 seconds per move)"), TEXT("Difficulty"), MB_OK | MB_ICONINFORMATION);
                }
            } else if (wParam == '4') {
                // Expert difficulty
                if (g_graphics) {
                    g_graphics->SetAIStrength(AIStrength::EXPERT);
                    SetWindowText(hwnd, TEXT("Chess - AI: Expert (8 seconds/move)"));
                    MessageBox(hwnd, TEXT("Difficulty set to EXPERT (8 seconds per move)"), TEXT("Difficulty"), MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_AI_MOVE: {
            // Process AI move on UI thread
            if (g_graphics) {
                g_graphics->ProcessAIMove();
            }
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
