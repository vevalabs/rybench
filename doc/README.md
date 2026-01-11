# Runtime Benchmarking Framework

A C++17 runtime benchmarking framework using iceoryx for real-time inter-process communication via shared memory.

## Build

```bash
mkdir build && cd build
cmake ..
#make consumer_json       # JSON consumer only
#make consumer_protobuf   # Protobuf consumer only
make
```

## Run

Requires 3 terminals:

**Terminal 1 - RouDi (iceoryx daemon):**
```bash
iox-roudi
```

**Terminal 1 (2) - Zenoh (zenohd deamon):**
```bash
zenohd
```

**Terminal 2 - Publisher:**
```bash
./run/benchmark config/publisher.ini
```

**Terminal 3 - Consumer:**
```bash
./build/test/consumer
```

## Supported Publishers

Configure via `[publisher] type` in the INI file:

| Publisher | Config Value | Description |
|-----------|--------------|-------------|
| Iceoryx | `iceoryx` | Zero-copy shared memory IPC using Eclipse iceoryx (default) |
| Zenoh | `zenoh` | Pub/sub middleware with support for various transport protocols |

## Supported Serializers

Configure via `[serializer] type` in the INI file:

| Serializer | Config Value | Description |
|------------|--------------|-------------|
| JSON | `json` | Human-readable JSON format using nlohmann/json (default) |
| Protobuf | `protobuf` | Binary Protocol Buffers format, supports OSI schemas |
| FlatBuffers | `flatbuffers` | Zero-copy binary format for high performance |

### Protobuf Options

- `proto_message` - Message type to use (e.g., `osi::SensorViewMessage`)

### FlatBuffers Options

- `fbs_message` - Message type to use (e.g., `osi::SensorViewMessage`)

### Schema Configuration

All serializers use FlatBuffers binary schema (.bfbs) for field constraints. Configure the schema path in the `[schema]` section:

```ini
[schema]
bfbs = config/flatbuffers/osi_sensorview.bfbs
```

The constraints are defined as attributes in the `.fbs` source file and compiled to `.bfbs`:

```flatbuffers
attribute "min";
attribute "max";
attribute "min_length";
attribute "max_length";
attribute "min_items";
attribute "max_items";

table SensorViewMessage {
    sensor_id: uint64 (min: "1", max: "100");
    sensor_name: string (min_length: "5", max_length: "20");
    moving_object: [MovingObject] (min_items: "1", max_items: "10");
}
```

Compile the schema with: `flatc --binary --schema -o config/flatbuffers config/flatbuffers/osi_sensorview.fbs`

### Available Proto Messages

| Message | Description |
|---------|-------------|
| `osi::SensorViewMessage` | OSI sensor view with timestamp, objects, sensor info |
| `osi::MovingObject` | Single moving object with position, velocity, type |
| `osi::Timestamp` | Time representation (seconds + nanos) |
| `osi::Vector3d` | 3D vector (x, y, z) |

To add new proto messages:
1. Add `.proto` file to `config/protos/`
2. Update `CMakeLists.txt` to include the proto file
3. Register message in `protobuf_serializer.cpp` using `REGISTER_PROTO_MESSAGE()`

## Supported Algorithms

Configure via `[generator] algorithm` in the INI file:

| Algorithm | Config Value | Description |
|-----------|--------------|-------------|
| Fixed | `fixed` | Reads messages from a JSONL file and replays them sequentially. |
| Schema-Guided | `schema_guided` | Random data within schema constraints (default). |
| Property-Guided | `property_guided` | Custom generators for fields (counters, timestamps, cycles). |

### Algorithm Options

**fixed:**
- `replay` - Path to JSONL file containing messages to replay

**schema_guided:**
- `seed` - RNG seed (0 = true random, non-zero = reproducible)

## Schema Format

Schemas are FlatBuffers files (.fbs) defining the message structure. Place in `config/flatbuffers/`.

### Supported Constraint Attributes

| Attribute | Applies To | Description |
|-----------|------------|-------------|
| `min` | numeric types | Minimum value |
| `max` | numeric types | Maximum value |
| `min_length` | string | Minimum string length |
| `max_length` | string | Maximum string length |
| `min_items` | vector | Minimum array size |
| `max_items` | vector | Maximum array size |

### Example Schema

```flatbuffers
namespace benchmark;

attribute "min";
attribute "max";
attribute "min_length";
attribute "max_length";
attribute "min_items";
attribute "max_items";

table Measurement {
    value: double (min: "-100.0", max: "100.0");
}

table BenchmarkMessage {
    timestamp: uint64 (min: "0", max: "9999999999");
    value: double (min: "0.0", max: "100.0");
    label: string (min_length: "5", max_length: "20");
    active: bool;
    measurements: [Measurement] (min_items: "1", max_items: "5");
}

root_type BenchmarkMessage;
```

## Configuration

Example `config/publisher.ini`:

```ini
[runtime]
app_name = publisher

[schema]
bfbs = config/flatbuffers/benchmark.bfbs

[publisher]
type = iceoryx
service = Benchmark
instance = Instance
event = Event
max_payload_size = 4096
rate_hz = 10

[generator]
algorithm = schema_guided
seed = 42

[serializer]
type = json
proto_message = osi::SensorViewMessage

[dump]
enabled = false
output = dump/benchmark.bin
```

## Commit Convention

Follow this commit message format:

```
<type>(<scope>): <description>

[optional body]
```

### Types

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `refactor` | Code refactoring (no feature change) |
| `docs` | Documentation only |
| `test` | Adding or updating tests |
| `chore` | Build, config, or tooling changes |
| `perf` | Performance improvement |


---

## Dependencies

- GCC with C++17 support
- iceoryx (iceoryx_posh, iceoryx_hoofs)
- nlohmann/json
- protobuf
- flatbuffers
- zenoh-cpp (optional, for Zenoh publisher)


Install on Fedora:
```bash
sudo dnf install nlohmann-json-devel iceoryx-devel protobuf-devel protobuf-compiler flatbuffers-devel flatbuffers-compiler


curl -LO https://github.com/eclipse-zenoh/zenoh/releases/download/1.7.1/zenoh-1.7.1-x86_64-unknown-linux-gnu-standalone.zip
unzip zenoh-1.7.1-x86_64-unknown-linux-gnu-standalone.zip -d zenoh-1.7.1

# Add zenoh client lib

```
