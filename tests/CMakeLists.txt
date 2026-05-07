add_executable(flux_scanner_tests
    scanner_tests.cpp)

target_link_libraries(flux_scanner_tests
    PRIVATE
        flux_lexer)

add_test(NAME flux_scanner_tests COMMAND flux_scanner_tests)

add_executable(flux_allocator_tests
    allocator_tests.cpp)

target_include_directories(flux_allocator_tests
    PRIVATE
        ${FLUX_INCLUDE_DIR})

add_test(NAME flux_allocator_tests COMMAND flux_allocator_tests)
set_tests_properties(flux_allocator_tests PROPERTIES TIMEOUT 2)
