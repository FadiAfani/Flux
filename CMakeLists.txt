cmake_minimum_required(VERSION 3.24)

project(Flux
    VERSION 0.1.0
    DESCRIPTION "Flux programming language compiler"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLUX_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(flux_lexer
    src/lexer/scanner.cpp)

target_include_directories(flux_lexer
    PUBLIC
        ${FLUX_INCLUDE_DIR})

add_library(flux_parser
    src/parser/parser.cpp
    src/parser/expression_parser.cpp
    src/parser/specification_parser.cpp)

target_include_directories(flux_parser
    PUBLIC
        ${FLUX_INCLUDE_DIR})

target_link_libraries(flux_parser
    PUBLIC
        flux_lexer)

add_library(flux_sema
    src/sema/module.cpp
    src/sema/name_resolver.cpp
    src/sema/symbol.cpp)

target_include_directories(flux_sema
    PUBLIC
        ${FLUX_INCLUDE_DIR})

target_link_libraries(flux_sema
    PUBLIC
        flux_parser)

enable_testing()
add_subdirectory(tests)
