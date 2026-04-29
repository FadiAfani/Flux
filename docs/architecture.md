# Flux Compiler Architecture

## Pipeline

`source -> parser -> semantic analysis -> HIR -> MIR -> LLVM IR -> object code`

## HIR

HIR is intended to retain source-level meaning that is useful for type checking, name resolution, borrow or ownership rules, and other semantic analysis.

Good HIR candidates:

- resolved names
- explicit types after inference
- structured control flow before flattening
- language-level constructs such as pattern matching or generics

## MIR

MIR is intended to simplify language constructs into a more explicit, lower-level form that is easier to analyze and lower into LLVM IR.

Good MIR candidates:

- explicit basic blocks
- canonicalized control flow
- simplified data movement
- lowered calls and temporaries

## Near-term roadmap

1. Add lexer and parser.
2. Define typed HIR node families.
3. Introduce pass management and diagnostics.
4. Design a control-flow based MIR.
5. Lower MIR into LLVM IR.
