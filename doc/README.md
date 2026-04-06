# Runtime Benchmarking Framework

A C++17 runtime benchmarking framework for real-time inter-process communication benchmarks. Generates test data from FlatBuffers schemas and publishes via iceoryx2 or Zenoh using configurable serialization formats.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Binaries are output to `build/bin/`.

## Run

```bash
scripts/run
```

This launches `publisher@pub1` and `subscriber@sub1` using the default templates under `config/`.

To launch custom node sets:

```bash
scripts/run publisher@pub1 publisher@pub2 subscriber@sub1
```

Press `Ctrl+C` to stop all nodes cleanly.

## Configuration

Configuration is organized as a directory tree under `$RYBENCH_CONFIG`:

```
config/
├── publisher@/       # template for publisher instances
│   └── config.toml
└── subscriber@/      # template for subscriber instances
    └── config.toml
```

Node names follow the systemd instance template convention:

| Name | Resolves to |
|------|-------------|
| `publisher@pub1` | `config/publisher@pub1/config.toml` if it exists, otherwise `config/publisher@/config.toml` |
| `publisher@pub2` | same template, `%i` = `pub2` |
| `mynode` | `config/mynode/config.toml` (concrete, no template) |

Inside a template, `%i` expands to the instance name and `%%` to a literal `%`.

### config.toml fields

```toml
[runtime]
app_name  = "benchmark_publisher_%i"   # %i = instance name
transport = "iceoryx2"                  # iceoryx2 | zenoh | filewriter

[schema]
bfbs = "schemas/osi_sensorview.bfbs"   # FlatBuffers binary schema

[generator]
algorithm = "schema_guided"             # fixed | schema_guided | property_guided
seed      = 0                           # 0 = random, non-zero = reproducible
replay    = "dump/benchmark.out"        # used with algorithm=fixed

[serializer]
type = "protobuf"                       # json | protobuf | flatbuffers

[iceoryx2]
mode             = "publish"            # publish | consume
service          = "Benchmark"
instance         = "Default"
event            = "Data"
max_payload_size = 4096
rate_hz          = 10
output           = "dump/iceoryx2-%i-received.bin"   # consume mode only

[zenoh]
mode             = "consume"            # publish | consume
key_expr         = "Benchmark/Default/Data"
max_payload_size = 4096
rate_hz          = 10
output           = "dump/zenoh-%i-received.bin"      # consume mode only

[filewriter]
output  = "dump/benchmark-%i.out"
rate_hz = 10
count   = 0                             # 0 = unlimited
```

### Per-instance overrides

Place a concrete config at `config/<name>/config.toml` to override the template for that instance only. Any key present in the concrete config takes priority; missing keys fall back to the template.

## Transports

| Transport | Binary | Daemon required |
|-----------|--------|-----------------|
| `iceoryx2` | `rybench-iceoryx2` | None |
| `zenoh` | `rybench-zenoh` | None (local); `zenohd` for cross-machine routing |
| `filewriter` | `rybench-filewriter` | None |

## Serializers

| Value | Format |
|-------|--------|
| `json` | Human-readable JSON via FlatBuffers reflection |
| `protobuf` | Protocol Buffers, dynamically generated from `.bfbs` schema |
| `flatbuffers` | FlatBuffers pass-through |

## Generators

| Value | Description |
|-------|-------------|
| `schema_guided` | Random data within constraints defined in `.fbs` attributes |
| `property_guided` | Custom per-field generators (counter, timestamp, cycle) |
| `fixed` | Replay from a recorded binary file (`replay` path) |

### Schema constraints

Constraints are declared as attributes in the `.fbs` source and compiled to `.bfbs`:

```flatbuffers
attribute "min";        attribute "max";
attribute "min_length"; attribute "max_length";
attribute "min_items";  attribute "max_items";

table SensorViewMessage {
    sensor_id:     uint64 (min: "1",  max: "100");
    sensor_name:   string (min_length: "5", max_length: "20");
    moving_object: [MovingObject] (min_items: "1", max_items: "10");
}
```

Compile with:

```bash
flatc --binary --schema -o schemas/ schemas/osi_sensorview.fbs
```

## Binaries

| Binary | Description |
|--------|-------------|
| `rybench-launch` | Launches multiple node instances from the CLI |
| `rybench-iceoryx2` | Iceoryx2 publisher or consumer (zero-copy shared memory) |
| `rybench-zenoh` | Zenoh publisher or consumer |
| `rybench-filewriter` | Writes serialized messages to a binary file |

Each transport binary can also be invoked directly (`$RYBENCH_CONFIG` must be set):

```bash
export RYBENCH_CONFIG=$PWD/config
./build/bin/rybench-iceoryx2 publisher@pub1
```

## Dependencies

Install on Fedora:

```bash
sudo dnf install protobuf-devel protobuf-compiler flatbuffers-devel flatbuffers-compiler
# Optional for Zenoh:
sudo dnf install zenoh-cpp-devel
```

- GCC with C++17 support
- CMake >= 3.16
- iceoryx2 (built from source, see `CMakeLists.txt`)
- rapidjson (fetched automatically via CMake FetchContent)
- toml++ (fetched automatically via CMake FetchContent)
- protobuf
- flatbuffers
- zenoh-cpp (optional, auto-detected)

## Commit Convention

```
<type>(<scope>): <description>
```

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `refactor` | Code refactoring |
| `docs` | Documentation only |
| `test` | Adding or updating tests |
| `chore` | Build, config, or tooling changes |
| `perf` | Performance improvement |
