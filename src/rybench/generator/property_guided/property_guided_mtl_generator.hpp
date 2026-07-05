#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/serializer/serializer.hpp>
#include "mtl_node.hpp"
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rybench::generator {

// Property-Guided generator: produces a discrete-time event trace that satisfies a
// future-fragment MTL formula. Each step runs MTLNode::proceed over the current
// obligations (retrying on a literal contradiction), then Merge collapses the
// obligations carried to the next step.
class PropertyGuidedMTLGenerator : public Generator {
public:
    explicit PropertyGuidedMTLGenerator(serializer::Serializer& backend,
                                        double timeStep = 1.0, uint64_t seed = 0);
    ~PropertyGuidedMTLGenerator() override;

    void loadSchema(const core::SchemaDefinition& schema) override;
    core::Message generate() override;
    std::string name() const override;

private:
    serializer::Serializer& backend_;
    const std::vector<core::SchemaField>* fields_ = nullptr;
    std::vector<int>                fieldAtomId_;  // fields_[i] -> atom id, or -1 if not a bool atom
    std::size_t                     numAtoms_ = 0; // number of bool atoms (size of the literal set L)

    MTLNode formula_;                  // preprocessed (NNF) formula tree
    double  timeStep_    = 1.0;
    double  currentTime_ = 0.0;
    bool    schemaLoaded_ = false;

    std::vector<std::string>        boolFields_;   // boolean schema fields (ordered)
    std::unordered_set<std::string> formulaVars_;  // atoms referenced by the formula
    std::mt19937_64                 rng_;
    int                             lookahead_     = 11; // probe depth (env-overridable)
    int                             probeAttempts_ = 8;  // probe per-step budget (env-overridable)

    // Coverage instrumentation (opt-in via RYBENCH_COVERAGE): per-node reached/fired
    // counts accumulated over committed steps, dumped at destruction.
    bool                     coverage_  = false;
    std::size_t              nodeCount_ = 0;
    std::vector<uint64_t>    totReached_, totFired_;  // per-node totals over the trace
    std::vector<uint64_t>    totChoiceLeft_, totChoiceRight_;  // per-Or branch choices
    std::vector<std::string> nodeLabels_;             // node id -> "op[bounds]" for the report
    // Per-atom determination: how each variable's value was fixed each committed step.
    std::vector<uint64_t>    totForcedTrue_, totForcedFalse_, totFree_;
    std::vector<uint64_t>    totFreeTrue_, totFreeFalse_;  // random outcome of free vars (~50/50)

    // Generate one consistent step from `curr` with up to `attempts` intra-step
    // retries on an a/¬a contradiction. On success the chosen literals and carried
    // obligations are left in the step globals (stepLiterals()/stepNext(), plus the
    // coverage globals); false if none was conflict-free.
    bool tryStep(const std::vector<Obligation>& curr, int attempts);
    // Walk `depth` steps from `start`; true iff the whole walk stays conflict-free
    // (picks AND validates). The first (emitted) step is saved in the first*_ buffers.
    bool probe(const std::vector<Obligation>& start, int depth);

    void          merge(std::vector<Obligation>& obs);  // collapse same-node obligations
    core::Message encode();                             // build a message from the literals

    std::vector<Obligation> mergeScratch_;    // reused across merge() calls (no per-call heap churn)
    std::vector<Obligation> rerollSnapshot_;  // reused reroll snapshot of O_curr (no per-event copy alloc)
    std::vector<Obligation> workCurr_;        // probe's reused walk cursor (no per-step copy alloc)
    // Saved first (emitted) step of a probe walk — reused buffers, no per-event alloc.
    std::vector<int8_t>     firstLiterals_;
    std::vector<Obligation> firstNext_;
    std::vector<uint32_t>   firstReached_, firstFired_, firstChLeft_, firstChRight_;  // coverage (empty if off)

    // Step-retry statistics, summarized at destruction.
    uint64_t statSteps_ = 0, statTrials_ = 0, statFirstTry_ = 0,
             statRetriedSteps_ = 0, statGiveUps_ = 0;
    uint64_t statTryStepCalls_ = 0;   // tryStep() invocations (step-0 + probe steps)
    uint64_t statProceedAttempts_ = 0; // inner proceed walks (the real work unit)
    uint64_t statTryStepFails_ = 0;   // rerolls where step-0 itself was unsatisfiable
    uint64_t statProbeFails_ = 0;     // rerolls where step-0 was OK but its lookahead dead-ended
    int      statMaxTrials_ = 0;
};

} // namespace rybench::generator
