add_executable(slursat slursat/slursat.cc)
target_link_libraries(slursat PRIVATE propan::propan propan_warnings)

add_executable(probe probe/probe.cc)
target_link_libraries(probe PRIVATE propan::propan propan_warnings)
