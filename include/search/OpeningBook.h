#ifndef OPENING_BOOK_H
#define OPENING_BOOK_H

#include "core/Piece.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <random>

// Forward declaration
class Board;

struct BookEntry {
    Position from;
    Position to;
    int weight;
    
    BookEntry(Position f, Position t, int w);
};

class OpeningBook {
private:
    // Map from FEN position string to list of candidate moves with weights
    std::unordered_map<std::string, std::vector<BookEntry>> bookMap;
    std::mt19937 rng;
    bool loaded;
    
    // Parse coordinate notation (e.g. "e2e4") into from/to positions
    bool parseMove(const std::string& moveStr, Position& from, Position& to) const;
    
public:
    OpeningBook();
    
    // Load opening book from file
    bool loadBook(const std::string& filename);
    
    // Probe the book for the current position
    // Returns true if a book move was found, fills from/to
    bool probeBook(Board* board, Position& from, Position& to);
    
    // Get number of positions in book
    size_t size() const { return bookMap.size(); }
    
    bool isLoaded() const { return loaded; }
};

#endif
