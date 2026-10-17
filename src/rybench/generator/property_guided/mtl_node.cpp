#include "mtl_node.hpp"
#include <array>
#include <stdexcept>

namespace rybench::generator {

namespace {

// Generation state (file-global; one generator runs at a time).
std::mt19937_64*                      g_rng = nullptr;
std::vector<Obligation>               g_curr;      // O_curr: obligations processed this step
std::vector<Obligation>               g_next;      // O_next: obligations carried to the next
std::vector<int8_t>                   g_literals;  // L: atom id -> -1 unset / 0 / 1
std::size_t                           g_numAtoms = 0;
std::vector<MTLNode*>                 g_fired;     // nodes already processed this step
bool                                  g_conflict = false;

// Coverage instrumentation (opt-in, gated so the hot path is untouched when off).
bool                                  g_coverage = false;
std::size_t                           g_nodeCount = 0;
std::vector<uint32_t>                 g_reached;   // per-node reached count (this attempt)
std::vector<uint32_t>                 g_firedCnt;  // per-node fired  count (this attempt)
std::vector<uint32_t>                 g_chLeft;    // per-Or left-branch taken (this attempt)
std::vector<uint32_t>                 g_chRight;   // per-Or right-branch taken (this attempt)

// Advance a window bound by one step (inf stays inf, floored at 0).
uint64_t dec(uint64_t x) { return (x == kInf) ? kInf : (x > 0 ? x - 1 : 0); }

// Fire probability for an existential obligation with `hi` steps left to its
// deadline: 1/(hi+1) — uniform over the remaining window, certain (1) at the
// deadline. An unbounded window (hi = inf) has no deadline → 1/2.
//
// hi is bounded by the formula's window (small: <= W), so the 1/(hi+1) divide is
// precomputed once into a table and read back with an indexed load on the hot
// path. Same double value as the divide -> bit-identical traces.
const std::array<double, 256> g_fireProb = [] {
    std::array<double, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = 1.0 / static_cast<double>(i + 1);
    return t;
}();

double fireProb(uint64_t hi) {
    if (hi == kInf) return 0.5;
    return hi < g_fireProb.size() ? g_fireProb[hi] : 1.0 / static_cast<double>(hi + 1);
}

double unif() { return std::uniform_real_distribution<double>(0.0, 1.0)(*g_rng); }

// The remaining window for `n`: the carried (tighter) one from O_curr if it is
// pending this step, otherwise the node's own bounds (a fresh instance).
Obligation windowOf(MTLNode* n) {
    for (const Obligation& o : g_curr) if (o.node == n) return o;
    return {n, n->timeLow, n->timeHigh};
}

void addLiteral(int id, bool value) {
    const int8_t v = value ? 1 : 0;
    if (g_literals[id] < 0) g_literals[id] = v;          // first write wins
    else if (g_literals[id] != v) g_conflict = true;     // a and ¬a
}

bool firstFire(MTLNode* n) {
    for (MTLNode* m : g_fired) if (m == n) return false;
    g_fired.push_back(n);
    return true;
}

}  // namespace

void setProceedRng(std::mt19937_64* rng) { g_rng = rng; }
void setLiteralCount(std::size_t numAtoms) {
    g_numAtoms = numAtoms;
    g_literals.assign(numAtoms, static_cast<int8_t>(-1));
}
void setCoverage(bool on) { g_coverage = on; }
void setNodeCount(std::size_t numNodes) {
    g_nodeCount = numNodes;
    g_reached.assign(numNodes, 0u);
    g_firedCnt.assign(numNodes, 0u);
    g_chLeft.assign(numNodes, 0u);
    g_chRight.assign(numNodes, 0u);
}

void resetAttempt() {                 // clears the per-attempt state, not O_curr
    g_literals.assign(g_numAtoms, static_cast<int8_t>(-1));
    g_fired.clear();
    g_next.clear();
    g_conflict = false;
    if (g_coverage) {
        g_reached.assign(g_nodeCount, 0u);
        g_firedCnt.assign(g_nodeCount, 0u);
        g_chLeft.assign(g_nodeCount, 0u);
        g_chRight.assign(g_nodeCount, 0u);
    }
}

bool stepConflict() { return g_conflict; }
std::vector<Obligation>& stepCurr()     { return g_curr; }
std::vector<Obligation>& stepNext()     { return g_next; }
std::vector<int8_t>&     stepLiterals() { return g_literals; }
std::vector<uint32_t>&   stepReached()  { return g_reached; }
std::vector<uint32_t>&   stepFired()    { return g_firedCnt; }
std::vector<uint32_t>&   stepChoiceLeft()  { return g_chLeft; }
std::vector<uint32_t>&   stepChoiceRight() { return g_chRight; }

// Proceed: boolean nodes only steer this step's traversal; temporal nodes call
// their child(ren) and/or carry an obligation into the next step. `lo,hi` is this
// reach's window — used by universal nodes (Always) so a re-trigger extends the
// window; existential nodes (Eventually/Until) ignore it and use the carried
// (tightest) window from windowOf, processing once.
void MTLNode::proceed(uint64_t lo, uint64_t hi) {
    // Count reach once per step (active-step count), not per proceed-call: a node
    // can be proceed'd twice in a step (carried obligation + fresh trigger).
    if (g_coverage && !g_reached[nodeId]) g_reached[nodeId] = 1;
    switch (op) {
    case Op::Atom:
        addLiteral(varId, !negated);                     // literal a → true, ¬a → false
        break;

    case Op::And:
        left->proceed(left->timeLow, left->timeHigh);
        right->proceed(right->timeLow, right->timeHigh);
        break;

    case Op::Or: {
        bool l, r;
        if (exclusive) {                                 // implies-rewrite: exactly one
            l = unif() < 0.5; r = !l;
        } else {                                         // inclusive: at least one
            l = unif() < 0.5; r = unif() < 0.5;
            if (!l && !r) { if (unif() < 0.5) l = true; else r = true; }
        }
        if (g_coverage) { if (l) ++g_chLeft[nodeId]; if (r) ++g_chRight[nodeId]; }
        if (l) left->proceed(left->timeLow, left->timeHigh);
        if (r) right->proceed(right->timeLow, right->timeHigh);
        break;
    }

    case Op::Eventually: {                               // F (existential): once, tightest deadline
        if (!firstFire(this)) break;
        Obligation o = windowOf(this);
        if (o.lo == 0 && unif() < fireProb(o.hi)) {
            if (g_coverage) ++g_firedCnt[nodeId];
            child->proceed(child->timeLow, child->timeHigh);
        } else {
            g_next.push_back({this, dec(o.lo), dec(o.hi)});
        }
        break;
    }

    case Op::Always: {                                  // G (universal): extend on every reach
        // Carry on EVERY reach with this reach's window — the carried obligation and
        // a fresh re-trigger both push, and merge's max for G combines them, so the
        // ¬p/p window EXTENDS when q recurs. No coin here, so asserting once
        // (firstFire) is the only thing that must be deduplicated.
        if (hi > 0) g_next.push_back({this, dec(lo), dec(hi)});
        // Consume the per-step fire token ONLY on a maturing reach (lo==0). A fresh
        // re-trigger with lo>0 (e.g. the inner G[1,1] of a nested always) must not
        // burn the token, or the matured carried obligation that actually asserts the
        // child gets suppressed and the constraint silently vanishes.
        if (lo == 0 && firstFire(this)) {
            if (g_coverage) ++g_firedCnt[nodeId];
            child->proceed(child->timeLow, child->timeHigh);
        }
        break;
    }

    case Op::Until: {                                   // U (existential): once, tightest deadline
        if (!firstFire(this)) break;
        Obligation o = windowOf(this);
        if (o.lo == 0 && unif() < fireProb(o.hi)) {
            if (g_coverage) ++g_firedCnt[nodeId];
            right->proceed(right->timeLow, right->timeHigh);   // psi → discharge
        } else {
            left->proceed(left->timeLow, left->timeHigh);      // phi holds
            g_next.push_back({this, dec(o.lo), dec(o.hi)});    // carry until it fires
        }
        break;
    }

    case Op::Release: {                                 // R (universal): unchanged for now (not in benchmark)
        if (!firstFire(this)) break;
        Obligation o = windowOf(this);
        right->proceed(right->timeLow, right->timeHigh);       // psi holds this step
        if (unif() < 0.5) {
            if (g_coverage) ++g_firedCnt[nodeId];
            left->proceed(left->timeLow, left->timeHigh);      // phi releases -> discharge
        } else if (o.hi > 0) {
            g_next.push_back({this, dec(o.lo), dec(o.hi)});
        }
        break;
    }

    case Op::Implies:
    case Op::Not:
        // These are eliminated by NNF preprocessing and must never reach
        // generation; if one does, the formula was not normalized — fail loudly
        // rather than silently produce a wrong trace.
        throw std::logic_error(
            "MTLNode::proceed: Implies/Not in generation tree — formula not in NNF");
    }
}

} // namespace rybench::generator
