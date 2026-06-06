#include "flatbuffers_serializer.hpp"
#include <cstring>
#include <stdexcept>

namespace rybench::serializer {

FlatbuffersSerializer::FlatbuffersSerializer(const std::string& bfbsPath) {
    if (!bfbsPath.empty()) {
        try { fbsSchema_.loadFromBfbs(bfbsPath); }
        catch (...) {}
    }
}

std::string FlatbuffersSerializer::name() const { return "flatbuffers"; }

core::MessageFormat FlatbuffersSerializer::format() const {
    return core::MessageFormat::FlatBuffers;
}

std::span<const uint8_t> FlatbuffersSerializer::serialize(const core::Message& msg) {
    if (msg.format() == core::MessageFormat::FlatBuffers)
        return msg.bytes();
    throw std::runtime_error("FlatbuffersSerializer: unsupported source format");
}

void FlatbuffersSerializer::loadSchema(const core::SchemaDefinition&) {}

void FlatbuffersSerializer::initMessage() {
    stack_.clear();
    builder_.Clear();
    stack_.push_back(Context{});
}

// ── helpers ───────────────────────────────────────────────────────────────────

static int64_t doubleBits(double v) {
    int64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

// ── add* ──────────────────────────────────────────────────────────────────────

void FlatbuffersSerializer::addBool(const core::SchemaField& f, bool val) {
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorScalars.push_back(val ? 1 : 0); return; }
    ctx.scalars.push_back({ f.fieldId, core::FieldType::Bool, val ? 1LL : 0LL, f.defaultInt });
}

void FlatbuffersSerializer::addInt(const core::SchemaField& f, int64_t val) {
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorScalars.push_back(val); return; }
    ctx.scalars.push_back({ f.fieldId, f.type, val, f.defaultInt });
}

void FlatbuffersSerializer::addUInt(const core::SchemaField& f, uint64_t val) {
    int64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorScalars.push_back(bits); return; }
    ctx.scalars.push_back({ f.fieldId, f.type, bits, f.defaultInt });
}

void FlatbuffersSerializer::addFloat(const core::SchemaField& f, float val) {
    double d = val;
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorScalars.push_back(doubleBits(d)); return; }
    ctx.scalars.push_back({ f.fieldId, core::FieldType::Float, doubleBits(d), doubleBits(f.defaultDouble) });
}

void FlatbuffersSerializer::addDouble(const core::SchemaField& f, double val) {
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorScalars.push_back(doubleBits(val)); return; }
    ctx.scalars.push_back({ f.fieldId, core::FieldType::Double, doubleBits(val), doubleBits(f.defaultDouble) });
}

void FlatbuffersSerializer::addString(const core::SchemaField& f, std::string_view val) {
    auto offset = builder_.CreateString(val.data(), val.size());
    auto& ctx = stack_.back();
    if (ctx.isVector) { ctx.vectorOffsets.push_back(offset.Union()); return; }
    ctx.offsets.push_back({ f.fieldId, offset.Union() });
}

// ── table/vector stack ────────────────────────────────────────────────────────

void FlatbuffersSerializer::beginTable(const core::SchemaField& f) {
    stack_.push_back(Context{ f.fieldId });
}

void FlatbuffersSerializer::endTable() {
    if (stack_.size() < 2) return;
    Context ctx = std::move(stack_.back());
    stack_.pop_back();
    writeTable(ctx, false);
}

void FlatbuffersSerializer::beginVector(const core::SchemaField& f, size_t) {
    core::FieldType elemType = f.children.empty()
        ? core::FieldType::Unknown : f.children[0].type;
    Context ctx;
    ctx.parentFieldId  = f.fieldId;
    ctx.isVector       = true;
    ctx.vectorElemType = elemType;
    stack_.push_back(std::move(ctx));
}

void FlatbuffersSerializer::endVector() {
    if (stack_.size() < 2) return;
    Context ctx = std::move(stack_.back());
    stack_.pop_back();
    writeVector(ctx);
}

core::Message FlatbuffersSerializer::finalizeMessage() {
    if (stack_.empty()) return core::Message();
    Context ctx = std::move(stack_.front());
    stack_.clear();
    writeTable(ctx, true);
    auto* buf = builder_.GetBufferPointer();
    auto  sz  = builder_.GetSize();
    return core::Message(std::vector<uint8_t>(buf, buf + sz),
                         core::MessageFormat::FlatBuffers);
}

// ── writeTable ────────────────────────────────────────────────────────────────

