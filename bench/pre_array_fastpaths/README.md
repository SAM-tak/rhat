# Before inline array access and leaner push

This is the complete valid reference measured with static data stacks, before
array indexing was emitted as checked LLVM IR and push growth was simplified.
The compiler/runtime/executable fingerprints and raw samples remain unchanged.

- [15 workloads](workloads_results.md)
- [bintrees](../pre_array_fastpaths_bintrees/results.md)
- [Current R^ / C++ / Rust comparisons](../results.md)

These are separate measurement sessions, not a controlled before/after trial.
