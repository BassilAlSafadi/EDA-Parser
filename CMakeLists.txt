cmake_minimum_required(VERSION 3.15)
project(vsim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_library(vsimcore
    src/arena.cpp
    src/diag.cpp
    src/lexer.cpp
    src/parser.cpp
    src/dump.cpp
    src/elab.cpp
    src/gate_eval.cpp
    src/gate_wire.cpp
    src/sig.cpp
)
target_include_directories(vsimcore PUBLIC src)

add_executable(vsim src/main.cpp)
target_link_libraries(vsim PRIVATE vsimcore)

enable_testing()
add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE vsimcore)
add_test(NAME test_lexer COMMAND test_lexer)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE vsimcore)
add_test(NAME test_parser COMMAND test_parser)

add_executable(test_elab tests/test_elab.cpp)
target_link_libraries(test_elab PRIVATE vsimcore)
add_test(NAME test_elab COMMAND test_elab)

add_executable(test_gate_eval tests/test_gate_eval.cpp)
target_link_libraries(test_gate_eval PRIVATE vsimcore)
add_test(NAME test_gate_eval COMMAND test_gate_eval)

add_executable(test_gate_wire tests/test_gate_wire.cpp)
target_link_libraries(test_gate_wire PRIVATE vsimcore)
add_test(NAME test_gate_wire COMMAND test_gate_wire WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
