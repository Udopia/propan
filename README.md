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

Unit tests use [doctest](https://github.com/doctest/doctest) 2.4.12, vendored in
[`third_party/doctest`](third_party/doctest) with its license. No download is needed.
CTest runs unit tests as one suite and applications end-to-end.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
