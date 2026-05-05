cmake_minimum_required(VERSION 3.24)

project(Flux
    VERSION 0.1.0
    DESCRIPTION "Flux programming language compiler"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLUX_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(flux_lexer INTERFACE)

target_include_directories(flux_lexer
    INTERFACE
        ${FLUX_INCLUDE_DIR})
