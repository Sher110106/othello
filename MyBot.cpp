/**
 * MyBot.cpp - Hybrid Othello Bot Implementation
 * 
 * Architecture:
 * - Perspective-agnostic alpha-beta with iterative deepening
 * - Dynamic phase-based evaluation weights
 * - Zobrist hashing with transposition table
 * - Sophisticated move ordering with X-square avoidance
 * - Adaptive depth based on game phase
 */

#include "MyBot.h"
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <random>
#include <limits>
#include <cstring>

using namespace std;

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

const int INF = 1000000;
const double TIME_LIMIT = 1.75; // Conservative buffer (0.25s safety margin)
const int MAX_DEPTH = 20;

// Standard Othello position weights matrix
// Corners (100) > Edges (10) > Center (low) > X-squares (-50)
const int POSITION_WEIGHTS[8][8] = {
    {100, -20, 10,  5,  5, 10, -20, 100},
    {-20, -50, -2, -2, -2, -2, -50, -20},
    { 10,  -2,  1,  0,  0,  1,  -2,  10},
    {  5,  -2,  0, -1, -1,  0,  -2,   5},
    {  5,  -2,  0, -1, -1,  0,  -2,   5},
    { 10,  -2,  1,  0,  0,  1,  -2,  10},
    {-20, -50, -2, -2, -2, -2, -50, -20},
    {100, -20, 10,  5,  5, 10, -20, 100}
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Phase-based evaluation weights
struct Weights {
    double position;
    double mobility;
    double corner;
    double stability;
    double parity;
};

// Transposition table entry
struct TTEntry {
    uint64_t hash;
    int depth;
    int score;
    Move bestMove;
    
    TTEntry() : hash(0), depth(-1), score(0), bestMove(0, 0) {}
    TTEntry(uint64_t h, int d, int s, Move m) 
        : hash(h), depth(d), score(s), bestMove(m) {}
};

// ============================================================================
// ZOBRIST HASHING
// ============================================================================

class ZobristHash {
private:
    uint64_t zobristTable[64][3]; // [position][BLACK=0/RED=1/EMPTY=2]
    
public:
    ZobristHash() {
        mt19937_64 rng(314159265); // Fixed seed for reproducibility
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 3; j++) {
                zobristTable[i][j] = rng();
            }
        }
    }
    
    // Hash board state based on piece positions
    // We'll use getValidMoves pattern and counts as proxy
    uint64_t hash(const OthelloBoard& board) const {
        uint64_t h = 0;
        
        // Use piece counts and their distribution
        int blackCount = board.getBlackCount();
        int redCount = board.getRedCount();
        
        // Hash based on counts
        h ^= zobristTable[blackCount % 64][0];
        h ^= zobristTable[redCount % 64][1];
        
        // Add move pattern hashing
        list<Move> blackMoves = board.getValidMoves(BLACK);
        list<Move> redMoves = board.getValidMoves(RED);
        
        for (const Move& m : blackMoves) {
            int idx = m.x * 8 + m.y;
            h ^= zobristTable[idx % 64][0];
        }
        
        for (const Move& m : redMoves) {
            int idx = m.x * 8 + m.y;
            h ^= zobristTable[idx % 64][1];
        }
        
        return h;
    }
};

// ============================================================================
// GLOBAL STATE (for search management)
// ============================================================================

static unordered_map<uint64_t, TTEntry> transpositionTable;
static ZobristHash zobrist;
static chrono::time_point<chrono::high_resolution_clock> searchStartTime;
static bool timeoutFlag;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

inline Turn opponent(Turn t) {
    return (t == BLACK) ? RED : BLACK;
}

inline int emptySquares(const OthelloBoard& board) {
    return 64 - board.getBlackCount() - board.getRedCount();
}

inline bool isTimeUp() {
    if (timeoutFlag) return true;
    
    auto now = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double>(now - searchStartTime).count();
    
    if (elapsed >= TIME_LIMIT) {
        timeoutFlag = true;
        return true;
    }
    return false;
}

inline bool isCorner(const Move& m) {
    return (m.x == 0 && m.y == 0) || (m.x == 0 && m.y == 7) ||
           (m.x == 7 && m.y == 0) || (m.x == 7 && m.y == 7);
}

