#include "property_guided_mtl_generator.hpp"
#include "mtl_parser.hpp"
#include "mtl_tree.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace rybench::generator {

namespace {
// Resolve each Atom node's varId from the atom-name -> id map (built at loadSchema).
void assignVarIds(MTLNode& n, const std::unordered_map<std::string, int>& ids) {
    if (n.op == MTLNode::Op::Atom) {
        auto it = n.variable.empty() ? ids.end() : ids.find(n.variable);
        n.varId = (it != ids.end()) ? it->second : -1;
        return;
    }
    if (n.left)  assignVarIds(*n.left, ids);
    if (n.right) assignVarIds(*n.right, ids);
    if (n.child) assignVarIds(*n.child, ids);
}

// Short label per node for the coverage report (matches the RYBENCH_DUMP_TREE figure).
std::string opLabel(const MTLNode& n) {
    auto bounds = [&]() {
        std::string hi = (n.timeHigh == kInf) ? "inf" : std::to_string(n.timeHigh);
        return "[" + std::to_string(n.timeLow) + "," + hi + "]";
    };
    switch (n.op) {
    case MTLNode::Op::Atom:       return n.negated ? "!" + n.variable : n.variable;
    case MTLNode::Op::And:        return "and";
    case MTLNode::Op::Or:         return n.exclusive ? "xor" : "or";
    case MTLNode::Op::Eventually: return "F" + bounds();
    case MTLNode::Op::Always:     return "G" + bounds();
    case MTLNode::Op::Until:      return "U" + bounds();
    case MTLNode::Op::Release:    return "R" + bounds();
    default:                      return "?";
    }
}

void collectNodeMeta(const MTLNode& n, std::vector<std::string>& labels) {
    if (static_cast<int>(labels.size()) <= n.nodeId) labels.resize(n.nodeId + 1);
    labels[n.nodeId] = opLabel(n);
    if (n.left)  collectNodeMeta(*n.left, labels);
    if (n.right) collectNodeMeta(*n.right, labels);
    if (n.child) collectNodeMeta(*n.child, labels);
}
}  // namespace

PropertyGuidedMTLGenerator::PropertyGuidedMTLGenerator(serializer::Serializer& backend,
                                                       double timeStep, uint64_t seed)
    : backend_(backend), timeStep_(timeStep),
      rng_(seed == 0 ? std::random_device{}() : seed) {
    // Overridable for sweeps: lookahead depth (steps the probe walks) and the
    // probe's per-step attempt budget.
    if (const char* e = std::getenv("RYBENCH_LOOKAHEAD"))
        lookahead_ = std::max(1, std::atoi(e));
    if (const char* e = std::getenv("RYBENCH_PROBE_ATTEMPTS"))
        probeAttempts_ = std::max(1, std::atoi(e));
    // Seed override: RYBENCH_SEED=0 → fresh random trace each run, =N → reproducible.
    // Takes precedence over the config seed so configs stay deterministic by default.
    if (const char* e = std::getenv("RYBENCH_SEED")) {
        const uint64_t s = std::strtoull(e, nullptr, 10);
        rng_.seed(s == 0 ? std::random_device{}() : s);
    }
    if (std::getenv("RYBENCH_COVERAGE")) coverage_ = true;
    setCoverage(coverage_);
}

