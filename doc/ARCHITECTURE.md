# Architecture Documentation

## Code Structure

```
src/
├── common/                          # Shared utilities and data types
│   ├── include/
│   │   ├── config.hpp               # INI configuration parser
│   │   ├── data_value.hpp           # Generic data container (variant-based)
│   │   ├── recorder.hpp             # Data recording utility
│   │   ├── schema.hpp               # JSON schema definition
│   │   └── signal_handler.hpp       # Signal handling utility
│   ├── config.cpp
│   ├── schema.cpp
│   ├── recorder.cpp
│   └── CMakeLists.txt
│
├── publisher/                       # IPC publisher implementations
│   ├── include/
│   │   ├── publisher.hpp            # IPublisher interface
│   │   └── publisher_factory.hpp    # Publisher factory
│   ├── iceoryx/
│   │   ├── include/
│   │   │   └── iceoryx_publisher.hpp
│   │   └── iceoryx_publisher.cpp
│   ├── zenoh/
│   │   ├── include/
│   │   │   └── zenoh_publisher.hpp
│   │   └── zenoh_publisher.cpp
│   ├── publisher_factory.cpp
│   └── CMakeLists.txt
│
├── generator/                       # Data generator implementations
│   ├── include/
│   │   ├── generator.hpp            # IGenerator interface
│   │   └── generator_factory.hpp    # Generator factory
│   ├── fixed/
│   │   ├── include/
│   │   │   └── fixed_generator.hpp
│   │   └── fixed_generator.cpp
│   ├── schema_guided/
│   │   ├── include/
│   │   │   └── schema_guided_generator.hpp
│   │   └── schema_guided_generator.cpp
│   ├── property_guided/
│   │   ├── include/
│   │   │   └── property_guided_generator.hpp
│   │   └── property_guided_generator.cpp
│   ├── generator_factory.cpp
│   └── CMakeLists.txt
│
├── serializer/                      # Serialization implementations
│   ├── include/
│   │   ├── serializer.hpp           # ISerializer interface
│   │   └── serializer_factory.hpp   # Serializer factory
│   ├── json/
│   │   ├── include/
│   │   │   └── json_serializer.hpp
│   │   └── json_serializer.cpp
│   ├── protobuf/
│   │   ├── include/
│   │   │   └── protobuf_serializer.hpp
│   │   └── protobuf_serializer.cpp
│   ├── flatbuffers/
│   │   ├── include/
│   │   │   └── flatbuffers_serializer.hpp
│   │   └── flatbuffers_serializer.cpp
│   ├── messages/
│   │   ├── osi_sensorview_pb.cpp    # Protobuf message registration
│   │   └── osi_sensorview_fbs.cpp   # FlatBuffers message registration
│   ├── serializer_factory.cpp
│   └── CMakeLists.txt
│
├── rybench/                         # Main application
│   ├── main.cpp
│   └── CMakeLists.txt
│
├── test/                            # Test consumers
│   ├── consumer_json.cpp
│   ├── consumer_protobuf.cpp
│   ├── consumer_flatbuffers.cpp
│   ├── zenoh_consumer_json.cpp
│   ├── zenoh_consumer_protobuf.cpp
│   └── zenoh_consumer_flatbuffers.cpp
│
config/
├── publisher.ini                    # Publisher configuration
├── schemas/                         # JSON schema definitions
├── protos/                          # Protobuf definitions
└── flatbuffers/                     # FlatBuffers definitions
```

---

## Data Flow

```
Generator → DataValue → Serializer → Binary Data → Publisher → IPC
```

1. **Serializer** provides schema describing expected message structure
2. **Generator** produces `DataValue` based on schema
3. **Serializer** converts `DataValue` to binary format
4. **Publisher** sends binary data via IPC (iceoryx/zenoh)

---

## Interfaces

### common::Config

INI configuration file parser.

```cpp
namespace common {

class Config {
public:
    void load(const std::string& filepath);

    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue = "") const;
    int getInt(const std::string& section, const std::string& key,
               int defaultValue = 0) const;
    double getDouble(const std::string& section, const std::string& key,
                     double defaultValue = 0.0) const;
    bool getBool(const std::string& section, const std::string& key,
                 bool defaultValue = false) const;

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;
    const Sections& sections() const;
};

}
```