inline bool isXSquare(const Move& m) {
    // X-squares are diagonally adjacent to corners (dangerous early game)
    return (m.x == 1 && m.y == 1) || (m.x == 1 && m.y == 6) ||
           (m.x == 6 && m.y == 1) || (m.x == 6 && m.y == 6);
}

inline bool isCSquare(const Move& m) {
    // C-squares are orthogonally adjacent to corners
    return ((m.x == 0 || m.x == 7) && (m.y == 1 || m.y == 6)) ||
           ((m.y == 0 || m.y == 7) && (m.x == 1 || m.x == 6));
}

inline bool isEdge(const Move& m) {
    return (m.x == 0 || m.x == 7 || m.y == 0 || m.y == 7);
}

// Get phase-based evaluation weights
Weights getWeights(int empty) {
    if (empty > 48) { // Opening (0-16 moves)
        return {0.40, 0.35, 0.15, 0.10, 0.00};
    } else if (empty > 20) { // Midgame (16-44 moves)
        return {0.25, 0.25, 0.20, 0.20, 0.10};
    } else { // Endgame (44+ moves)
        return {0.10, 0.05, 0.15, 0.20, 0.50};
    }
}

// ============================================================================
// EVALUATION FUNCTION
// ============================================================================

// Evaluate corner control (critical for stability)
int evaluateCorners(const OthelloBoard& board, Turn perspective) {
    int score = 0;
    Turn opp = opponent(perspective);
    
    // Check all four corners by trying to validate moves (indirect check)
    // A corner is occupied if neither player can play there
    vector<pair<int, int>> corners = {{0,0}, {0,7}, {7,0}, {7,7}};
    
    for (auto& corner : corners) {
        Move m(corner.first, corner.second);
        
        bool perspectiveCanPlay = board.validateMove(perspective, m);
        bool opponentCanPlay = board.validateMove(opp, m);
        
        // If neither can play, the corner is occupied
        if (!perspectiveCanPlay && !opponentCanPlay) {
            // Check who likely owns it by checking adjacent squares
            // Simplified: use mobility near corner as proxy
            OthelloBoard temp = board;
            list<Move> myNearby = temp.getValidMoves(perspective);
            list<Move> oppNearby = temp.getValidMoves(opp);
            
            int myNear = 0, oppNear = 0;
            for (const Move& mv : myNearby) {
                if (abs(mv.x - corner.first) <= 1 && abs(mv.y - corner.second) <= 1) {
                    myNear++;
                }
            }
            for (const Move& mv : oppNearby) {
                if (abs(mv.x - corner.first) <= 1 && abs(mv.y - corner.second) <= 1) {
                    oppNear++;
                }
            }
            
            if (myNear < oppNear) score += 100; // Likely my corner
            else if (oppNear < myNear) score -= 100; // Likely opponent's
        }
    }
    
    return score;
}

// Evaluate positional strength using weight matrix
int evaluatePositional(const list<Move>& myMoves, const list<Move>& oppMoves) {
    int score = 0;
    
    // Weight reachable positions (proxy for control)
    for (const Move& m : myMoves) {
        score += POSITION_WEIGHTS[m.x][m.y];
    }
    for (const Move& m : oppMoves) {
        score -= POSITION_WEIGHTS[m.x][m.y];
    }
    
    return score;
}

// Evaluate stability (simplified: edges + corners)
int evaluateStability(const OthelloBoard& board, Turn perspective) {
    int score = 0;
    
    // Corners provide maximum stability
    score += evaluateCorners(board, perspective);
    
    // Count edge control
    list<Move> myMoves = board.getValidMoves(perspective);
    list<Move> oppMoves = board.getValidMoves(opponent(perspective));
    
    int myEdges = 0, oppEdges = 0;
    for (const Move& m : myMoves) {
        if (isEdge(m) && !isXSquare(m) && !isCSquare(m)) myEdges++;
    }
    for (const Move& m : oppMoves) {
        if (isEdge(m) && !isXSquare(m) && !isCSquare(m)) oppEdges++;
    }
    
    score += (myEdges - oppEdges) * 5;
    
    return score;
}

