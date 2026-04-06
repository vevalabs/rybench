# Architecture Documentation

## Code Structure

```
src/
├── rybench/                         # Shared library (librybench)
│   ├── common/
│   │   ├── include/
│   │   │   ├── config.hpp               # TOML config parser with node resolution
│   │   │   ├── node_config_path.hpp     # Template/concrete path resolution
│   │   │   ├── flatbuffers_schema.hpp   # FlatBuffers binary schema loader
│   │   │   ├── message.hpp              # Message wrapper (FlatBuffer data + schema)
│   │   │   ├── recorder.hpp             # JSONL data recording utility
│   │   │   └── signal_handler.hpp       # SIGINT/SIGTERM handler
│   │   ├── config.cpp
│   │   ├── flatbuffers_schema.cpp
│   │   ├── message.cpp
│   │   └── recorder.cpp
│   ├── generator/
│   │   ├── include/
│   │   │   ├── generator.hpp            # IGenerator interface
│   │   │   ├── generator_factory.hpp
│   │   │   └── reflection_builder.hpp
│   │   ├── fixed/                       # Replay from recorded file
│   │   ├── schema_guided/               # Random generation via FlatBuffers reflection
│   │   └── property_guided/             # Custom per-field generators
│   ├── serializer/
│   │   ├── include/
│   │   │   ├── serializer.hpp           # ISerializer interface
│   │   │   └── serializer_factory.hpp
│   │   ├── json/                        # FlatBuffer → JSON
│   │   ├── protobuf/                    # FlatBuffer → Protobuf (dynamic)
│   │   └── flatbuffers/                 # Pass-through
│   └── CMakeLists.txt
│
├── rybench-iceoryx2/                # iceoryx2 publisher + consumer binary
│   ├── iceoryx2_publisher.hpp/cpp
│   ├── iceoryx2_consumer.hpp/cpp
│   └── main.cpp
│
├── rybench-zenoh/                   # Zenoh publisher + consumer binary
│   ├── zenoh_publisher.hpp/cpp
│   ├── zenoh_consumer.hpp/cpp
│   └── main.cpp
│
├── rybench-filewriter/              # File writer binary
│   └── main.cpp
│
└── rybench-launch/                  # Multi-node orchestrator
    └── main.cpp

schemas/
├── osi_sensorview.fbs               # FlatBuffers schema source
└── osi_sensorview.bfbs              # Compiled binary schema (generated)

config/
├── publisher@/                      # Base template: publisher role
│   └── config.toml                  #   [runtime] [schema] [generator] [serializer]
├── subscriber@/                     # Base template: subscriber role
│   └── config.toml
├── iceoryx2:publisher@/             # Transport overlay template
│   └── config.toml                  #   [iceoryx2]
├── iceoryx2:subscriber@/
│   └── config.toml
├── zenoh:publisher@/
│   └── config.toml                  #   [serializer] [zenoh]
├── zenoh:subscriber@/
│   └── config.toml
├── filewriter:publisher@/
│   └── config.toml                  #   [filewriter]
└── <nodename>/                      # Concrete node config (overrides template)
    └── config.toml

scripts/
└── run                              # Launch helper (wraps rybench-launch)
```

---

## Node Configuration System

Configs follow the systemd unit template convention. The config root is set via `$RYBENCH_CONFIG`.

### Node Naming

| Node name | Meaning |
|-----------|---------|
| `iceoryx2:publisher@pub1` | transport=iceoryx2, role=publisher, instance=pub1 |
| `zenoh:subscriber@sub1` | transport=zenoh, role=subscriber, instance=sub1 |
| `filewriter:publisher@w1` | transport=filewriter, role=publisher, instance=w1 |

### Path Resolution

For a node named `foo@bar`:

1. Concrete: `$RYBENCH_CONFIG/foo@bar/config.toml` — checked first
2. Template: `$RYBENCH_CONFIG/foo@/config.toml` — fallback, `%i` expands to `bar`

For a node named `foo` (no instance):

1. Concrete: `$RYBENCH_CONFIG/foo/config.toml` — checked first
2. Template: `$RYBENCH_CONFIG/foo@/config.toml` — fallback, `%i` expands to `""`

### Base + Overlay Merge

For transport-prefixed nodes (`transport:role@instance`), `Config::loadNode` performs a two-layer merge:

```
$RYBENCH_CONFIG/role@/config.toml          ← base   (runtime, schema, generator, serializer)
      +
$RYBENCH_CONFIG/transport:role@/config.toml ← overlay (transport section, optional overrides)
      ↓
merged config (overlay sections replace base sections)
```

Example for `zenoh:publisher@pub1`:
- Base: `publisher@/config.toml` provides `[runtime]`, `[schema]`, `[generator]`, `[serializer]`
- Overlay: `zenoh:publisher@/config.toml` overrides `[serializer]` and adds `[zenoh]`

### `%i` Expansion

Inside any config value, `%i` is replaced with the instance name at load time:

```toml
app_name = "benchmark_publisher_%i"   # → "benchmark_publisher_pub1"
output   = "dump/iceoryx2-%i.bin"     # → "dump/iceoryx2-pub1.bin"
```

---

## Binary Entry Points

Each transport is a separate binary. `rybench-launch` is the orchestrator.