| Method | Description |
|--------|-------------|
| `load(filepath)` | Load INI file from path |
| `get(section, key, default)` | Get string value |
| `getInt(section, key, default)` | Get integer value |
| `getDouble(section, key, default)` | Get double value |
| `getBool(section, key, default)` | Get boolean value |
| `hasSection(section)` | Check if section exists |
| `hasKey(section, key)` | Check if key exists in section |
| `sections()` | Get all sections as map |

---

### common::Schema

JSON schema definition for data structure.

```cpp
namespace common {

enum class FieldType { Int, Double, String, Bool, Array, Object };

struct FieldConstraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<size_t> minLength;
    std::optional<size_t> maxLength;
    std::optional<size_t> arraySize;
};

struct SchemaField {
    std::string name;
    FieldType type;
    FieldConstraints constraints;
    std::vector<SchemaField> children;
};

class Schema {
public:
    void loadFromFile(const std::string& filepath);
    void loadFromJson(const nlohmann::json& j);

    const std::vector<SchemaField>& fields() const;
    const std::string& name() const;
};

}
```

| Method | Description |
|--------|-------------|
| `loadFromFile(filepath)` | Load schema from JSON file for json-serializer |
| `loadFromJson(json)` | Load schema from JSON object - all serializers use to pass json to generator |
| `fields()` | Get list of schema fields |
| `name()` | Get schema name |

---

### common::DataValue

Generic data container using `std::variant`.

```cpp
namespace common {

using DataObject = std::unordered_map<std::string, DataValue>;
using DataArray = std::vector<DataValue>;

class DataValue {
public:
    // Constructors for all types
    DataValue();                          // null
    DataValue(std::nullptr_t);            // null
    DataValue(bool v);                    // boolean
    DataValue(int v);                     // integer
    DataValue(int64_t v);                 // integer
    DataValue(double v);                  // double
    DataValue(const std::string& v);      // string
    DataValue(const DataArray& v);        // array
    DataValue(const DataObject& v);       // object

    // Type checking
    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    // Value access
    bool asBool() const;
    int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const DataArray& asArray() const;
    const DataObject& asObject() const;
    DataArray& asArray();
    DataObject& asObject();
};

}
```

| Method | Description |
|--------|-------------|
| `isNull/Bool/Int/Double/String/Array/Object()` | Check value type |
| `asBool/Int/Double/String/Array/Object()` | Get value (throws if wrong type) |

---

### common::Recorder

Utility for recording data to JSONL files.

```cpp
namespace common {

class Recorder {
public:
    void open(const std::string& filepath);
    void record(const DataValue& value);
    void close();

    size_t count() const;
    bool isOpen() const;
};

}
```

| Method | Description |
|--------|-------------|
| `open(filepath)` | Open file for writing |
| `record(value)` | Write DataValue as JSON line |
| `close()` | Close file |
| `count()` | Number of records written |
| `isOpen()` | Check if file is open |

---

### generator::IGenerator

Interface for data generators.

```cpp
namespace generator {

enum class Algorithm { Fixed, SemiRandom, SchemaGuided, PropertyGuided };

class IGenerator {
public:
    virtual ~IGenerator() = default;

    virtual void loadSchema(const common::Schema& schema) = 0;
    virtual common::DataValue generate() = 0;
    virtual Algorithm algorithm() const = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<IGenerator>;

// Factory function
GeneratorPtr createGenerator(const common::Config& config, const common::Schema& schema);

}
```

| Method | Description |
|--------|-------------|
| `loadSchema(schema)` | Load schema for data generation which created by serializer on start |
| `generate()` | Generate next data value |
| `algorithm()` | Get generator algorithm type |
| `name()` | Get generator name |

#### Implementations

| Class | Algorithm | Description |
|-------|-----------|-------------|
| `FixedGenerator` | Fixed | Returns fixed/pre-recorded data |
| `SchemaGuidedGenerator` | SchemaGuided | Random data based on schema constraints |
| `PropertyGuidedGenerator` | PropertyGuided | Custom property generators per field - not implemented yet |

##### FixedGenerator (additional methods)

```cpp
void loadDump(const std::string& filepath);  // Load JSONL dump file - only opens file, reads and generates for evey cycle
void reset();                                  // Reset to beginning
bool finished() const;                         // Check if dump exhausted
```

##### SchemaGuidedGenerator (additional methods)
```cpp
// No additional methods
```
##### PropertyGuidedGenerator (additional methods)

```cpp
// Not decided 
```

---

### publisher::IPublisher

Interface for IPC publishers.