// Main evaluation function
int evaluate(const OthelloBoard& board, Turn perspective) {
    int empty = emptySquares(board);
    Weights w = getWeights(empty);
    
    int sign = (perspective == BLACK) ? 1 : -1;
    
    // Material (coin parity)
    int material = (board.getBlackCount() - board.getRedCount()) * sign;
    
    // Mobility
    list<Move> myMoves = board.getValidMoves(perspective);
    list<Move> oppMoves = board.getValidMoves(opponent(perspective));
    int mobility = myMoves.size() - oppMoves.size();
    
    // Positional
    int positional = evaluatePositional(myMoves, oppMoves);
    
    // Corners
    int corners = evaluateCorners(board, perspective);
    
    // Stability
    int stability = evaluateStability(board, perspective);
    
    // Weighted combination
    int score = (int)(
        w.position * positional + 
        w.mobility * mobility * 5 + 
        w.corner * corners + 
        w.stability * stability + 
        w.parity * material
    );
    
    return score;
}

// ============================================================================
// MOVE ORDERING
// ============================================================================

list<Move> orderMoves(const list<Move>& moves, const OthelloBoard& board, Turn turn) {
    if (moves.size() <= 1) return moves;
    
    vector<pair<int, Move>> scoredMoves;
    int empty = emptySquares(board);
    
    for (const Move& m : moves) {
        int score = 0;
        
        // 1. Corners have absolute highest priority
        if (isCorner(m)) {
            score += 100000;
        }
        // 2. X-squares are VERY bad early game (gift corners to opponent)
        else if (isXSquare(m) && empty > 30) {
            score -= 50000;
        }
        // 3. C-squares also risky early game
        else if (isCSquare(m) && empty > 30) {
            score -= 20000;
        }
        // 4. Good edges (not adjacent to empty corners)
        else if (isEdge(m)) {
            score += 5000;
        }
        // 5. Base position weight
        else {
            score += POSITION_WEIGHTS[m.x][m.y] * 100;
        }
        
        // 6. Minimize opponent mobility after move
        try {
            OthelloBoard temp = board;
            temp.makeMove(turn, m);
            int oppMobility = temp.getValidMoves(opponent(turn)).size();
            score -= oppMobility * 50;
        } catch (...) {
            // If move fails, heavily penalize
            score -= 100000;
        }
        
        scoredMoves.push_back({score, m});
    }
    
    // Sort descending by score (only compare first element of pair)
    sort(scoredMoves.begin(), scoredMoves.end(), 
         [](const pair<int, Move>& a, const pair<int, Move>& b) {
             return a.first > b.first; // Descending order
         });
    
    list<Move> ordered;
    for (const auto& p : scoredMoves) {
        ordered.push_back(p.second);
    }
    
    return ordered;
}

// ============================================================================
// ALPHA-BETA SEARCH
// ============================================================================

int alphaBeta(const OthelloBoard& board, Turn currTurn, int depth, 
              int alpha, int beta, Turn perspective) {
    
    // Timeout check
    if (isTimeUp()) {
        return evaluate(board, perspective);
    }
    
    // Get valid moves for both players
    list<Move> myMoves = board.getValidMoves(currTurn);
    list<Move> oppMoves = board.getValidMoves(opponent(currTurn));
    
    // Terminal conditions
    bool gameOver = myMoves.empty() && oppMoves.empty();
    
    if (depth == 0 || gameOver) {
        return evaluate(board, perspective);
    }
    
    // Pass if no moves available
    if (myMoves.empty()) {
        return alphaBeta(board, opponent(currTurn), depth, alpha, beta, perspective);
    }
    
    // Transposition table lookup
    uint64_t hash = zobrist.hash(board);
    if (transpositionTable.count(hash)) {
        TTEntry& entry = transpositionTable[hash];
        if (entry.hash == hash && entry.depth >= depth) {
            // Move best move to front
            myMoves.remove(entry.bestMove);
            myMoves.push_front(entry.bestMove);
        }
    }
    
    // Order moves for better pruning
    myMoves = orderMoves(myMoves, board, currTurn);
    
    bool maximizing = (currTurn == perspective);
    int bestVal = maximizing ? -INF : INF;
    Move bestMove = myMoves.front();
    
    for (const Move& m : myMoves) {
        if (isTimeUp()) break;
        
        try {
            OthelloBoard newBoard = board;
            newBoard.makeMove(currTurn, m);
            
            int val = alphaBeta(newBoard, opponent(currTurn), depth - 1, 
                               alpha, beta, perspective);
            
            if (maximizing) {
                if (val > bestVal) {
                    bestVal = val;
                    bestMove = m;
                }
                alpha = max(alpha, val);
            } else {
                if (val < bestVal) {
                    bestVal = val;
                    bestMove = m;
                }
                beta = min(beta, val);
            }
            
            // Alpha-beta cutoff
            if (beta <= alpha) {
                break;
            }
        } catch (...) {
            // Skip invalid moves
            continue;
        }
    }
    
    // Store in transposition table
    if (!isTimeUp()) {
        transpositionTable[hash] = TTEntry(hash, depth, bestVal, bestMove);
    }
    
    return bestVal;
}