### `rybench-launch`

Reads node names from argv, resolves each config, infers the transport binary from whichever section (`[iceoryx2]`, `[zenoh]`, `[filewriter]`) is present, then forks and execs the appropriate binary. Forwards SIGINT/SIGTERM to all children.

```bash
RYBENCH_CONFIG=./config rybench-launch \
  iceoryx2:publisher@pub1 \
  iceoryx2:subscriber@sub1 \
  iceoryx2:subscriber@sub2
```

### `rybench-iceoryx2`

Publishes or consumes over iceoryx2 shared memory. Mode is set by `[iceoryx2].mode = publish|consume`.

### `rybench-zenoh`

Publishes or consumes over Zenoh. Mode is set by `[zenoh].mode = publish|consume`.

### `rybench-filewriter`

Generates and serializes messages, writing them to a binary file (`uint32 size` + payload per message). Useful for producing replay input for `algorithm = fixed`.

---

## Data Flow

```
$RYBENCH_CONFIG  +  node name
        │
        ▼
Config::loadNode()
  ├── resolveNodeConfigPath()   concrete → template fallback
  ├── base + overlay merge      (for transport:role nodes)
  └── %i expansion              in all string values
        │
        ▼
FlatBuffersSchema ← .bfbs file
        │
        ├──► Generator::loadSchema()
        │         │
        │         ▼
        │    Generator::generate() → Message (FlatBuffer binary)
        │                                │
        │                                ▼
        └──► Serializer::serialize(msg) → std::vector<uint8_t>
                      │
              ┌───────┼───────┐
              ▼       ▼       ▼
           json   protobuf  flatbuffers (pass-through)
                                │
                                ▼
                     Publisher / FileWriter
                  (iceoryx2 / zenoh / file)
```

---

## Configuration Reference

### Common sections (in `publisher@/` and `subscriber@/` base templates)

| Section | Key | Values | Description |
|---------|-----|--------|-------------|
| `[runtime]` | `app_name` | string | App identifier; `%i` expands to instance name |
| `[schema]` | `bfbs` | path | FlatBuffers binary schema (.bfbs) |
| `[generator]` | `algorithm` | `fixed`, `schema_guided`, `property_guided` | Generation strategy |
| | `seed` | int | Random seed (0 = random) |
| | `replay` | path | Input file for `algorithm = fixed` |
| `[serializer]` | `type` | `json`, `protobuf`, `flatbuffers` | Serialization format |

### Transport sections (in transport overlay templates)

| Section | Key | Description |
|---------|-----|-------------|
| `[iceoryx2]` | `mode` | `publish` or `consume` |
| | `service`, `instance`, `event` | Service discovery triple |
| | `max_payload_size` | Max message size in bytes |
| | `rate_hz` | Publish/consume rate |
| | `output` | Output file path for consume mode |
| `[zenoh]` | `mode` | `publish` or `consume` |
| | `key_expr` | Zenoh key expression |
| | `max_payload_size`, `rate_hz`, `output` | Same as above |
| `[filewriter]` | `output` | Output file path |
| | `rate_hz` | Write rate |
| | `count` | Number of messages (0 = unlimited) |

---

## Key Interfaces

### `IGenerator`

```cpp
class IGenerator {
public:
    virtual void loadSchema(const common::FlatBuffersSchema& schema) = 0;
    virtual common::Message generate() = 0;
    virtual std::string name() const = 0;
};
GeneratorPtr createGenerator(const common::Config& config);
```

| Implementation | Algorithm | Description |
|----------------|-----------|-------------|
| `FixedGenerator` | `fixed` | Replay from recorded binary file |
| `SchemaGuidedGenerator` | `schema_guided` | Random data via FlatBuffers reflection |
| `PropertyGuidedGenerator` | `property_guided` | Custom per-field generators (counter, timestamp, cycle) |

### `ISerializer`

```cpp
class ISerializer {
public:
    virtual std::vector<uint8_t> serialize(const common::Message& msg) = 0;
    virtual std::string name() const = 0;
};
SerializerPtr createSerializer(const common::Config& config);
```

| Implementation | Format | Notes |
|----------------|--------|-------|
| `FlatbuffersSerializer` | FlatBuffers | Pass-through |
| `JsonSerializer` | JSON | Via FlatBuffers reflection |
| `ProtobufSerializer` | Protobuf | Dynamic descriptor from `.bfbs`, no `.proto` files needed |

### `Config`

```cpp
class Config {
public:
    void loadNode(const std::string& configRoot, const std::string& nodeName);

    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue = "") const;
    int    getInt   (const std::string& section, const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    bool   getBool  (const std::string& section, const std::string& key, bool defaultValue = false) const;

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

    const FlatBuffersSchema& schema() const;
    const std::string& instanceName() const;   // part after '@' in node name
};
```

---

## Schema

Schemas are defined in FlatBuffers format (`.fbs`) and compiled to binary (`.bfbs`) by CMake at build time:

```bash
flatc --binary --schema -o build/ schemas/osi_sensorview.fbs
```

The `.bfbs` file is the single source of truth used at runtime for:
- FlatBuffers data generation (reflection-based field population)
- JSON conversion (FlatBuffers reflection)
- Protobuf conversion (dynamic `FileDescriptorProto` built from schema)