PropertyGuidedMTLGenerator::~PropertyGuidedMTLGenerator() {
    if (statSteps_ == 0 && statGiveUps_ == 0) return;
    // A "step" is one candidate step tried (one proceed); an "event" is a committed
    // output event. Two retry causes make us try extra steps per event, both a
    // completeness cost:
    //   conflict-retry — a single step re-proceeded after an a/¬a literal conflict.
    //   deadend-retry  — the whole lookahead walk abandoned (dead-end) and restarted.
    // step/event is the total candidate steps tried to commit one event.
    const uint64_t conflictRetries = statProceedAttempts_ - statTryStepCalls_;
    const uint64_t deadendRetries  = statTrials_ - statSteps_ - statGiveUps_;
    const int firstPct = statSteps_
        ? static_cast<int>(100.0 * statFirstTry_ / statSteps_ + 0.5) : 0;
    const double conflictPerEvent = statSteps_ ? static_cast<double>(conflictRetries) / statSteps_ : 0.0;
    const double deadendPerEvent  = statSteps_ ? static_cast<double>(deadendRetries)  / statSteps_ : 0.0;
    const double stepPerEvent     = statSteps_ ? static_cast<double>(statProceedAttempts_) / statSteps_ : 0.0;
    std::cout << "[property_guided] retry stats: events=" << statSteps_
              << ", first-try=" << statFirstTry_ << " (" << firstPct << "%)"
              << ", conflict-retry=" << conflictRetries << " (" << conflictPerEvent << "/event)"
              << ", deadend-retry=" << deadendRetries << " (" << deadendPerEvent << "/event)"
              << ", step/event=" << stepPerEvent
              << ", max-trials/event=" << statMaxTrials_;
    if (statGiveUps_) std::cout << ", give-ups=" << statGiveUps_;
    std::cout << ", lookahead=" << lookahead_
              << ", probe-attempts=" << probeAttempts_;
    std::cout << "\n";

    if (coverage_ && nodeCount_) {
        std::cout << "[property_guided] node coverage (committed steps):\n";
        for (std::size_t i = 0; i < nodeCount_; ++i) {
            const double rate = totReached_[i]
                ? static_cast<double>(totFired_[i]) / totReached_[i] : 0.0;
            std::cout << "  [" << i << "] " << nodeLabels_[i]
                      << "  reached=" << totReached_[i]
                      << " fired="   << totFired_[i]
                      << " fire-rate=" << rate;
            if (totChoiceLeft_[i] + totChoiceRight_[i] > 0)
                std::cout << " choice=L:" << totChoiceLeft_[i]
                          << " R:" << totChoiceRight_[i];
            std::cout << "\n";
        }
        std::cout << "[property_guided] variable determination (committed steps):\n";
        for (std::size_t i = 0; i < numAtoms_; ++i) {
            const uint64_t det = totForcedTrue_[i] + totForcedFalse_[i];
            const uint64_t tot = det + totFree_[i];
            const double pdet = tot ? 100.0 * static_cast<double>(det) / tot : 0.0;
            std::cout << "  " << boolFields_[i]
                      << "  forced-true=" << totForcedTrue_[i]
                      << " forced-false=" << totForcedFalse_[i]
                      << " free=" << totFree_[i]
                      << " (free-true=" << totFreeTrue_[i]
                      << " free-false=" << totFreeFalse_[i] << ")"
                      << " (" << pdet << "% determined)\n";
        }
    }
}

void PropertyGuidedMTLGenerator::loadSchema(const core::SchemaDefinition& schema) {
    std::string mtl = schema.attribute("mtl");
    if (mtl.empty())
        throw std::runtime_error("PropertyGuidedMTLGenerator: no 'mtl' attribute on root table");

    // Parse -> NNF (eliminate implies, push negations) -> fold not(atom) into literals.
    formula_ = foldLiterals(preprocessFormula(parseMTLNode(mtl)));

    // Number every node of the converted tree so coverage stats and the figure align.
    assignNodeIds(formula_);
    if (std::getenv("RYBENCH_DUMP_TREE"))
        std::cout << "[property_guided] converted formula tree:\n"
                  << toAsciiTree(formula_);

    nodeLabels_.clear();
    collectNodeMeta(formula_, nodeLabels_);
    nodeCount_ = nodeLabels_.size();
    if (coverage_) {
        setNodeCount(nodeCount_);
        totReached_.assign(nodeCount_, 0);
        totFired_.assign(nodeCount_, 0);
        totChoiceLeft_.assign(nodeCount_, 0);
        totChoiceRight_.assign(nodeCount_, 0);
    }

    backend_.loadSchema(schema);
    fields_ = &schema.fields();

    boolFields_.clear();
    for (const auto& f : schema.fields())
        if (f.type == core::FieldType::Bool) boolFields_.push_back(f.name);

    formulaVars_.clear();
    collectAtoms(formula_, formulaVars_);

    // Assign a small integer id to each bool atom so the literal set L is an
    // index-addressed vector<int8_t> rather than a string-keyed map (no hashing).
    std::unordered_map<std::string, int> atomIds;
    for (std::size_t i = 0; i < boolFields_.size(); ++i)
        atomIds[boolFields_[i]] = static_cast<int>(i);
    numAtoms_ = boolFields_.size();
    assignVarIds(formula_, atomIds);
    setLiteralCount(numAtoms_);
    if (coverage_) {                       // per-atom vectors (need numAtoms_ + boolFields_)
        totForcedTrue_.assign(numAtoms_, 0);
        totForcedFalse_.assign(numAtoms_, 0);
        totFree_.assign(numAtoms_, 0);
        totFreeTrue_.assign(numAtoms_, 0);
        totFreeFalse_.assign(numAtoms_, 0);
    }

    fieldAtomId_.assign(fields_->size(), -1);
    for (std::size_t i = 0; i < fields_->size(); ++i) {
        const auto& f = (*fields_)[i];
        if (f.type == core::FieldType::Bool) {
            auto it = atomIds.find(f.name);
            if (it != atomIds.end()) fieldAtomId_[i] = it->second;
        }
    }

    setProceedRng(&rng_);
    currentTime_ = 0.0;
    // O_curr starts with one obligation: satisfy the whole formula, within the
    // root operator's own window.
    auto& curr = stepCurr();
    curr.clear();
    curr.push_back({&formula_, formula_.timeLow, formula_.timeHigh});

    schemaLoaded_ = true;
}

