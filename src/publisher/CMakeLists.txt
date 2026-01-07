add_library(publisher STATIC
    iceoryx/iceoryx_publisher.cpp
    publisher_factory.cpp
)

# Add zenoh sources conditionally
if(zenohcxx_FOUND)
    target_sources(publisher PRIVATE zenoh/zenoh_publisher.cpp)
endif()

target_include_directories(publisher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/iceoryx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/zenoh/include
)

target_link_libraries(publisher PUBLIC
    common
    iceoryx_posh::iceoryx_posh
)

if(zenohcxx_FOUND)
    target_link_libraries(publisher PUBLIC zenohcxx::zenohc)
    target_compile_definitions(publisher PUBLIC HAVE_ZENOH)
endif()
