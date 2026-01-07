#pragma once

#include "generator.hpp"
#include <fstream>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace generator {

class FixedGenerator : public IGenerator {
public:
    FixedGenerator() = default;
    ~FixedGenerator();

    void loadSchema(const common::Schema& schema) override;
    void loadDump(const std::string& filepath);
    common::DataValue generate() override;

    void reset();
    bool finished() const;

    Algorithm algorithm() const override;
    std::string name() const override;

private:
    common::Schema schema_;
    std::ifstream file_;
    bool finished_ = false;
    bool useSchemaData_ = false;
    common::DataValue schemaData_;

    common::DataValue buildFixedFromSchema();
    common::DataValue buildFixedField(const common::SchemaField& field);
    static common::DataValue jsonToDataValue(const nlohmann::json& j);
    static nlohmann::json dataValueToJson(const common::DataValue& v);
};

} // namespace generator
