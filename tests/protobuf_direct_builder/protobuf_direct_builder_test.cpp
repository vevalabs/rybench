// Tests for the direct Protobuf builder in ProtobufSerializer: the field API
// (initMessage / addX / beginTable / beginVector / finalizeMessage) must build a
// DynamicMessage in place and serialize it to wire bytes, without the intermediate
// FlatBuffer + convert step.
//
// Strategy: drive the SAME schema-guided generation (same seed, in lockstep) once
// through a FlatBuffers backend + flat→proto convert, and once through the Protobuf
// backend directly. Both encode identical field values, so the canonical protobuf
// bytes must match. A second test parses the direct output back to confirm it is
// valid, structured protobuf.
//
// BUILDER_TEST_BFBS is injected by CMake as the absolute path to builder.bfbs.

#include <gtest/gtest.h>

#include <rybench/core/message.hpp>
#include "flatbuffers_schema.hpp"
#include "flatbuffers_serializer.hpp"
#include "protobuf_serializer.hpp"
#include "schema_guided_random_generator.hpp"

#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#ifndef BUILDER_TEST_BFBS
#define BUILDER_TEST_BFBS ""  // injected by CMake; fallback keeps editors quiet
#endif
#ifndef HVD_TEST_BFBS
#define HVD_TEST_BFBS ""      // real OSI HostVehicleData schema, injected by CMake
#endif

using namespace rybench;

namespace {
std::vector<uint8_t> toVec(std::span<const uint8_t> s) { return {s.begin(), s.end()}; }
}

// Direct build must be byte-identical to build-a-FlatBuffer-then-convert: identical
// field values, identical canonical protobuf encoding.
TEST(ProtobufDirectBuilder, DirectEqualsConvert) {
    const std::string bfbs = BUILDER_TEST_BFBS;
    ASSERT_FALSE(bfbs.empty());

    for (uint64_t seed : {1ull, 7ull, 42ull, 1234ull}) {
        serializer::FlatbuffersSerializer fbBackend(bfbs);
        serializer::ProtobufSerializer   pbBackend(bfbs);   // direct build
        serializer::ProtobufSerializer   pbConvert(bfbs);   // flat -> proto convert

        ASSERT_NE(fbBackend.loadedSchema(), nullptr);
        ASSERT_NE(pbBackend.loadedSchema(), nullptr);

        generator::SchemaGuidedRandomGenerator genFb(fbBackend, seed);
        generator::SchemaGuidedRandomGenerator genPb(pbBackend, seed);
        genFb.loadSchema(*fbBackend.loadedSchema());
        genPb.loadSchema(*pbBackend.loadedSchema());

        for (int i = 0; i < 100; ++i) {
            core::Message mfb = genFb.generate();
            std::vector<uint8_t> convert = toVec(pbConvert.serialize(mfb));

            core::Message mpb = genPb.generate();          // protobuf bytes, built directly
            std::vector<uint8_t> direct = toVec(mpb.bytes());

            ASSERT_EQ(direct, convert) << "seed=" << seed << " msg=" << i;
        }
    }
}

// The direct output must be valid, parseable protobuf with the expected structure:
// the nested table is present and the min_items vectors carry at least one element.
TEST(ProtobufDirectBuilder, DirectOutputParses) {
    const std::string bfbs = BUILDER_TEST_BFBS;
    ASSERT_FALSE(bfbs.empty());

    serializer::ProtobufSerializer pbBackend(bfbs);
    ASSERT_NE(pbBackend.loadedSchema(), nullptr);

    generator::SchemaGuidedRandomGenerator gen(pbBackend, 99);
    gen.loadSchema(*pbBackend.loadedSchema());

    core::Message m = gen.generate();
    auto bytes = m.bytes();
    ASSERT_FALSE(bytes.empty());

    std::unique_ptr<google::protobuf::Message> parsed(pbBackend.newRootInstanceForTest());
    ASSERT_NE(parsed, nullptr);
    ASSERT_TRUE(parsed->ParseFromArray(bytes.data(), static_cast<int>(bytes.size())));

    const auto* d = parsed->GetDescriptor();
    const auto* r = parsed->GetReflection();

    const auto* ints  = d->FindFieldByName("ints");
    const auto* items = d->FindFieldByName("items");
    const auto* inner = d->FindFieldByName("inner");
    ASSERT_NE(ints,  nullptr);
    ASSERT_NE(items, nullptr);
    ASSERT_NE(inner, nullptr);

    EXPECT_GE(r->FieldSize(*parsed, ints),  1);   // min_items:"1"
    EXPECT_GE(r->FieldSize(*parsed, items), 1);   // min_items:"1", vector-of-table
    EXPECT_TRUE(r->HasField(*parsed, inner));     // singular nested table always built
}

// The real OSI HostVehicleData schema — deep nesting, enums, an enum vector and a
// table vector — must build valid, complete protobuf. A parse then re-serialize
// round-trip that reproduces the original bytes proves the output is canonical and
// nothing was dropped (unlike the flat->convert path, which silently drops the
// enum vector).
TEST(ProtobufDirectBuilder, RealSchemaRoundTrips) {
    const std::string bfbs = HVD_TEST_BFBS;
    ASSERT_FALSE(bfbs.empty());

    serializer::ProtobufSerializer pbBackend(bfbs);
    ASSERT_NE(pbBackend.loadedSchema(), nullptr);

    generator::SchemaGuidedRandomGenerator gen(pbBackend, 2024);
    gen.loadSchema(*pbBackend.loadedSchema());

    for (int i = 0; i < 50; ++i) {
        core::Message m = gen.generate();
        std::vector<uint8_t> original = toVec(m.bytes());
        ASSERT_GT(original.size(), 200u) << "msg=" << i;   // a deep OSI message is large

        std::unique_ptr<google::protobuf::Message> parsed(pbBackend.newRootInstanceForTest());
        ASSERT_NE(parsed, nullptr);
        ASSERT_TRUE(parsed->ParseFromArray(original.data(), static_cast<int>(original.size())))
            << "msg=" << i;

        const std::size_t n = static_cast<std::size_t>(parsed->ByteSizeLong());
        std::vector<uint8_t> reserialized(n);
        ASSERT_TRUE(parsed->SerializeToArray(reserialized.data(), static_cast<int>(n)));
        ASSERT_EQ(reserialized, original) << "msg=" << i;   // canonical + complete
    }
}
