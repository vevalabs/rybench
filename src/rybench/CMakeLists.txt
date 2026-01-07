add_executable(rybench
    main.cpp
)

# Use whole-archive to ensure static initializers for message registration are included
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_libraries(rybench PRIVATE
        common
        -Wl,--whole-archive
        serializer
        -Wl,--no-whole-archive
        generator
        publisher
        iceoryx_posh::iceoryx_posh
        iceoryx_hoofs::iceoryx_hoofs
    )
else()
    target_link_libraries(rybench PRIVATE
        common
        serializer
        generator
        publisher
        iceoryx_posh::iceoryx_posh
        iceoryx_hoofs::iceoryx_hoofs
    )
endif()