// Merge: collapse obligations on the same node, keeping the earliest deadline for
// the existential operators (F, U) and the latest for the universal ones (G, R).
void PropertyGuidedMTLGenerator::merge(std::vector<Obligation>& obs) {
    std::vector<Obligation>& out = mergeScratch_;
    out.clear();                                  // keep capacity; no per-call malloc/free
    for (const Obligation& o : obs) {
        Obligation* e = nullptr;
        for (Obligation& c : out) if (c.node == o.node) { e = &c; break; }
        if (!e) { out.push_back(o); continue; }
        const bool existential = (o.node->op == MTLNode::Op::Eventually ||
                                  o.node->op == MTLNode::Op::Until);
        e->hi = existential ? std::min(e->hi, o.hi) : std::max(e->hi, o.hi);
        e->lo = std::min(e->lo, o.lo);
    }
    obs.swap(out);
}

core::Message PropertyGuidedMTLGenerator::generate() {
    if (!schemaLoaded_)
        throw std::runtime_error("PropertyGuidedMTLGenerator: loadSchema() not called");
    rerollSnapshot_.assign(stepCurr().begin(), stepCurr().end());  // reused buffer, no per-event alloc
    const std::vector<Obligation>& curr = rerollSnapshot_;         // this step's obligations
    // No remaining obligations: a bounded formula is fully satisfied and the trace
    // is finite. Signal completion so the engine stops instead of skip-looping.
    if (curr.empty())
        throw std::runtime_error(
            "[property_guided] trace complete: all obligations satisfied at t=" +
            std::to_string(static_cast<long>(currentTime_)));

    const int     kLookahead = lookahead_;  // steps the probe walks (bound+1=11 default; env-overridable)
    constexpr int kReroll    = 100000;      // step candidates to try before giving up

    // Pick a step that is itself consistent AND admits a conflict-free continuation
    // for the next kLookahead steps — so we never commit to a dead end.
    bool found = false;
    int  rerolls = 0;
    for (; rerolls < kReroll; ++rerolls) {
        // The probe both PICKS this step and validates its continuation: it walks
        // kLookahead steps and, on success, leaves the first (emitted) step in the
        // first*_ buffers.
        if (probe(curr, kLookahead)) { found = true; break; }
        ++statProbeFails_;                                 // the lookahead walk dead-ended
    }

    if (!found) {
        ++statGiveUps_; statTrials_ += rerolls + 1;
        if (rerolls + 1 > statMaxTrials_) statMaxTrials_ = rerolls + 1;
        throw std::runtime_error(
            "[property_guided] CONFLICT at t=" +
            std::to_string(static_cast<long>(currentTime_)) +
            ": no conflict-free continuation within lookahead — ending trace");
    }

    ++statSteps_; statTrials_ += rerolls + 1;
    if (rerolls == 0) ++statFirstTry_; else ++statRetriedSteps_;
    if (rerolls + 1 > statMaxTrials_) statMaxTrials_ = rerolls + 1;

    // Commit the chosen step: the probe left the emitted step in first*_; move it
    // into the step globals (swap = buffer reuse, no alloc) for encode() to read.
    stepLiterals().swap(firstLiterals_);
    auto& L = stepLiterals();
    if (coverage_) {                                    // determination BEFORE the random fill
        for (std::size_t i = 0; i < L.size(); ++i) {
            if (L[i] < 0)       ++totFree_[i];
            else if (L[i] == 1) ++totForcedTrue_[i];
            else                ++totForcedFalse_[i];
        }
    }
    for (std::size_t i = 0; i < L.size(); ++i)          // free variables -> random
        if (L[i] < 0) {
            const int8_t v = static_cast<int8_t>(std::uniform_int_distribution<int>(0, 1)(rng_));
            L[i] = v;
            if (coverage_) { if (v) ++totFreeTrue_[i]; else ++totFreeFalse_[i]; }
        }

    core::Message msg = encode();
    stepCurr().swap(firstNext_);          // O_curr <- O_next (the validated continuation)
    if (coverage_) {
        for (std::size_t i = 0; i < nodeCount_ && i < firstReached_.size(); ++i) {
            totReached_[i]     += firstReached_[i];
            totFired_[i]       += firstFired_[i];
            totChoiceLeft_[i]  += firstChLeft_[i];
            totChoiceRight_[i] += firstChRight_[i];
        }
    }
    currentTime_ += timeStep_;
    return msg;
}

