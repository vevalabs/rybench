// Minimal one-variable MTL trace example for the thesis.
//
// Links librybench and calls the property_guided MTL generator DIRECTLY — no
// external binary, no .bfbs/flatc, no dump/decode. The formula (over a single atom
// p) is a string right here; the schema is built in code, with the formula carried
// on the root table's "mtl" attribute (what loadSchema reads). The trace is printed
// as a bit string, values side by side:  1010101010...
//
// Build:  cmake --build build --target mtl_one_var_example
// Run:    ./build/bin/mtl_one_var_example ["<formula>" [seed [lookahead]]]

#include <rybench/core/schema.hpp>
#include <rybench/serializer/serializer.hpp>
#include "rybench/generator/property_guided/property_guided_mtl_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

using rybench::core::FieldType;
using rybench::core::SchemaField;

// ---- edit here ------------------------------------------------------------
// Generator MTL syntax: always[a,b] / eventually[a,b] / until[a,b], and/or/implies,
// not, single atom p. Binary ops must be parenthesised, e.g. (p implies q).
// G[a,b] -> always[a,b],  F[a,b] -> eventually[a,b],  next -> always[1,1].
static const std::string FORMULA =
    "always[0,45] (((not p) until[0,4] p))";  // bounded -> trace is finite, ends on its own
static const std::string ATOM      = "p";  // the single boolean variable
static const std::chrono::microseconds EVENT_PERIOD{100};  // 100 us/event = 10000 events/s
static const int      DEFAULT_LOOKAHEAD = 5;   // probe depth (arg 2)
static const uint64_t DEFAULT_SEED      = 1;   // used when no seed arg is given (0 = fresh random)
// ---------------------------------------------------------------------------

// Schema built in code: one bool for the atom + an integer timestamp (the generator
// needs one), and the MTL formula on the root table's "mtl" attribute.
class InlineSchema : public rybench::core::SchemaDefinition {
public:
    InlineSchema(std::string mtl, const std::string& atom) : mtl_(std::move(mtl)) {
        rootName_ = "Event";
        SchemaField ts;   ts.name = "timestamp"; ts.type = FieldType::ULong; fields_.push_back(ts);
        SchemaField a;    a.name  = atom;         a.type  = FieldType::Bool;  fields_.push_back(a);
    }
    std::string attribute(const std::string& key) const override {
        return key == "mtl" ? mtl_ : std::string{};
    }
private:
    std::string mtl_;
};

// Generation backend that just captures the atom's bit for the current step.
class BitSink : public rybench::serializer::Serializer {
public:
    std::string name() const override { return "bit"; }
    rybench::core::MessageFormat format() const override {
        return rybench::core::MessageFormat::Unknown;
    }
    void loadSchema(const rybench::core::SchemaDefinition&) override {}
    void initMessage() override {}
    void addBool  (const SchemaField&, bool v) override { bit_ = v ? '1' : '0'; }
    void addUInt  (const SchemaField&, uint64_t) override {}
    void addInt   (const SchemaField&, int64_t) override {}
    void addFloat (const SchemaField&, float) override {}
    void addDouble(const SchemaField&, double) override {}
    void addString(const SchemaField&, std::string_view) override {}
    void beginTable (const SchemaField&) override {}
    void endTable   () override {}
    void beginVector(const SchemaField&, size_t) override {}
    void endVector  () override {}
    rybench::core::Message finalizeMessage() override { return rybench::core::Message(); }
    std::span<const uint8_t> serialize(const rybench::core::Message&) override { return {}; }

    char bit() const { return bit_; }
private:
    char bit_ = '0';
};

int main(int argc, char** argv) {
    // Optional args:  <formula> <seed> <lookahead>   (seed 0 = fresh random trace each run).
    const std::string formula   = (argc > 1) ? argv[1] : FORMULA;
    const uint64_t    seed      = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : DEFAULT_SEED;
    const int         lookahead = (argc > 3) ? std::max(1, std::atoi(argv[3]))     : DEFAULT_LOOKAHEAD;

    setenv("RYBENCH_LOOKAHEAD", std::to_string(lookahead).c_str(), 1);

    InlineSchema schema(formula, ATOM);
    BitSink      sink;
    rybench::generator::PropertyGuidedMTLGenerator gen(sink, /*timeStep=*/1.0, seed);
    gen.loadSchema(schema);

    std::cout << "formula   : " << formula << "\n";
    std::cout << "lookahead : " << lookahead << "    seed: " << seed
              << "    rate: " << (std::chrono::seconds(1) / EVENT_PERIOD) << " events/s\n\n";

    // Stream forever (Ctrl+C to stop), one bit per event, paced at EVENT_PERIOD.
    std::cout << ATOM << ": " << std::flush;
    std::uint64_t steps = 0;
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        try {
            gen.generate();               // one step; the atom bit lands in the sink
        } catch (const std::exception&) { // bounded formula fully satisfied -> finite trace
            std::cout << "\n\ntrace finished on its own after " << steps
                      << " steps (all obligations discharged)\n";
            return 0;
        }
        std::cout << sink.bit() << std::flush;
        ++steps;
        next += EVENT_PERIOD;
        std::this_thread::sleep_until(next);
    }
}
