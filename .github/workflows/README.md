
## The testings are done this way

```mermaid
flowchart TB
subgraph CleanUp and Summary
  Part1-20.04-->CleanUp+nice+Summary
  Part2-20.04-->CleanUp+nice+Summary
  PartN-20.04-->CleanUp+nice+Summary
  Part1-22.04-->CleanUp+nice+Summary
  Part2-22.04-->CleanUp+nice+Summary
  PartN-22.04-->CleanUp+nice+Summary
end

subgraph Functional Testings
  functional-testing-20.04-->Part1-20.04
  functional-testing-20.04-->Part2-20.04
  functional-testing-20.04-->PartN-20.04
  functional-testing-22.04-->Part1-22.04
  functional-testing-22.04-->Part2-22.04
  functional-testing-22.04-->PartN-22.04
end

subgraph Sanity and zloop Testings
  sanity-checks-20.04-->functional-testing-20.04
  sanity-checks-22.04-->functional-testing-22.04
  zloop-checks-20.04-->functional
  zloop-checks-22.04-->functional
end

subgraph Code Checking + Building
  codeql.yml
  checkstyle.yml
  Build-Ubuntu-20.04-->sanity-checks-20.04
  Build-Ubuntu-22.04-->sanity-checks-22.04
  Build-Ubuntu-20.04-->zloop-checks-20.04
  Build-Ubuntu-22.04-->zloop-checks-22.04
end
```


1) build zfs modules for Ubuntu 20.04 and 22.04 (~15m)
2) 2x zloop test (~10m) + 2x sanity test (~25m)
3) functional testings in parts 1..5 (each ~1h)
4) cleanup and create summary
   - content of summary depends on the results of the steps

When everything runs fine, the full run should be done in
about 2 hours.

The codeql.yml and checkstyle.yml are not part in this circle.