// One step from `curr`: proceed (with intra-step retries on an a/¬a contradiction)
// and, on success, return the chosen literals and the merged next obligations.
bool PropertyGuidedMTLGenerator::tryStep(const std::vector<Obligation>& curr, int attempts) {
    ++statTryStepCalls_;
    stepCurr().assign(curr.begin(), curr.end());  // reuse g_curr buffer (no move/steal)
    const std::vector<Obligation>& gc = stepCurr();
    for (int a = 0; a < attempts; ++a) {
        ++statProceedAttempts_;
        resetAttempt();
        for (const Obligation& o : gc) o.node->proceed(o.lo, o.hi);
        if (!stepConflict()) {
            merge(stepNext());
            return true;   // results left in stepLiterals()/stepNext() (+ coverage globals)
        }
    }
    return false;
}

// Walk up to `depth` steps from `start`, picking each one greedily. Returns the
// FIRST step (the one to emit) iff the whole walk stays conflict-free; nullopt if
// it dead-ends within the horizon. So one routine both chooses the next step and
// proves it has a continuation — there is no separate "commit" step. Each step uses
// a small attempt budget (a dead-end is confirmed cheaply; a falsely-rejected step
// just causes the caller to reroll).
bool PropertyGuidedMTLGenerator::probe(const std::vector<Obligation>& start, int depth) {
    bool haveFirst = false;
    workCurr_.assign(start.begin(), start.end());         // reused walk cursor
    for (int i = 0; i < depth; ++i) {
        if (workCurr_.empty()) break;                     // trace ends naturally -> feasible
        if (!tryStep(workCurr_, probeAttempts_)) return false;  // dead end within the horizon
        if (!haveFirst) {                                 // save the first (emitted) step
            firstLiterals_.assign(stepLiterals().begin(), stepLiterals().end());
            firstNext_.assign(stepNext().begin(), stepNext().end());
            if (coverage_) {
                firstReached_.assign(stepReached().begin(), stepReached().end());
                firstFired_.assign(stepFired().begin(), stepFired().end());
                firstChLeft_.assign(stepChoiceLeft().begin(), stepChoiceLeft().end());
                firstChRight_.assign(stepChoiceRight().begin(), stepChoiceRight().end());
            }
            haveFirst = true;
        }
        if (i + 1 < depth)                                // feed the next step (skip on last)
            workCurr_.assign(stepNext().begin(), stepNext().end());
    }
    return haveFirst;                                     // false if start had no obligations
}

core::Message PropertyGuidedMTLGenerator::encode() {
    if (!fields_) return core::Message();
    const uint64_t ts = static_cast<uint64_t>(currentTime_);
    const auto& L = stepLiterals();
    backend_.initMessage();
    for (std::size_t i = 0; i < fields_->size(); ++i) {
        const auto& sf = (*fields_)[i];
        switch (sf.type) {
        case core::FieldType::Bool: {
            const int id = fieldAtomId_[i];
            backend_.addBool(sf, id >= 0 && L[id] == 1);
            break;
        }
        case core::FieldType::UByte:
        case core::FieldType::UShort:
        case core::FieldType::UInt:
        case core::FieldType::ULong:
            backend_.addUInt(sf, ts);
            break;
        case core::FieldType::Byte:
        case core::FieldType::Short:
        case core::FieldType::Int:
        case core::FieldType::Long:
            backend_.addInt(sf, static_cast<int64_t>(ts));
            break;
        default:
            break;  // MTL schemas only use an integer timestamp + bool atoms
        }
    }
    return backend_.finalizeMessage();
}

std::string PropertyGuidedMTLGenerator::name() const {
    return "property_guided_mtl";
}

} // namespace rybench::generator
