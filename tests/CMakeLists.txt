include(FetchContent)
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(doctest)
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# Unit tests
add_executable(propan_tests
    main.cc
    test_solver_types.cc
    test_stream_buffer.cc
    test_cnf_formula.cc
    test_wcnf_formula.cc
    test_trail.cc)
target_link_libraries(propan_tests PRIVATE propan::propan propan_warnings doctest::doctest)
target_compile_definitions(propan_tests PRIVATE
    PROPAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
doctest_discover_tests(propan_tests)

# End-to-end tests of applications
if(PROPAN_BUILD_APPS)
    set(DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)

    foreach(k 1 2 -2)
        add_test(NAME slursat.sat.k${k} COMMAND slursat ${DATA}/sat_small.cnf 3 ${k})
        set_tests_properties(slursat.sat.k${k} PROPERTIES
            PASS_REGULAR_EXPRESSION "s SATISFIABLE"
            FAIL_REGULAR_EXPRESSION "ERROR;nan")

        add_test(NAME slursat.unsat.k${k} COMMAND slursat ${DATA}/unsat_units.cnf 3 ${k})
        set_tests_properties(slursat.unsat.k${k} PROPERTIES
            PASS_REGULAR_EXPRESSION "s UNSATISFIABLE")
    endforeach()

    add_test(NAME slursat.gzip COMMAND slursat ${DATA}/sat_small.cnf.gz)
    set_tests_properties(slursat.gzip PROPERTIES PASS_REGULAR_EXPRESSION "s SATISFIABLE")

    add_test(NAME slursat.wcnf COMMAND slursat ${DATA}/sample_old.wcnf)
    set_tests_properties(slursat.wcnf PROPERTIES PASS_REGULAR_EXPRESSION "s SATISFIABLE")

    # Pigeonhole PHP(3,2): unsatisfiable, but not refutable by SLUR
    add_test(NAME slursat.gives_up COMMAND slursat ${DATA}/php_3_2.cnf)
    set_tests_properties(slursat.gives_up PROPERTIES PASS_REGULAR_EXPRESSION "s UNKNOWN")

    add_test(NAME slursat.usage COMMAND slursat)
    set_tests_properties(slursat.usage PROPERTIES PASS_REGULAR_EXPRESSION "USAGE")

    # Formula solved by unit propagation alone: no decision levels for the statistics
    add_test(NAME slursat.units_only_stats COMMAND slursat ${DATA}/sat_units_only.cnf)
    set_tests_properties(slursat.units_only_stats PROPERTIES
        PASS_REGULAR_EXPRESSION "s SATISFIABLE"
        FAIL_REGULAR_EXPRESSION "nan;ERROR")
endif()
