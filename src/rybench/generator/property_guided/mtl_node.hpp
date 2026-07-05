#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace rybench::generator {

// Unbounded time-bound sentinel — the 'inf' in eventually[0,inf], bare until, etc.
inline constexpr uint64_t kInf = std::numeric_limits<uint64_t>::max();

// A node of the MTL formula tree. Pure structure plus its generation behavior
// (proceed). No mutable runtime state is stored on the node — an obligation's
// remaining window lives in the per-step generation state below.
struct MTLNode {
    enum class Op { Atom, And, Or, Implies, Not, Eventually, Always, Until, Release };

    Op          op        = Op::Atom;
    int         nodeId    = -1;           // stable DFS id over the converted tree (set at loadSchema)
    std::string variable;                 // Atom only
    int         varId     = -1;           // Atom only: resolved atom index (set at loadSchema)
    bool        negated   = false;        // Atom only: literal sign (true → ¬variable)
    bool        exclusive = false;        // Or only: pick exactly one branch (implies-rewrite)
    uint64_t    timeLow   = 0;            // temporal lower bound l
    uint64_t    timeHigh  = kInf;         // temporal upper bound u (kInf = inf)

    std::unique_ptr<MTLNode> left;        // binary ops: And, Or, Implies, Until, Release
    std::unique_ptr<MTLNode> right;
    std::unique_ptr<MTLNode> child;       // unary/temporal: Not, Eventually, Always

    // Proceed: depth-first traversal of this subtree, with the obligation's window
    // [lo, hi] for this reach. Emits this step's literals and queues the next step's
    // obligations. Universal nodes (Always) carry on every reach so a re-trigger
    // extends their window (via merge's max); existential nodes use windowOf (the
    // tightest carried deadline) and process once. NNF only (no Not/Implies).
    void proceed(uint64_t lo, uint64_t hi);
};

// An obligation (phi_i, [lo, hi]): subformula `node` to satisfy within its
// remaining time window [lo, hi].
struct Obligation { MTLNode* node; uint64_t lo; uint64_t hi; };

// ── Generation state ─────────────────────────────────────────────────────────
// File-global state that MTLNode::proceed reads and writes (one generator runs at
// a time). The two obligation lists are O_curr (processed this step) and O_next
// (carried to the following step). The generator drives a step through these:
void setProceedRng(std::mt19937_64* rng);              // seedable RNG, set once
void setLiteralCount(std::size_t numAtoms);            // size L to the number of atoms
void setCoverage(bool on);                             // enable per-node reach/fire counting
void setNodeCount(std::size_t numNodes);               // size the coverage vectors
std::vector<uint32_t>& stepReached();                  // per-node reached count (this attempt)
std::vector<uint32_t>& stepFired();                    // per-node fired count (this attempt)
std::vector<uint32_t>& stepChoiceLeft();               // per-Or left-branch taken (this attempt)
std::vector<uint32_t>& stepChoiceRight();              // per-Or right-branch taken (this attempt)
void resetAttempt();                                   // clear literals/fired/next/conflict
bool stepConflict();                                   // a literal `a` and `¬a` both chosen
std::vector<Obligation>& stepCurr();                   // O_curr (persists across steps)
std::vector<Obligation>& stepNext();                   // O_next collected this step
std::vector<int8_t>&     stepLiterals();               // L: atom id -> -1 unset / 0 / 1

} // namespace rybench::generator
