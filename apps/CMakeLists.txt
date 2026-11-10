# Helpers shared by the applications.
add_library(propan_apps_common INTERFACE)
target_include_directories(propan_apps_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(propan_apps_common SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(propan_apps_common INTERFACE propan::propan propan_warnings)

# Solvers: decision procedures built on the library.
if(PROPAN_BUILD_SOLVERS)
    add_subdirectory(solvers)
endif()

# Test and benchmark drivers.
if(PROPAN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