```cpp
namespace publisher {

struct PublisherConfig {
    std::string serviceName = "Benchmark";
    std::string instanceName = "Instance";
    std::string eventName = "Event";
    size_t maxPayloadSize = 4096;
};

class IPublisher {
public:
    virtual ~IPublisher() = default;

    virtual void init(const PublisherConfig& config) = 0;
    virtual bool publish(const void* data, size_t size) = 0;
    virtual void shutdown() = 0;
    virtual std::string name() const = 0;
};

using PublisherPtr = std::unique_ptr<IPublisher>;

// Factory functions
PublisherConfig createPublisherConfig(const common::Config& config);
PublisherPtr createPublisher(const common::Config& config);

}
```

| Method | Description |
|--------|-------------|
| `init(config)` | Initialize publisher with configuration |
| `publish(data, size)` | Publish binary data, returns success |
| `shutdown()` | Cleanup and shutdown |
| `name()` | Get publisher name |

#### Implementations

| Class | Transport | Description |
|-------|-----------|-------------|
| `IceoryxPublisher` | iceoryx | Zero-copy shared memory IPC |
| `ZenohPublisher` | Zenoh | Network-capable pub/sub |

##### IceoryxPublisher (additional methods)

```cpp
static void initRuntime(const std::string& appName);  // Initialize iceoryx runtime
```

---

### serializer::ISerializer

Interface for data serialization.

```cpp
namespace serializer {

class ISerializer {
public:
    virtual ~ISerializer() = default;

    virtual std::vector<uint8_t> serialize(const common::DataValue& value) = 0;
    virtual common::DataValue deserialize(const std::vector<uint8_t>& data) = 0;
    virtual common::Schema getSchema() const = 0;
    virtual std::string name() const = 0;
};

using SerializerPtr = std::unique_ptr<ISerializer>;

// Factory function
SerializerPtr createSerializer(const common::Config& config);

}
```

| Method | Description |
|--------|-------------|
| `serialize(value)` | Convert DataValue to binary |
| `deserialize(data)` | Convert binary to DataValue |
| `getSchema()` | Get schema for generator |
| `name()` | Get serializer name |

#### Implementations

| Class | Format | Description |
|-------|--------|-------------|
| `JsonSerializer` | JSON | Human-readable JSON format |
| `ProtobufSerializer` | Protobuf | Google Protocol Buffers |
| `FlatbuffersSerializer` | FlatBuffers | Zero-copy serialization |

##### ProtobufSerializer (additional methods)

```cpp
explicit ProtobufSerializer(const std::string& messageName = "",
                            const std::string& fieldsConfigPath = "");

template<typename T>
static void registerMessage(const std::string& name);
static std::vector<std::string> availableMessages();
static std::string availableMessagesStr();
static MessageFactory getFactory(const std::string& name);
```

| Method | Description |
|--------|-------------|
| `ProtobufSerializer(messageName, fieldsConfigPath)` | Constructor with message type name and optional field constraints file |
| `registerMessage<T>(name)` | Register a protobuf message type for serialization |
| `availableMessages()` | Get list of all registered message type names |
| `availableMessagesStr()` | Get comma-separated string of registered message types |
| `getFactory(name)` | Get factory function for creating message by name |

##### FlatbuffersSerializer (additional methods)

```cpp
explicit FlatbuffersSerializer(const std::string& messageName = "",
                               const std::string& bfbsPath = "");

static void registerMessage(const std::string& name, BuilderFunc builder, ParserFunc parser);
static std::vector<std::string> availableMessages();
static std::string availableMessagesStr();
```

| Method | Description |
|--------|-------------|
| `FlatbuffersSerializer(messageName, bfbsPath)` | Constructor with message type name and optional binary schema path (.bfbs) |
| `registerMessage(name, builder, parser)` | Register message with custom build/parse functions |
| `availableMessages()` | Get list of all registered message type names |
| `availableMessagesStr()` | Get comma-separated string of registered message types |
---

## Configuration

### publisher.ini Example

```ini
[publisher]
type = iceoryx          # iceoryx or zenoh
service = Benchmark
instance = Default
event = Data
rate_hz = 100
max_payload_size = 65536

[serializer]
type = protobuf         # json, protobuf, or flatbuffers
proto_message = osi::SensorViewMessage

[generator]
type = schema_guided    # fixed, schema_guided, or property_guided
schema = config/schemas/sensor.json
seed = 0

[dump]
enabled = false
output = dump/benchmark.jsonl
```
