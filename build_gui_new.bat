@echo off
echo Building Chess GUI with AI (New Structure)...

REM Compile source files
set OPT=-std=c++17 -O3 -march=native -funroll-loops
g++ -c src/core/Piece.cpp       -o build/Piece.o       -Iinclude %OPT%
g++ -c src/core/Board.cpp       -o build/Board.o       -Iinclude %OPT%
g++ -c src/ui/Game.cpp          -o build/Game.o        -Iinclude %OPT%
g++ -c src/ui/Graphics.cpp      -o build/Graphics.o    -Iinclude %OPT%
g++ -c src/search/Search.cpp    -o build/Search.o      -Iinclude %OPT%
g++ -c src/search/OpeningBook.cpp -o build/OpeningBook.o -Iinclude %OPT%
g++ -c src/search/AIPlayer.cpp   -o build/AIPlayer.o   -Iinclude %OPT%
g++ -c src/main_gui.cpp         -o build/main_gui.o    -Iinclude %OPT%

REM Link (with console for logging)
g++ build/Piece.o build/Board.o build/Game.o build/Graphics.o build/Search.o build/OpeningBook.o build/AIPlayer.o build/main_gui.o -o build/chess_gui.exe -lgdiplus -lgdi32 -luser32 -mconsole %OPT%

if exist build\chess_gui.exe (
    echo.
    echo [32mBuild successful![0m
    echo Executable: build\chess_gui.exe
    echo.
    echo To run: build\chess_gui.exe
    echo.
    echo Controls:
    echo - Click to select and move pieces
    echo - 'N' key - New game
    echo - 'A' key - Toggle AI mode
    echo - ESC - Exit
) else (
    echo.
    echo [31mBuild failed![0m
)
