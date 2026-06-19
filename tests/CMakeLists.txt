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
