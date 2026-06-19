#include "mtl_node.hpp"
#include <stdexcept>

namespace rybench::generator {

namespace {

// Generation state (file-global; one generator runs at a time).
std::mt19937_64*                      g_rng = nullptr;
std::vector<Obligation>               g_curr;      // O_curr: obligations processed this step
std::vector<Obligation>               g_next;      // O_next: obligations carried to the next
std::unordered_map<std::string, bool> g_literals;  // L: variable -> chosen value
std::vector<MTLNode*>                 g_fired;     // nodes already processed this step
bool                                  g_conflict = false;

// Advance a window bound by one step (inf stays inf, floored at 0).
uint64_t dec(uint64_t x) { return (x == kInf) ? kInf : (x > 0 ? x - 1 : 0); }

// Fire probability for an existential obligation with `hi` steps left to its
// deadline: 1/(hi+1) — uniform over the remaining window, certain (1) at the
// deadline. An unbounded window (hi = inf) has no deadline → 1/2.
double fireProb(uint64_t hi) { return (hi == kInf) ? 0.5 : 1.0 / static_cast<double>(hi + 1); }

double unif() { return std::uniform_real_distribution<double>(0.0, 1.0)(*g_rng); }

// The remaining window for `n`: the carried (tighter) one from O_curr if it is
// pending this step, otherwise the node's own bounds (a fresh instance).
Obligation windowOf(MTLNode* n) {
    for (const Obligation& o : g_curr) if (o.node == n) return o;
    return {n, n->timeLow, n->timeHigh};
}

void addLiteral(const std::string& var, bool value) {
    auto it = g_literals.find(var);
    if (it == g_literals.end()) g_literals.emplace(var, value);  // first write wins
    else if (it->second != value) g_conflict = true;             // a and ¬a
}

bool firstFire(MTLNode* n) {
    for (MTLNode* m : g_fired) if (m == n) return false;
    g_fired.push_back(n);
    return true;
}

}  // namespace

void setProceedRng(std::mt19937_64* rng) { g_rng = rng; }

void resetAttempt() {                 // clears the per-attempt state, not O_curr
    g_literals.clear();
    g_fired.clear();
    g_next.clear();
    g_conflict = false;
}

bool stepConflict() { return g_conflict; }
std::vector<Obligation>&               stepCurr()     { return g_curr; }
std::vector<Obligation>&               stepNext()     { return g_next; }
std::unordered_map<std::string, bool>& stepLiterals() { return g_literals; }

// Proceed: boolean nodes only steer this step's traversal; temporal nodes call
// their child(ren) and/or carry an obligation into the next step. `lo,hi` is this
// reach's window — used by universal nodes (Always) so a re-trigger extends the
// window; existential nodes (Eventually/Until) ignore it and use the carried
// (tightest) window from windowOf, processing once.
void MTLNode::proceed(uint64_t lo, uint64_t hi) {
    switch (op) {
    case Op::Atom:
        addLiteral(variable, !negated);                  // literal a → true, ¬a → false
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
        if (l) left->proceed(left->timeLow, left->timeHigh);
        if (r) right->proceed(right->timeLow, right->timeHigh);
        break;
    }

    case Op::Eventually: {                               // F (existential): once, tightest deadline
        if (!firstFire(this)) break;
        Obligation o = windowOf(this);
        if (o.lo == 0 && unif() < fireProb(o.hi)) child->proceed(child->timeLow, child->timeHigh);
        else g_next.push_back({this, dec(o.lo), dec(o.hi)});
        break;
    }

    case Op::Always: {                                  // G (universal): extend on every reach
        // Carry on EVERY reach with this reach's window — the carried obligation and
        // a fresh re-trigger both push, and merge's max for G combines them, so the
        // ¬p/p window EXTENDS when q recurs. No coin here, so asserting once
        // (firstFire) is the only thing that must be deduplicated.
        if (hi > 0) g_next.push_back({this, dec(lo), dec(hi)});
        if (firstFire(this) && lo == 0) child->proceed(child->timeLow, child->timeHigh);
        break;
    }

    case Op::Until: {                                   // U (existential): once, tightest deadline
        if (!firstFire(this)) break;
        Obligation o = windowOf(this);
        if (o.lo == 0 && unif() < fireProb(o.hi)) {
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
        if (unif() < 0.5) left->proceed(left->timeLow, left->timeHigh);  // phi releases -> discharge
        else if (o.hi > 0) g_next.push_back({this, dec(o.lo), dec(o.hi)});
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
