# Architecture Documentation

## Code Structure

```
src/
├── common/                          # Shared utilities and data types
│   ├── include/
│   │   ├── config.hpp               # TOML configuration parser
│   │   ├── flatbuffers_schema.hpp   # FlatBuffers schema loader
│   │   ├── message.hpp              # Message wrapper for FlatBuffer data
│   │   ├── recorder.hpp             # Data recording utility
│   │   └── signal_handler.hpp       # Signal handling utility
│   ├── config.cpp
│   ├── flatbuffers_schema.cpp
│   ├── message.cpp
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
│   │   ├── generator_factory.hpp    # Generator factory
│   │   └── reflection_builder.hpp   # FlatBuffer reflection builder
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
│   ├── reflection_builder.cpp
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
│   ├── serializer_factory.cpp
│   └── CMakeLists.txt
│
├── rybench/                         # Main application
│   ├── main.cpp
│   └── CMakeLists.txt
│
test/
├── consumer_json.cpp
├── consumer_protobuf.cpp
├── consumer_flatbuffers.cpp
├── zenoh_consumer_json.cpp
├── zenoh_consumer_protobuf.cpp
└── zenoh_consumer_flatbuffers.cpp

config/
├── publisher.toml                   # Publisher configuration
└── schemas/                         # FlatBuffers schema definitions
    ├── benchmark.fbs                # FlatBuffers schema source
    └── benchmark.bfbs               # Compiled binary schema
```

---

## Internal Data Representation

The framework uses **FlatBuffers binary format** as the internal message representation. This provides:
- Zero-copy access to data
- Runtime reflection via `.bfbs` schema
- Efficient conversion to other formats

```
.fbs (schema source)
    │
    ▼ flatc --binary
.bfbs (binary schema)
    │
    ▼ runtime
┌─────────────────────────────────────────────┐
│  FlatBuffersSchema (reflection)             │
│  - Load and parse .bfbs                     │
│  - Provide reflection::Schema for runtime   │
└─────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────┐
│  Message (FlatBuffer binary + schema ref)   │
│  - Wraps raw FlatBuffer data                │
│  - Carries schema reference                 │
└─────────────────────────────────────────────┘
```

---

## Data Flow

```
Config (.toml)
    │
    ▼
FlatBuffersSchema ← .bfbs file
    │
    ▼
Generator (uses reflection to build FlatBuffer)
    │
    ▼
Message (FlatBuffer binary)
    │
    ▼
Serializer (converts to output format)
    │
    ├─ FlatBuffers → pass-through
    ├─ JSON → FlatBuffer-to-JSON conversion
    └─ Protobuf → FlatBuffer-to-Protobuf conversion (dynamic)
    │
    ▼
Binary Data
    │
    ▼
Publisher → IPC (iceoryx/zenoh)
```

---

## Interfaces

### rybench::common::Config

TOML configuration file parser.

```cpp
namespace rybench {
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

    const FlatBuffersSchema& schema() const;  // Get loaded schema
};

} // namespace common
} // namespace rybench
```

---

### rybench::common::FlatBuffersSchema

FlatBuffers binary schema loader for runtime reflection.

```cpp
namespace rybench {
namespace common {

class FlatBuffersSchema {
public:
    void loadFromBfbs(const std::string& bfbsPath);

    const reflection::Schema* reflectionSchema() const;
    const reflection::Object* rootTable() const;
    std::string rootTableName() const;
};

} // namespace common
} // namespace rybench
```

| Method | Description |
|--------|-------------|
| `loadFromBfbs(path)` | Load compiled .bfbs schema file |
| `reflectionSchema()` | Get FlatBuffers reflection schema |
| `rootTable()` | Get root table definition |
| `rootTableName()` | Get root table name |

---

### rybench::common::Message

Wrapper for FlatBuffer binary data with schema reference.

```cpp
namespace rybench {
namespace common {

class Message {
public:
    Message() = default;
    Message(std::vector<uint8_t> data, const reflection::Schema* schema = nullptr);

    const uint8_t* data() const;
    size_t size() const;
    bool empty() const;

    std::vector<uint8_t> buffer() const;
    const reflection::Schema* schema() const;
};

} // namespace common
} // namespace rybench
```

| Method | Description |
|--------|-------------|
| `data()` | Get pointer to raw FlatBuffer data |
| `size()` | Get data size in bytes |
| `empty()` | Check if message is empty |
| `buffer()` | Get copy of data as vector |
| `schema()` | Get associated reflection schema |

---

### rybench::generator::IGenerator

Interface for data generators.

```cpp
namespace rybench {
namespace generator {

enum class Algorithm { Fixed, SemiRandom, SchemaGuided, PropertyGuided };

class IGenerator {
public:
    virtual ~IGenerator() = default;

    virtual void loadSchema(const common::FlatBuffersSchema& schema) = 0;
    virtual common::Message generate() = 0;
    virtual Algorithm algorithm() const = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<IGenerator>;

GeneratorPtr createGenerator(const common::Config& config);

} // namespace generator
} // namespace rybench
```

| Method | Description |
|--------|-------------|
| `loadSchema(schema)` | Load FlatBuffers schema for generation |
| `generate()` | Generate next Message (FlatBuffer binary) |
| `algorithm()` | Get generator algorithm type |
| `name()` | Get generator name |

