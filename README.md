# propan

Boolean Constraint Propagation Libary

## Building

Requires CMake 3.20, a C++20 compiler and libarchive (`libarchive-dev`).

```bash
cmake -S . -B build
cmake --build build
./build/apps/slursat <formula.cnf> [seed] [k]
```
