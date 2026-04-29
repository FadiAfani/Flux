# Flux

Flux is an experimental programming language compiler scaffold built in C++ with LLVM as the backend.

## Architecture

The project is organized around a staged compilation pipeline:

1. Frontend work produces a high-level intermediate representation (HIR).
2. HIR is lowered into a machine-aware middle intermediate representation (MIR).
3. MIR is translated into LLVM IR for optimization and native code generation.

## Layout

- `include/flux/driver`: compiler entry points and pipeline coordination
- `include/flux/hir`: HIR data structures
- `include/flux/mir`: MIR data structures
- `include/flux/lowering`: lowering passes between IR layers
- `include/flux/codegen`: LLVM backend integration
- `src`: implementation files
- `docs`: notes on pipeline responsibilities and roadmap

## Build

Flux uses CMake and expects an installed LLVM with CMake package files available.

```bash
cmake -S . -B build -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
cmake --build build
```

## Current status

This repository currently provides the compiler skeleton, IR module stubs, and a simple `fluxc` executable that demonstrates the planned pipeline.