#### Implementations

| Class | Algorithm | Description |
|-------|-----------|-------------|
| `FixedGenerator` | Fixed | Replay from recorded data |
| `SchemaGuidedGenerator` | SchemaGuided | Random data using FlatBuffers reflection |
| `PropertyGuidedGenerator` | PropertyGuided | Custom property generators per field |

---

### rybench::publisher::IPublisher

Interface for IPC publishers.

```cpp
namespace rybench {
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

} // namespace publisher
} // namespace rybench
```

#### Implementations

| Class | Transport | Description |
|-------|-----------|-------------|
| `IceoryxPublisher` | iceoryx | Zero-copy shared memory IPC |
| `ZenohPublisher` | Zenoh | Network-capable pub/sub |

---

### rybench::serializer::ISerializer

Interface for output serialization. Converts internal FlatBuffer representation to output format.

```cpp
namespace rybench {
namespace serializer {

class ISerializer {
public:
    virtual ~ISerializer() = default;

    virtual std::vector<uint8_t> serialize(const common::Message& msg) = 0;
    virtual std::string name() const = 0;
};

using SerializerPtr = std::unique_ptr<ISerializer>;

SerializerPtr createSerializer(const common::Config& config);

} // namespace serializer
} // namespace rybench
```

| Method | Description |
|--------|-------------|
| `serialize(msg)` | Convert Message to output binary format |
| `name()` | Get serializer name |

#### Implementations

| Class | Format | Description |
|-------|--------|-------------|
| `FlatbuffersSerializer` | FlatBuffers | Pass-through (data already in FlatBuffers format) |
| `JsonSerializer` | JSON | Converts FlatBuffer to JSON using reflection |
| `ProtobufSerializer` | Protobuf | Converts FlatBuffer to Protobuf using dynamic messages |

##### FlatbuffersSerializer

Simple pass-through serializer - returns the FlatBuffer data as-is.

```cpp
class FlatbuffersSerializer : public ISerializer {
public:
    FlatbuffersSerializer() = default;

    std::vector<uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;
};
```

##### JsonSerializer

Converts FlatBuffer to JSON using reflection.

```cpp
class JsonSerializer : public ISerializer {
public:
    explicit JsonSerializer(const std::string& bfbsPath = "");

    std::vector<uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;
};
```

##### ProtobufSerializer

Converts FlatBuffer to Protobuf using dynamic message generation from .bfbs schema.
No pre-compiled .proto files or message registration required.

```cpp
class ProtobufSerializer : public ISerializer {
public:
    explicit ProtobufSerializer(const std::string& bfbsPath);

    std::vector<uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;
};
```

**Key feature:** Builds Protobuf `FileDescriptorProto` dynamically from FlatBuffers schema at runtime:
- Maps FlatBuffers tables → Protobuf messages
- Maps FlatBuffers fields → Protobuf fields
- Maps FlatBuffers enums → Protobuf enums
- Uses `DynamicMessageFactory` to create messages without compiled .proto files

---

## Configuration

### publisher.toml Example

```toml
[runtime]
app_name = "rybench"

[schema]
bfbs = "config/schemas/benchmark.bfbs"

[publisher]
type = "iceoryx"          # iceoryx or zenoh
service = "Benchmark"
instance = "Default"
event = "Data"
rate_hz = 100
max_payload_size = 65536

[serializer]
type = "flatbuffers"      # flatbuffers, json, or protobuf

[generator]
algorithm = "schema_guided"    # fixed, schema_guided, or property_guided
seed = 0

[dump]
enabled = false
output = "dump/benchmark.bin"
```

### Configuration Options

| Section | Key | Values | Description |
|---------|-----|--------|-------------|
| `[runtime]` | app_name | string | Application identifier |
| `[schema]` | bfbs | path | FlatBuffers binary schema file |
| `[publisher]` | type | iceoryx, zenoh | IPC transport |
| | service | string | Service name |
| | instance | string | Instance name |
| | event | string | Event name |
| | rate_hz | int | Publishing frequency |
| | max_payload_size | int | Maximum message size |
| `[serializer]` | type | flatbuffers, json, protobuf | Output format |
| `[generator]` | algorithm | fixed, schema_guided, property_guided | Generation strategy |
| | seed | int | Random seed (0 = random) |
| `[dump]` | enabled | bool | Enable recording |
| | output | path | Output file path |

---

## Schema Definition

Schemas are defined in FlatBuffers format (`.fbs`) and compiled to binary (`.bfbs`):

```bash
flatc --binary --schema benchmark.fbs
```

Example schema (`benchmark.fbs`):

```fbs
namespace benchmark;

enum SensorType : byte {
    UNKNOWN = 0,
    LIDAR = 1,
    CAMERA = 2,
    RADAR = 3
}

table Sensor {
    id: uint64;
    type: SensorType;
    name: string;
}

table Packet {
    timestamp: uint64;
    sequence: uint32;
    sensors: [Sensor];
}

root_type Packet;
```

The `.bfbs` file serves as the **single source of truth** for:
- FlatBuffers data generation (via reflection)
- FlatBuffers serialization (pass-through)
- JSON conversion (via reflection)
- Protobuf conversion (dynamic descriptor generation)
