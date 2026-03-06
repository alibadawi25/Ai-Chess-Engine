#include "search/OpeningBook.h"
#include "core/Board.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>

BookEntry::BookEntry(Position f, Position t, int w)
    : from(f), to(t), weight(w) {}

OpeningBook::OpeningBook() 
    : loaded(false) {
    // Seed RNG with current time for variety
    auto seed = static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    rng.seed(seed);
}

bool OpeningBook::parseMove(const std::string& moveStr, Position& from, Position& to) const {
    // Format: "e2e4" - 4 characters, col as letter, row as digit
    if (moveStr.length() < 4) return false;
    
    int fromCol = moveStr[0] - 'a';
    int fromRow = 8 - (moveStr[1] - '0');  // '1' -> row 7, '8' -> row 0
    int toCol = moveStr[2] - 'a';
    int toRow = 8 - (moveStr[3] - '0');
    
    if (fromCol < 0 || fromCol > 7 || fromRow < 0 || fromRow > 7 ||
        toCol < 0 || toCol > 7 || toRow < 0 || toRow > 7) {
        return false;
    }
    
    from = Position(fromRow, fromCol);
    to = Position(toRow, toCol);
    return true;
}

bool OpeningBook::loadBook(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "[Book] Failed to open: " << filename << std::endl;
        
        // Try alternative paths
        std::vector<std::string> altPaths = {
            "../resources/Book.txt",
            "resources/Book.txt",
            "Book.txt"
        };
        
        for (const auto& path : altPaths) {
            file.open(path);
            if (file.is_open()) {
                std::cout << "[Book] Found at: " << path << std::endl;
                break;
            }
        }
        
        if (!file.is_open()) {
            std::cout << "[Book] Opening book not found!" << std::endl;
            return false;
        }
    }
    
    std::string line;
    std::string currentFEN;
    int posCount = 0;
    int moveCount = 0;
    
    while (std::getline(file, line)) {
        // Skip empty lines
        if (line.empty()) continue;
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        if (line.substr(0, 4) == "pos ") {
            // New position - extract FEN string
            currentFEN = line.substr(4);
            // Trim trailing whitespace
            size_t end = currentFEN.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) {
                currentFEN = currentFEN.substr(0, end + 1);
            }
            posCount++;
        } else if (!currentFEN.empty()) {
            // Move line: "e2e4 243109"
            std::istringstream iss(line);
            std::string moveStr;
            int weight;
            
            if (iss >> moveStr >> weight) {
                Position from, to;
                if (parseMove(moveStr, from, to)) {
                    bookMap[currentFEN].emplace_back(from, to, weight);
                    moveCount++;
                }
            }
        }
    }
    
    file.close();
    loaded = true;
    
    std::cout << "[Book] Loaded " << posCount << " positions, " 
              << moveCount << " moves" << std::endl;
    
    return true;
}

bool OpeningBook::probeBook(Board* board, Position& from, Position& to) {
    if (!loaded || bookMap.empty()) return false;
    
    // Generate FEN for current position
    std::string fen = board->toFEN();
    
    std::cout << "[Book] Looking up FEN: " << fen << std::endl;
    
    auto it = bookMap.find(fen);
    if (it == bookMap.end()) {
        return false;
    }
    
    const auto& entries = it->second;
    if (entries.empty()) return false;
    
    // Weighted random selection
    int totalWeight = 0;
    for (const auto& entry : entries) {
        totalWeight += entry.weight;
    }
    
    if (totalWeight <= 0) return false;
    
    std::uniform_int_distribution<int> dist(0, totalWeight - 1);
    int pick = dist(rng);
    
    int cumulative = 0;
    for (const auto& entry : entries) {
        cumulative += entry.weight;
        if (pick < cumulative) {
            from = entry.from;
            to = entry.to;
            
            // Log the book move
            char fromFile = 'a' + from.col;
            char fromRank = '0' + (8 - from.row);
            char toFile = 'a' + to.col;
            char toRank = '0' + (8 - to.row);
            
            std::cout << "[Book] Playing " << fromFile << fromRank 
                      << toFile << toRank;
            
            // Show probability
            double pct = 100.0 * entry.weight / totalWeight;
            std::cout << " (weight: " << entry.weight 
                      << "/" << totalWeight 
                      << " = " << static_cast<int>(pct) << "%)"
                      << " from " << entries.size() << " candidates" 
                      << std::endl;
            
            return true;
        }
    }
    
    // Fallback: pick last entry
    from = entries.back().from;
    to = entries.back().to;
    return true;
}
