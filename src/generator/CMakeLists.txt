add_library(generator STATIC
    fixed/fixed_generator.cpp
    schema_guided/schema_guided_generator.cpp
    property_guided/property_guided_generator.cpp
    generator_factory.cpp
    reflection_builder.cpp
)

target_include_directories(generator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fixed/include
    ${CMAKE_CURRENT_SOURCE_DIR}/schema_guided/include
    ${CMAKE_CURRENT_SOURCE_DIR}/property_guided/include
)

target_link_libraries(generator PUBLIC
    common
)
