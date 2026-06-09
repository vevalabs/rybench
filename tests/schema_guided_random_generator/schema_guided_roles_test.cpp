// Tests for FieldRole-driven generation in SchemaGuidedRandomGenerator:
// counter fields increment per message, timestamp fields carry wall-clock, and
// ordinary fields stay random within constraints — with the role parsed once
// from the .fbs `generate` attribute.
//
// ROLES_TEST_BFBS is injected by CMake as the absolute path to roles.bfbs.

#include <gtest/gtest.h>

#include <rybench/core/schema.hpp>
#include "flatbuffers_schema.hpp"
#include "flatbuffers_serializer.hpp"
#include "schema_guided_random_generator.hpp"

#include <flatbuffers/reflection.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string>

#ifndef ROLES_TEST_BFBS
#define ROLES_TEST_BFBS ""  // injected by CMake; fallback keeps editors quiet
#endif

using namespace rybench;

namespace {

const core::SchemaField* findField(const core::SchemaDefinition& schema,
                                   const std::string& name) {
    for (const auto& f : schema.fields())
        if (f.name == name) return &f;
    return nullptr;
}

const reflection::Field* rootField(const reflection::Schema* refl, const char* name) {
    return refl->root_table()->fields()->LookupByKey(name);
}

int64_t readInt(const reflection::Schema* refl, const uint8_t* buf, const char* f) {
    return flatbuffers::GetAnyFieldI(*flatbuffers::GetAnyRoot(buf), *rootField(refl, f));
}

double readDouble(const reflection::Schema* refl, const uint8_t* buf, const char* f) {
    return flatbuffers::GetAnyFieldF(*flatbuffers::GetAnyRoot(buf), *rootField(refl, f));
}

std::string readString(const reflection::Schema* refl, const uint8_t* buf, const char* f) {
    const auto* s = flatbuffers::GetFieldS(*flatbuffers::GetAnyRoot(buf), *rootField(refl, f));
    return s ? s->str() : std::string{};
}

size_t vectorSize(const reflection::Schema* refl, const uint8_t* buf, const char* f) {
    const auto* v = flatbuffers::GetFieldAnyV(*flatbuffers::GetAnyRoot(buf), *rootField(refl, f));
    return v ? v->size() : 0;
}

// Read int field `sub` of nested table field `parent`.
int64_t readNestedInt(const reflection::Schema* refl, const uint8_t* buf,
                      const char* parent, const char* sub) {
    const auto* root      = flatbuffers::GetAnyRoot(buf);
    const auto* parentF   = rootField(refl, parent);
    const auto* subTable  = flatbuffers::GetFieldT(*root, *parentF);
    const auto* parentObj = refl->objects()->Get(parentF->type()->index());
    const auto* subField  = parentObj->fields()->LookupByKey(sub);
    return flatbuffers::GetAnyFieldI(*subTable, *subField);
}

uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// Shared fixture: a loaded RoleEvent schema.
class SchemaGuidedRoles : public ::testing::Test {
protected:
    core::FlatBuffersSchema schema;
    void SetUp() override {
        schema.loadFromBfbs(ROLES_TEST_BFBS);
        ASSERT_TRUE(schema.isLoaded()) << "fixture bfbs missing: " ROLES_TEST_BFBS;
    }
    const reflection::Schema* refl() const { return schema.reflectionSchema(); }
};

} // namespace

// ─── Parser ─────────────────────────────────────────────────────────────────

TEST_F(SchemaGuidedRoles, ParserAssignsRolesFromAttribute) {
    const auto* seq   = findField(schema, "seq");
    const auto* ts    = findField(schema, "ts");
    const auto* value = findField(schema, "value");
    const auto* label = findField(schema, "label");

    ASSERT_TRUE(seq && ts && value && label);
    EXPECT_EQ(seq->role,   core::FieldRole::Counter);
    EXPECT_EQ(ts->role,    core::FieldRole::Timestamp);
    EXPECT_EQ(value->role, core::FieldRole::None);   // constraint, not a role
    EXPECT_EQ(label->role, core::FieldRole::None);
}

// ─── Generator ───────────────────────────────────────────────────────────────

TEST_F(SchemaGuidedRoles, CounterIncrementsPerMessage) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);

    for (int64_t i = 0; i < 8; ++i) {
        core::Message msg = gen.generate();
        ASSERT_FALSE(msg.empty());
        EXPECT_EQ(readInt(refl(), msg.data(), "seq"), i);
    }
}

TEST_F(SchemaGuidedRoles, TimestampIsWallClockAndMonotonic) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);

    const int64_t before = static_cast<int64_t>(nowMs());
    int64_t prev = 0;
    for (int i = 0; i < 5; ++i) {
        core::Message msg = gen.generate();
        int64_t t = readInt(refl(), msg.data(), "ts");
        EXPECT_GE(t, before) << "timestamp older than test start";
        EXPECT_GE(t, prev)   << "timestamp went backwards";
        prev = t;
    }
    EXPECT_LE(prev, static_cast<int64_t>(nowMs())) << "timestamp in the future";
}

