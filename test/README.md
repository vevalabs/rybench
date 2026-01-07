# Test Consumers

Test consumers for verifying data transmission.

## Build

From project root:

```bash
cd build
make consumer_json              # Iceoryx JSON consumer
make consumer_protobuf          # Iceoryx Protobuf consumer
make consumer_flatbuffers       # Iceoryx FlatBuffers consumer
make zenoh_consumer_json        # Zenoh JSON consumer
make zenoh_consumer_protobuf    # Zenoh Protobuf consumer
make zenoh_consumer_flatbuffers # Zenoh FlatBuffers consumer
make                            # Build all
```

## Run with Iceoryx Publisher

Start RouDi first:

```bash
iox-roudi
```

Then run the appropriate consumer:

```bash
# For JSON serialized messages
./bin/consumer_json

# For Protobuf serialized messages
./bin/consumer_protobuf

# For FlatBuffers serialized messages
./bin/consumer_flatbuffers
```

## Run with Zenoh Publisher

No daemon required. Just run the appropriate consumer:

```bash
# For JSON serialized messages
./bin/zenoh_consumer_json

# For Protobuf serialized messages
./bin/zenoh_consumer_protobuf

# For FlatBuffers serialized messages
./bin/zenoh_consumer_flatbuffers
```

## Configuration

Make sure `config/publisher.ini` matches your consumer:

**Publisher type:**
- `type = iceoryx` → use `consumer_*` (requires RouDi)
- `type = zenoh` → use `zenoh_consumer_*`

**Serializer type:**
- `type = json` → use `*_json` consumer
- `type = protobuf` → use `*_protobuf` consumer
- `type = flatbuffers` → use `*_flatbuffers` consumer
