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

### Field Ranges Configuration

When using protobuf or flatbuffers serializer, field value ranges are automatically loaded from a `.fields` config file.

**Naming convention:** The `.fields` file is auto-derived from the proto message name:
- `osi::SensorViewMessage` → `config/protos/osi_sensorview.fields` (removes "Message" suffix)
- `osi::MovingObject` → `config/protos/osi_movingobject.fields`

**Format:**
```ini
# Field ranges for osi_sensorview.proto
# Use [MessageType] sections and field = min, max

[Timestamp]
seconds = 0, 1000000
nanos = 0, 999999999

[Vector3d]
x = -1000.0, 1000.0
y = -1000.0, 1000.0
z = -100.0, 100.0

[MovingObject]
id = 1, 10000

[SensorViewMessage]
sensor_id = 1, 100
sensor_name.length = 5, 20       # string length
moving_object.count = 1, 10      # repeated field count
```

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

Schemas are JSON files defining the message structure. Place in `config/schemas/`.

### Supported Field Types

| Type | Aliases | Constraints |
|------|---------|-------------|
| `int` | `integer` | `min`, `max` |
| `double` | `float`, `number` | `min`, `max` |
| `string` | - | `minLength`, `maxLength` |
| `bool` | `boolean` | - |
| `array` | - | `arraySize`, `items` |
| `object` | - | `fields` |

### Example Schema

```json
{
    "name": "BenchmarkMessage",
    "fields": [
        {
            "name": "timestamp",
            "type": "int",
            "min": 0,
            "max": 9999999999
        },
        {
            "name": "value",
            "type": "double",
            "min": 0.0,
            "max": 100.0
        },
        {
            "name": "label",
            "type": "string",
            "minLength": 5,
            "maxLength": 20
        },
        {
            "name": "active",
            "type": "bool"
        },
        {
            "name": "measurements",
            "type": "array",
            "arraySize": 5,
            "items": {
                "name": "measurement",
                "type": "double",
                "min": -100.0,
                "max": 100.0
            }
        }
    ]
}
```

## Configuration

Example `config/publisher.ini`:

```ini
[runtime]
app_name = publisher

[publisher]
type = iceoryx
service = Benchmark
instance = Instance
event = Event
max_payload_size = 4096
rate_hz = 10

[generator]
algorithm = schema_guided
schema = config/schemas/message.json
seed = 42

[serializer]
type = json
proto_message = osi::SensorViewMessage

[dump]
enabled = false
output = dump/benchmark.jsonl
```

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