void FlatbuffersSerializer::writeTable(Context& ctx, bool isRoot) {
    auto start = builder_.StartTable();

    for (auto& s : ctx.scalars) {
        using FT = core::FieldType;
        switch (s.type) {
            case FT::Bool:
                builder_.AddElement<uint8_t>(s.vtableOffset,
                    s.value ? 1 : 0, static_cast<uint8_t>(s.defaultVal)); break;
            case FT::Byte:
                builder_.AddElement<int8_t>(s.vtableOffset,
                    static_cast<int8_t>(s.value), static_cast<int8_t>(s.defaultVal)); break;
            case FT::UByte:
                builder_.AddElement<uint8_t>(s.vtableOffset,
                    static_cast<uint8_t>(s.value), static_cast<uint8_t>(s.defaultVal)); break;
            case FT::Short:
                builder_.AddElement<int16_t>(s.vtableOffset,
                    static_cast<int16_t>(s.value), static_cast<int16_t>(s.defaultVal)); break;
            case FT::UShort:
                builder_.AddElement<uint16_t>(s.vtableOffset,
                    static_cast<uint16_t>(s.value), static_cast<uint16_t>(s.defaultVal)); break;
            case FT::Int: case FT::Enum:
                builder_.AddElement<int32_t>(s.vtableOffset,
                    static_cast<int32_t>(s.value), static_cast<int32_t>(s.defaultVal)); break;
            case FT::UInt: {
                uint32_t v, d;
                std::memcpy(&v, &s.value,      sizeof(v));
                std::memcpy(&d, &s.defaultVal, sizeof(d));
                builder_.AddElement<uint32_t>(s.vtableOffset, v, d); break;
            }
            case FT::Long:
                builder_.AddElement<int64_t>(s.vtableOffset, s.value, s.defaultVal); break;
            case FT::ULong: {
                uint64_t v, d;
                std::memcpy(&v, &s.value,      sizeof(v));
                std::memcpy(&d, &s.defaultVal, sizeof(d));
                builder_.AddElement<uint64_t>(s.vtableOffset, v, d); break;
            }
            case FT::Float: {
                double v, d;
                std::memcpy(&v, &s.value,      sizeof(v));
                std::memcpy(&d, &s.defaultVal, sizeof(d));
                builder_.AddElement<float>(s.vtableOffset,
                    static_cast<float>(v), static_cast<float>(d)); break;
            }
            case FT::Double: {
                double v, d;
                std::memcpy(&v, &s.value,      sizeof(v));
                std::memcpy(&d, &s.defaultVal, sizeof(d));
                builder_.AddElement<double>(s.vtableOffset, v, d); break;
            }
            default: break;
        }
    }

    for (auto& [voff, off] : ctx.offsets)
        builder_.AddOffset(voff, off);

    auto tableOffset = flatbuffers::Offset<void>(builder_.EndTable(start));

    if (isRoot) {
        builder_.Finish(flatbuffers::Offset<flatbuffers::Table>(tableOffset.o));
    } else if (!stack_.empty()) {
        auto& parent = stack_.back();
        if (parent.isVector)
            parent.vectorOffsets.push_back(tableOffset);
        else
            parent.offsets.push_back({ ctx.parentFieldId, tableOffset });
    }
}

// ── writeVector ───────────────────────────────────────────────────────────────

void FlatbuffersSerializer::writeVector(Context& ctx) {
    using FT = core::FieldType;
    flatbuffers::Offset<void> vecOffset;

    auto& sc = ctx.vectorScalars;
    auto& of = ctx.vectorOffsets;

    switch (ctx.vectorElemType) {
        case FT::Bool: {
            std::vector<uint8_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(x ? 1 : 0);
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Byte: {
            std::vector<int8_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(static_cast<int8_t>(x));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::UByte: {
            std::vector<uint8_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(static_cast<uint8_t>(x));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Short: {
            std::vector<int16_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(static_cast<int16_t>(x));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::UShort: {
            std::vector<uint16_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(static_cast<uint16_t>(x));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Int: case FT::Enum: {
            std::vector<int32_t> v; v.reserve(sc.size());
            for (auto x : sc) v.push_back(static_cast<int32_t>(x));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::UInt: {
            std::vector<uint32_t> v; v.reserve(sc.size());
            for (auto x : sc) { uint32_t u; std::memcpy(&u, &x, sizeof(u)); v.push_back(u); }
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Long: {
            std::vector<int64_t> v(sc.begin(), sc.end());
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::ULong: {
            std::vector<uint64_t> v; v.reserve(sc.size());
            for (auto x : sc) { uint64_t u; std::memcpy(&u, &x, sizeof(u)); v.push_back(u); }
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Float: {
            std::vector<float> v; v.reserve(sc.size());
            for (auto x : sc) { double d; std::memcpy(&d, &x, sizeof(d)); v.push_back(static_cast<float>(d)); }
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::Double: {
            std::vector<double> v; v.reserve(sc.size());
            for (auto x : sc) { double d; std::memcpy(&d, &x, sizeof(d)); v.push_back(d); }
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        case FT::String: {
            std::vector<flatbuffers::Offset<flatbuffers::String>> v; v.reserve(of.size());
            for (auto o : of) v.push_back(flatbuffers::Offset<flatbuffers::String>(o.o));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
        default: {
            // Table/Struct vector — offsets collected by endTable() calls
            std::vector<flatbuffers::Offset<flatbuffers::Table>> v; v.reserve(of.size());
            for (auto o : of) v.push_back(flatbuffers::Offset<flatbuffers::Table>(o.o));
            vecOffset = builder_.CreateVector(v).Union(); break;
        }
    }

    if (!stack_.empty())
        stack_.back().offsets.push_back({ ctx.parentFieldId, vecOffset });
}

} // namespace rybench::serializer
