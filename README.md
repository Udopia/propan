# propan

Boolean Constraint Propagation Libary

## Building

Requires CMake 3.20, a C++20 compiler and libarchive (`libarchive-dev`).

```sh
cmake -S . -B build
cmake --build build
./build/apps/slursat <formula.cnf> [seed] [k]
```

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest) (fetched by CMake); applications are tested end-to-end via CTest.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
