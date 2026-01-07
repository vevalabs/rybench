add_library(publisher INTERFACE)

target_include_directories(publisher INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(publisher INTERFACE
    common
    iceoryx_posh::iceoryx_posh
)

# Zenoh support (optional) - requires zenohcxx with zenohc backend
if(zenohcxx_FOUND)
    target_link_libraries(publisher INTERFACE zenohcxx::zenohc)
    target_compile_definitions(publisher INTERFACE HAVE_ZENOH)
endif()
