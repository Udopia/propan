add_library(propan_doctest INTERFACE)
target_include_directories(propan_doctest SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")

include(headers.cmake)

# Unit tests
add_executable(propan_tests
    main.cc
    test_solver_types.cc
    test_stream_buffer.cc
    test_clause_arena.cc
    test_cnf_formula.cc
    test_wcnf_formula.cc
    test_trail.cc
    test_propagators.cc
    test_use_cases.cc
    test_solver.cc)
target_link_libraries(propan_tests PRIVATE propan::propan propan_warnings propan_doctest)
target_compile_definitions(propan_tests PRIVATE
    PROPAN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME propan_tests COMMAND propan_tests)

# End-to-end tests of applications
if(TARGET slursat)
    set(DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)

    foreach(k 1 2 -2)
        add_test(NAME slursat.sat.k${k} COMMAND slursat ${DATA}/sat_small.cnf -s 3 -k ${k})
        set_tests_properties(slursat.sat.k${k} PROPERTIES
            PASS_REGULAR_EXPRESSION "s SATISFIABLE"
            FAIL_REGULAR_EXPRESSION "ERROR;nan")

        add_test(NAME slursat.unsat.k${k} COMMAND slursat ${DATA}/unsat_units.cnf -s 3 -k ${k})
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

    foreach(k 1 2 -2)
        add_test(NAME slursat.occ.k${k} COMMAND slursat ${DATA}/sat_small.cnf -s 3 -k ${k} -p occ)
        set_tests_properties(slursat.occ.k${k} PROPERTIES
            PASS_REGULAR_EXPRESSION "s SATISFIABLE"
            FAIL_REGULAR_EXPRESSION "ERROR;nan")
    endforeach()

    add_test(NAME slursat.usage COMMAND slursat --help)
    set_tests_properties(slursat.usage PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

    add_test(NAME slursat.bad_option COMMAND slursat ${DATA}/sat_small.cnf -p bogus)
    set_tests_properties(slursat.bad_option PROPERTIES WILL_FAIL TRUE)

    # Formula solved by unit propagation alone: no decision levels for the statistics
    add_test(NAME slursat.units_only_stats COMMAND slursat ${DATA}/sat_units_only.cnf)
    set_tests_properties(slursat.units_only_stats PROPERTIES
        PASS_REGULAR_EXPRESSION "s SATISFIABLE"
        FAIL_REGULAR_EXPRESSION "nan;ERROR")

    include(integration.cmake)
endif()
