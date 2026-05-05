add_executable(flux_scanner_tests
    scanner_tests.cpp)

target_link_libraries(flux_scanner_tests
    PRIVATE
        flux_lexer)

add_test(NAME flux_scanner_tests COMMAND flux_scanner_tests)
