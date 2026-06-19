#include "property_guided_mtl_generator.hpp"
#include "mtl_parser.hpp"
#include "mtl_tree.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace rybench::generator {

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
}

PropertyGuidedMTLGenerator::~PropertyGuidedMTLGenerator() {
    if (statSteps_ == 0 && statGiveUps_ == 0) return;
    const uint64_t retries = statTrials_ - statSteps_ - statGiveUps_;
    const int firstPct = statSteps_
        ? static_cast<int>(100.0 * statFirstTry_ / statSteps_ + 0.5) : 0;
    std::cout << "[property_guided] retry stats: steps=" << statSteps_
              << ", trials=" << statTrials_ << ", first-try=" << statFirstTry_
              << " (" << firstPct << "%), retried-steps=" << statRetriedSteps_
              << ", retries=" << retries << ", max-trials/step=" << statMaxTrials_;
    if (statGiveUps_) std::cout << ", give-ups=" << statGiveUps_;
    std::cout << ", tryStep-calls=" << statTryStepCalls_
              << ", proceed-attempts=" << statProceedAttempts_;
    if (statSteps_)
        std::cout << " (" << (statProceedAttempts_ / statSteps_) << "/step)";
    std::cout << ", reroll-cause[tryStep-fail=" << statTryStepFails_
              << ", probe-fail=" << statProbeFails_ << "]";
    std::cout << ", lookahead=" << lookahead_
              << ", probe-attempts=" << probeAttempts_;
    std::cout << "\n";
}

void PropertyGuidedMTLGenerator::loadSchema(const core::SchemaDefinition& schema) {
    std::string mtl = schema.attribute("mtl");
    if (mtl.empty())
        throw std::runtime_error("PropertyGuidedMTLGenerator: no 'mtl' attribute on root table");

    // Parse -> NNF (eliminate implies, push negations) -> fold not(atom) into literals.
    formula_ = foldLiterals(preprocessFormula(parseMTLNode(mtl)));

    backend_.loadSchema(schema);
    fields_ = &schema.fields();

    boolFields_.clear();
    for (const auto& f : schema.fields())
        if (f.type == core::FieldType::Bool) boolFields_.push_back(f.name);

    formulaVars_.clear();
    collectAtoms(formula_, formulaVars_);

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
    std::vector<Obligation> out;
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
    const std::vector<Obligation> curr = stepCurr();   // this step's obligations
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
    StepResult chosen;
    bool found = false;
    int  rerolls = 0;
    for (; rerolls < kReroll; ++rerolls) {
        // The probe both PICKS this step and validates its continuation: it walks
        // kLookahead steps and returns the first one iff the whole walk survives.
        std::optional<StepResult> step = probe(curr, kLookahead);
        if (step) {
            chosen = std::move(*step);
            found  = true;
            break;
        }
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

    // Commit the chosen step (the lookahead probe overwrote the global state).
    stepLiterals() = chosen.literals;
    auto& L = stepLiterals();
    for (const std::string& f : boolFields_)            // free variables -> random
        if (!L.count(f))
            L[f] = std::uniform_int_distribution<int>(0, 1)(rng_) == 1;

    core::Message msg = encode();
    stepCurr() = chosen.next;          // O_curr <- O_next (the validated continuation)
    currentTime_ += timeStep_;
    return msg;
}

// One step from `curr`: proceed (with intra-step retries on an a/¬a contradiction)
// and, on success, return the chosen literals and the merged next obligations.
std::optional<PropertyGuidedMTLGenerator::StepResult>
PropertyGuidedMTLGenerator::tryStep(const std::vector<Obligation>& curr, int attempts) {
    ++statTryStepCalls_;
    stepCurr() = curr;
    for (int a = 0; a < attempts; ++a) {
        ++statProceedAttempts_;
        resetAttempt();
        for (const Obligation& o : curr) o.node->proceed(o.lo, o.hi);
        if (!stepConflict()) {
            merge(stepNext());
            return StepResult{stepLiterals(), stepNext()};
        }
    }
    return std::nullopt;
}

// Walk up to `depth` steps from `start`, picking each one greedily. Returns the
// FIRST step (the one to emit) iff the whole walk stays conflict-free; nullopt if
// it dead-ends within the horizon. So one routine both chooses the next step and
// proves it has a continuation — there is no separate "commit" step. Each step uses
// a small attempt budget (a dead-end is confirmed cheaply; a falsely-rejected step
// just causes the caller to reroll).
std::optional<PropertyGuidedMTLGenerator::StepResult>
PropertyGuidedMTLGenerator::probe(const std::vector<Obligation>& start, int depth) {
    StepResult first;
    bool haveFirst = false;
    std::vector<Obligation> curr = start;
    for (int i = 0; i < depth; ++i) {
        if (curr.empty()) break;                          // trace ends naturally -> feasible
        std::optional<StepResult> step = tryStep(curr, probeAttempts_);
        if (!step) return std::nullopt;                   // dead end within the horizon
        if (!haveFirst) { first = *step; haveFirst = true; }
        curr = std::move(step->next);
    }
    if (!haveFirst) return std::nullopt;                  // start had no obligations
    return first;
}

core::Message PropertyGuidedMTLGenerator::encode() {
    if (!fields_) return core::Message();
    const uint64_t ts = static_cast<uint64_t>(currentTime_);
    const auto& L = stepLiterals();
    backend_.initMessage();
    for (const auto& sf : *fields_) {
        switch (sf.type) {
        case core::FieldType::Bool: {
            auto it = L.find(sf.name);
            backend_.addBool(sf, it != L.end() && it->second);
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
