#include <windows.h>
#include "ui/Graphics.h"
#include "core/Board.h"
#include <iostream>

Board* g_board = nullptr;
ChessGraphics* g_graphics = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = TEXT("ChessWindowClass");
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Create window
    HWND hwnd = CreateWindowEx(
        0,
        TEXT("ChessWindowClass"),
        TEXT("Chess - AI: Hard (3s/move)"),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_W + 16, WINDOW_H + 39,  // sidebar + borders
        NULL, NULL, hInstance, NULL
    );
    
    if (hwnd == NULL) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Initialize board and graphics
    g_board = new Board();
    g_board->initialize();
    
    std::cout << "\n========================================\n";
    std::cout << "    CHESS GAME - AI EDITION\n";
    std::cout << "========================================\n";
    std::cout << "Game initialized successfully!\n";
    std::cout << "AI Difficulty: HARD (3 seconds/move) - Press 1/2/3/4 to change\n";
    std::cout << "========================================\n\n";
    
    g_graphics = new ChessGraphics(hwnd, g_board);
    g_graphics->Initialize();
    
    // Ask user if they want to play against AI
    int result = MessageBox(hwnd, 
        TEXT("Do you want to play against the computer?\n\n")
        TEXT("Difficulty is set to HARD (3 sec/move) by default.\n\n")
        TEXT("Change difficulty anytime with number keys:\n")
        TEXT("  1 = Easy   (depth 3)\n")
        TEXT("  2 = Medium (depth 6)\n")
        TEXT("  3 = Hard   (3 seconds)\n")
        TEXT("  4 = Expert (8 seconds)\n\n")
        TEXT("Other shortcuts:\n")
        TEXT("  N = New Game  |  ESC = Exit"),
        TEXT("Game Mode"),
        MB_YESNO | MB_ICONQUESTION);
    
    if (result == IDYES) {
        // Enable AI mode - player is white, AI is black, HARD difficulty
        g_graphics->SetAIMode(true, PieceColor::WHITE, AIStrength::HARD);
        SetWindowText(hwnd, TEXT("Chess - AI: Hard (3 seconds/move)"));
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    // Message loop
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    delete g_graphics;
    delete g_board;
    
    return (int)msg.wParam;
}
