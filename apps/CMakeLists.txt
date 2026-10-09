add_executable(slursat slursat/slursat.cc)
target_link_libraries(slursat PRIVATE propan::propan propan_warnings)