TEST_F(SchemaGuidedRoles, OrdinaryFieldRespectsConstraint) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);

    for (int i = 0; i < 50; ++i) {
        core::Message msg = gen.generate();
        EXPECT_LE(readInt(refl(), msg.data(), "value"), 100);
    }
}

TEST_F(SchemaGuidedRoles, CounterResetsOnReload) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);

    gen.loadSchema(schema);
    gen.generate();
    gen.generate();

    gen.loadSchema(schema);  // reload should reset counters
    core::Message msg = gen.generate();
    EXPECT_EQ(readInt(refl(), msg.data(), "seq"), 0);
}

// ─── Field types: each respects its declared constraint ───────────────────────

TEST_F(SchemaGuidedRoles, DoubleRespectsRange) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        double r = readDouble(refl(), gen.generate().data(), "ratio");
        EXPECT_GE(r, -1.5);
        EXPECT_LE(r,  2.5);
    }
}

TEST_F(SchemaGuidedRoles, FloatRespectsRange) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        double t = readDouble(refl(), gen.generate().data(), "temp");
        EXPECT_GE(t, 10.0 - 1e-3);
        EXPECT_LE(t, 20.0 + 1e-3);
    }
}

TEST_F(SchemaGuidedRoles, StringRespectsLengthAndPattern) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        std::string s = readString(refl(), gen.generate().data(), "label");
        EXPECT_GE(s.size(), 4u);
        EXPECT_LE(s.size(), 8u);
        for (char c : s)
            EXPECT_TRUE(c >= 'A' && c <= 'Z') << "non [A-Z] char in '" << s << "'";
    }
}

TEST_F(SchemaGuidedRoles, EnumProducesValidValue) {
    const auto* color = findField(schema, "color");
    ASSERT_TRUE(color);
    const auto& allowed = color->enumNumericValues;
    ASSERT_FALSE(allowed.empty());

    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        int64_t c = readInt(refl(), gen.generate().data(), "color");
        EXPECT_NE(std::find(allowed.begin(), allowed.end(), c), allowed.end())
            << "enum value " << c << " not in schema";
    }
}

TEST_F(SchemaGuidedRoles, VectorRespectsItemBounds) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        size_t n = vectorSize(refl(), gen.generate().data(), "tags");
        EXPECT_GE(n, 2u);
        EXPECT_LE(n, 4u);
    }
}

TEST_F(SchemaGuidedRoles, NestedTableFieldRespectsConstraint) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        int64_t k = readNestedInt(refl(), gen.generate().data(), "inner", "k");
        EXPECT_GE(k, 5);
        EXPECT_LE(k, 9);
    }
}

TEST_F(SchemaGuidedRoles, BoolIsZeroOrOne) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        int64_t b = readInt(refl(), gen.generate().data(), "flag");
        EXPECT_TRUE(b == 0 || b == 1) << "bool was " << b;
    }
}

TEST_F(SchemaGuidedRoles, SignedIntegerWidthsRespectRange) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 200; ++i) {
        core::Message msg = gen.generate();       // keep alive across reads
        const uint8_t* b = msg.data();
        int64_t score = readInt(refl(), b, "score");  // byte
        int64_t level = readInt(refl(), b, "level");  // short
        int64_t big   = readInt(refl(), b, "big");    // long
        EXPECT_GE(score, -5);    EXPECT_LE(score, 5);
        EXPECT_GE(level, -100);  EXPECT_LE(level, 100);
        EXPECT_GE(big, 1000);    EXPECT_LE(big, 2000);
    }
}

TEST_F(SchemaGuidedRoles, UnsignedIntegerWidthsRespectRange) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 200; ++i) {
        core::Message msg = gen.generate();       // keep alive across reads
        const uint8_t* b = msg.data();
        int64_t count   = readInt(refl(), b, "count");    // uint
        int64_t ucount  = readInt(refl(), b, "ucount");   // ulong
        EXPECT_GE(count, 10);    EXPECT_LE(count, 20);
        EXPECT_GE(ucount, 100);  EXPECT_LE(ucount, 200);
    }
}

TEST_F(SchemaGuidedRoles, UnboundedUnsignedIsNonNegative) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 200; ++i) {
        int64_t u = readInt(refl(), gen.generate().data(), "ubyte_v");
        EXPECT_GE(u, 0) << "unsigned field produced a negative value";
    }
}

TEST_F(SchemaGuidedRoles, StringWithoutPatternRespectsLength) {
    serializer::FlatbuffersSerializer backend(ROLES_TEST_BFBS);
    generator::SchemaGuidedRandomGenerator gen(backend, /*seed=*/1);
    gen.loadSchema(schema);
    for (int i = 0; i < 100; ++i) {
        std::string s = readString(refl(), gen.generate().data(), "code");
        EXPECT_GE(s.size(), 2u);
        EXPECT_LE(s.size(), 6u);
        for (char c : s)
            EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)))
                << "non-alphanumeric char in '" << s << "'";
    }
}
