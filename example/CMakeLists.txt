# Thesis example: call the property_guided MTL generator directly from librybench.
add_executable(mtl_one_var_example mtl_one_var_example.cpp)

# librybench's PUBLIC includes give the public headers (core/, serializer/); add
# src/ so we can reach the private generator header directly.
target_include_directories(mtl_one_var_example PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mtl_one_var_example PRIVATE rybench)
