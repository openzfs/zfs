
## The testings are done this way

```mermaid
flowchart TB
subgraph CleanUp and Summary
  CleanUp+Summary
end

subgraph Functional Testings
  sanity-checks-20.04
  zloop-checks-20.04
  functional-testing-20.04-->Part1-20.04
  functional-testing-20.04-->Part2-20.04
  functional-testing-20.04-->Part3-20.04
  functional-testing-20.04-->Part4-20.04
  functional-testing-22.04-->Part1-22.04
  functional-testing-22.04-->Part2-22.04
  functional-testing-22.04-->Part3-22.04
  functional-testing-22.04-->Part4-22.04
  sanity-checks-22.04
  zloop-checks-22.04
end

subgraph Code Checking + Building
  Build-Ubuntu-20.04
  codeql.yml
  checkstyle.yml
  Build-Ubuntu-22.04
end

  Build-Ubuntu-20.04-->sanity-checks-20.04
  Build-Ubuntu-20.04-->zloop-checks-20.04
  Build-Ubuntu-20.04-->functional-testing-20.04
  Build-Ubuntu-22.04-->sanity-checks-22.04
  Build-Ubuntu-22.04-->zloop-checks-22.04
  Build-Ubuntu-22.04-->functional-testing-22.04

  sanity-checks-20.04-->CleanUp+Summary
  Part1-20.04-->CleanUp+Summary
  Part2-20.04-->CleanUp+Summary
  Part3-20.04-->CleanUp+Summary
  Part4-20.04-->CleanUp+Summary
  Part1-22.04-->CleanUp+Summary
  Part2-22.04-->CleanUp+Summary
  Part3-22.04-->CleanUp+Summary
  Part4-22.04-->CleanUp+Summary
  sanity-checks-22.04-->CleanUp+Summary
```


1) build zfs modules for Ubuntu 20.04 and 22.04 (~15m)
2) 2x zloop test (~10m) + 2x sanity test (~25m)
3) 4x functional testings in parts 1..4 (each ~1h)
4) cleanup and create summary
   - content of summary depends on the results of the steps

When everything runs fine, the full run should be done in
about 2 hours.

The codeql.yml and checkstyle.yml are not part in this circle.
