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

    MTLNode formula_;                  // preprocessed (NNF) formula tree
    double  timeStep_    = 1.0;
    double  currentTime_ = 0.0;
    bool    schemaLoaded_ = false;

    std::vector<std::string>        boolFields_;   // boolean schema fields (ordered)
    std::unordered_set<std::string> formulaVars_;  // atoms referenced by the formula
    std::mt19937_64                 rng_;
    int                             lookahead_     = 11; // probe depth (env-overridable)
    int                             probeAttempts_ = 8;  // probe per-step budget (env-overridable)

    // One generated step: the chosen literals and the obligations carried forward.
    struct StepResult {
        std::unordered_map<std::string, bool> literals;
        std::vector<Obligation>               next;
    };
    // Generate one consistent step from `curr` with up to `attempts` intra-step
    // retries on an a/¬a contradiction; null if none was conflict-free.
    std::optional<StepResult> tryStep(const std::vector<Obligation>& curr, int attempts);
    // Walk `depth` steps from `start`, returning the first (the step to emit) iff the
    // whole walk stays conflict-free; nullopt if it dead-ends. Picks AND validates.
    std::optional<StepResult> probe(const std::vector<Obligation>& start, int depth);

    void          merge(std::vector<Obligation>& obs);  // collapse same-node obligations
    core::Message encode();                             // build a message from the literals

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
