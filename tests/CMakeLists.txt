# GoogleTest-based tests wired into CTest.

find_program(FLATC flatc REQUIRED)

# Compile the role-attribute fixture schema to a .bfbs the test loads at runtime.
set(ROLES_FBS  ${CMAKE_CURRENT_SOURCE_DIR}/schema_guided_random_generator/fixtures/roles.fbs)
set(ROLES_BFBS ${CMAKE_CURRENT_BINARY_DIR}/roles.bfbs)
add_custom_command(
    OUTPUT  ${ROLES_BFBS}
    COMMAND ${FLATC} --binary --schema -o ${CMAKE_CURRENT_BINARY_DIR} ${ROLES_FBS}
    DEPENDS ${ROLES_FBS}
    COMMENT "Generating roles.bfbs test fixture"
)
add_custom_target(roles_bfbs DEPENDS ${ROLES_BFBS})

add_executable(schema_guided_random_generator_test
    schema_guided_random_generator/schema_guided_roles_test.cpp
)
add_dependencies(schema_guided_random_generator_test roles_bfbs)
target_include_directories(schema_guided_random_generator_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/flatbuffers   # flatbuffers_schema/serializer.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/generator/schema_guided  # schema_guided_random_generator.hpp
)
target_link_libraries(schema_guided_random_generator_test PRIVATE
    rybench
    GTest::gtest_main
    flatbuffers
)
target_compile_definitions(schema_guided_random_generator_test PRIVATE
    ROLES_TEST_BFBS="${ROLES_BFBS}")

include(GoogleTest)
gtest_discover_tests(schema_guided_random_generator_test)

# Direct Protobuf builder tests. Compares direct-build output against the
# flat-then-convert path and checks the bytes parse back to a DynamicMessage.
set(BUILDER_FBS  ${CMAKE_CURRENT_SOURCE_DIR}/protobuf_direct_builder/fixtures/builder.fbs)
set(BUILDER_BFBS ${CMAKE_CURRENT_BINARY_DIR}/builder.bfbs)
add_custom_command(
    OUTPUT  ${BUILDER_BFBS}
    COMMAND ${FLATC} --binary --schema -o ${CMAKE_CURRENT_BINARY_DIR} ${BUILDER_FBS}
    DEPENDS ${BUILDER_FBS}
    COMMENT "Generating builder.bfbs test fixture"
)
add_custom_target(builder_bfbs DEPENDS ${BUILDER_BFBS})

# Also compile the real OSI HostVehicleData schema (deep nesting, enums, an enum
# vector and a table vector) so the builder is exercised on a perf-table schema.
set(HVD_FBS  ${CMAKE_SOURCE_DIR}/schemas/osi_hostvehicledata.fbs)
set(HVD_BFBS ${CMAKE_CURRENT_BINARY_DIR}/osi_hostvehicledata.bfbs)
add_custom_command(
    OUTPUT  ${HVD_BFBS}
    COMMAND ${FLATC} --binary --schema -o ${CMAKE_CURRENT_BINARY_DIR} ${HVD_FBS}
    DEPENDS ${HVD_FBS}
    COMMENT "Generating osi_hostvehicledata.bfbs test fixture"
)
add_custom_target(hvd_bfbs DEPENDS ${HVD_BFBS})

add_executable(protobuf_direct_builder_test
    protobuf_direct_builder/protobuf_direct_builder_test.cpp
)
add_dependencies(protobuf_direct_builder_test builder_bfbs hvd_bfbs)
target_include_directories(protobuf_direct_builder_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/flatbuffers   # flatbuffers_schema/serializer.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/protobuf      # protobuf_serializer.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/generator/schema_guided  # schema_guided_random_generator.hpp
)
target_link_libraries(protobuf_direct_builder_test PRIVATE
    rybench
    GTest::gtest_main
    flatbuffers
)
target_compile_definitions(protobuf_direct_builder_test PRIVATE
    BUILDER_TEST_BFBS="${BUILDER_BFBS}"
    HVD_TEST_BFBS="${HVD_BFBS}")
gtest_discover_tests(protobuf_direct_builder_test)

# MTL parser/preprocessor unit tests. Compiles the parser + tree sources directly
# (no librybench link) since they are self-contained and have no FlatBuffers deps.
add_executable(test_mtl_parser
    property_guided/mtl_parser_test.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/generator/property_guided/mtl_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/generator/property_guided/mtl_tree.cpp
)
target_include_directories(test_mtl_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/generator/property_guided
)
target_link_libraries(test_mtl_parser PRIVATE GTest::gtest_main)
gtest_discover_tests(test_mtl_parser)