// ============================================================================
// ROOT SEARCH WITH ITERATIVE DEEPENING
// ============================================================================

Move rootSearch(const OthelloBoard& board, Turn turn, int maxDepth) {
    list<Move> moves = board.getValidMoves(turn);
    
    if (moves.empty()) {
        return Move(0, 0);
    }
    
    if (moves.size() == 1) {
        return moves.front();
    }
    
    // Order moves initially
    moves = orderMoves(moves, board, turn);
    Move bestMove = moves.front();
    
    // Iterative deepening
    for (int depth = 2; depth <= maxDepth; depth += 2) {
        if (isTimeUp()) break;
        
        Move iterBest = bestMove;
        int iterScore = -INF;
        
        for (const Move& m : moves) {
            if (isTimeUp()) break;
            
            try {
                OthelloBoard newBoard = board;
                newBoard.makeMove(turn, m);
                
                int score = alphaBeta(newBoard, opponent(turn), depth - 1, 
                                     -INF, INF, turn);
                
                if (score > iterScore) {
                    iterScore = score;
                    iterBest = m;
                }
            } catch (...) {
                continue;
            }
        }
        
        // Update best move if iteration completed
        if (!isTimeUp()) {
            bestMove = iterBest;
            
            // Move best to front for next iteration
            moves.remove(iterBest);
            moves.push_front(iterBest);
        }
    }
    
    return bestMove;
}

// ============================================================================
// MAIN PLAY FUNCTION (Called by Framework)
// ============================================================================

Move playMove(const OthelloBoard& board, Turn turn) {
    // Initialize search state
    searchStartTime = chrono::high_resolution_clock::now();
    timeoutFlag = false;
    transpositionTable.clear();
    
    // Quick validation
    list<Move> moves = board.getValidMoves(turn);
    if (moves.empty()) {
        return Move(0, 0); // No valid moves (pass/game over)
    }
    
    // Single move - return immediately
    if (moves.size() == 1) {
        return moves.front();
    }
    
    // Calculate adaptive search depth based on game phase
    int empty = emptySquares(board);
    int maxDepth;
    
    if (empty <= 12) {
        // Endgame: search to completion
        maxDepth = empty;
    } else if (empty <= 20) {
        // Late midgame: deep search
        maxDepth = 10;
    } else if (empty <= 40) {
        // Midgame: moderate depth
        maxDepth = 8;
    } else {
        // Opening: shallower but fast
        maxDepth = 6;
    }
    
    maxDepth = min(maxDepth, MAX_DEPTH);
    
    // Run iterative deepening search
    Move bestMove = rootSearch(board, turn, maxDepth);
    
    // Final validation
    if (!board.validateMove(turn, bestMove)) {
        // Fallback to first valid move if something went wrong
        return moves.front();
    }
    
    return bestMove;
}

// ============================================================================
// BOT WRAPPER FOR DESDEMONA FRAMEWORK
// ============================================================================

class MyBot: public OthelloPlayer
{
public:
    MyBot(Turn turn) : OthelloPlayer(turn) {}
    virtual ~MyBot() {}
    
    virtual Move play(const OthelloBoard& board) {
        return playMove(board, turn);
    }
};

// The following lines are _very_ important to create a bot module for Desdemona
extern "C" {
    OthelloPlayer* createBot(Turn turn) {
        return new MyBot(turn);
    }

    void destroyBot(OthelloPlayer* bot) {
        delete bot;
    }
}
