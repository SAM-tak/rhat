# Invalid historical measurements

These are verbatim pre-fix snapshots. Do not use the affected R^ rows as performance evidence.
Rust overwrote R^ executables with the same basename. Checksums could not detect this because the workloads intentionally produce identical output.
The corrected harness uses language-specific filenames, rejects duplicate paths, and checks build-time executable SHA-256 before and after every run.

| Original dataset | Invalid R^ workloads | Original raw JSON SHA-256 |
|---|---|---|
| bench/workloads_results.json | particles, sum | `afeee75f724c0f97d3e57abd40534bf665cf98e0bbfd08787b5fe984f3038c5a` |
| bench/push/workloads_results.json | push | `a7842f5e800716d8e3eef56b1312b6b009ae198214089846d8a769dd4904de29` |
| bench/variants_parsers/workloads_results.json | records | `db46070150f0007967a4846566e1728fe917c47ce6c341a684dc977909176c6f` |

Verbatim snapshots: [numeric JSON](numeric.json), [push JSON](push.json),
[partial variant/parser JSON](variants_parsers_partial.json). Their old Markdown
reports are retained alongside them. The current reference is the complete
[corrected suite](../results.md); current numeric/push/variant matrices have been
replaced with verified subsets of that full session.
