# Unit tests

> [!NOTE]
>
> This document is a draft. It will be updated as we gain experience writing
> and running unit tests.

This directory contains a unit testing framework for OpenZFS, and a collection
of unit tests.

## Building and running

The unit tests are built by default as part of the regular userspace build, so
you probably don’t have to do anything else.

The easiest way to run the tests is to run `make unit`, which will run all the
available tests.

```
$ make unit
  UNITTEST tests/unit/test_zap
Running test suite with seed 0x9d36890b...
zap.mock_microzap_sanity             [ OK    ] [ 0.00001088 / 0.00000939 CPU ]
zap.mock_fatzap_sanity               [ OK    ] [ 0.00004281 / 0.00004257 CPU ]
zap.zap_basic
  type=micro                         [ OK    ] [ 0.00001899 / 0.00001893 CPU ]
  type=fat                           [ OK    ] [ 0.00004174 / 0.00004135 CPU ]
4 of 4 (100%) tests successful, 0 (0%) test skipped.
```

Running a single test binary is possible with the `T=` param to `make unit`.

```
$ make unit T=zap
  UNITTEST tests/unit/test_zap
  ...
```

The test binaries are just normal programs in `./tests/unit`, and can be run
directly. This is useful for debugging with `gdb`.

```
$ ./tests/unit/test_zap
Running test suite with seed 0x18e131ac...
...
```

The test framework provides various options for controlling how the tests are
run. Add the `--help` switch for more info. If using the make rule, options can
be passed via the `TOPT=` param.

### Building just for tests

Recommended “minimum” build for just the unit tests, with additional debug to
assist with understanding issues.

```
./configure \
	--with-config=user \
	--enable-debug --enable-debuginfo \
	--disable-sysvinit --disable-systemd --disable-pam --disable-pyzfs
make -j$(nproc)
```

TODO: add `--with-config=unit` that disables _everything_ not needed for the tests

### Generating a coverage report

If `configure` was run with `--enable-code-coverage`, then an additional
`unit-coverage` target is available, which will run the requested tests, then
run `lcov` and `genhtml` to produce a HTML coverage report:

```
$ make unit-coverage T=zap
  UNITTEST tests/unit/test_zap
Running test suite with seed 0xe461208d...
zap.mock_microzap_sanity             [ OK    ] [ 0.00000933 / 0.00000773 CPU ]
zap.mock_fatzap_sanity               [ OK    ] [ 0.00004685 / 0.00004612 CPU ]
zap.zap_basic
  type=micro                         [ OK    ] [ 0.00002579 / 0.00002484 CPU ]
  type=fat                           [ OK    ] [ 0.00004093 / 0.00004038 CPU ]
4 of 4 (100%) tests successful, 0 (0%) test skipped.
lcov: WARNING: (inconsistent) /home/robn/code/zfs-unit/module/zfs/u8_textprep.c:1104: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "lcov --ignore-errors inconsistent,inconsistent ..." to suppress this warning)
Message summary:
  1 warning message:
    inconsistent: 1
Overall coverage rate:
  source files: 6
  lines.......: 42.3% (1270 of 3002 lines)
  functions...: 42.0% (76 of 181 functions)
Message summary:
  no messages were reported
coverage results: file://tests/unit/test_zap_coverage/index.html
```

TODO: improve the overall structure to make this less noisy.

## Guidance for test writers

### Top five

* Only bring in the source files under test.
* Use mocks to create the test scenario, then interrogate them to understand the result.
* Prefer more smaller tests over fewer bigger ones.
* Use coverage reports to guide test development.
* Do the simplest possible thing.

### Test structure

Tests should be as simple and as readable as possible. When a test fails, we
want to avoid the possibility that it could be the test itself at fault rather
than the system under test.

* Aim for one source file per subsystem or source concept (eg ZAP).
* Aim for one test function per API call or logical behaviour
  * Each “version” or “mode” of an API call or behaviour is a separate test
  * Don’t test more than one thing in the same test; a test shouldn’t rely on
    state or results from an earlier test
* Use test parameters for “class“ or ”vtable” -type APIs, where each
  implementation should respond to API calls the same way

### Build system

The build setup `tests/unit/Makefile.am` is very similar to the other
userspace, however it has a couple of differences to make the run and coverage
targets work more smoothly.

* Name the test program `test_foo`. Almost always, you will have one source
  file with the actual tests in it, called `test_foo.c`.
* Add the program to `UNIT_TESTS`. `noinst_PROGRAMS` will be populated from it,
  but this gives a specific name the run and coverage targets can use to
  resolve the `T=` parameter to a specific test.
* List the source files under test in `nodist_%C%_test_foo_SOURCES`, and the
  source files for the test itself in `%C%_test_foo_SOURCES`. This is
  important, as the coverage targets use `nodist_%C%_ ... _SOURCES` as the list
  of objects to include in the coverage output.

### Mocks

A “mock” struct is a fake version of some data structure that the subsystem
under test will accept and use as though it was a real one.

* Make mock structs opaque. All uses from the test suite should be through
  specific named accessor functions.
* Name a mock struct for the struct it is mimicking, prefixed with `mock_`. eg
  `mock_dnode_t` is the mock for `dnode_t`.
* Access functions should be named for the struct, eg the function to create a
  `mock_dnode_t` is `mock_dnode_t *mock_dnode_create(...)`.
* `mock_*` functions should always use the mock type name in its signature,
  never the original.
* The mock object should always be directly castable to its real type and
  vice-versa, ie a `mock_dnode_t *`   is always usable wherever a  `dnode_t *`
  is (within the domain of the subsystem under test).

This guidance pushes the programmer towards being explicit at the possible
expense of concision. This is in service of keeping the tests reliable; in
particular, if mocks require explicit casting to use, then there’s far less
chance of either a mock or a real object being used incorrectly in the test,
which can be confusing.

### Unit testing framework

[µnit](https://nemequ.github.io/munit/) (aka munit) is the unit test framework.
It is a relatively niche choice, and arguably abandoned by upstream, but is
well constructed with a thoughtful feature set and some useful properties:

* Just two source files we can easily carry in the repo.
* Portable, including to Windows.
* Each test is run in a forked process, so a test failure will not corrupt the
  rest of the test suite run
* Parameterised tests.
* A large suite of assertions and other useful functions that make it easy to
  integrate with.

All OpenZFS unit tests are ultimately targeting munit, so its expected that
they will use various features as needed. However, we also supply our own
facilities to extend those in useful ways.

#### Local extensions

`unit.h` provides a handful of macros. The majority of these are aliases for
the much longer munit names for same function, eg `unit_true(n)` is an alias
for `munit_assert_true(n)`, `unit_eq(a,b)` is an alias for
`munit_assert_uint64(a, ==, b)`, and so on. These are there so that the
assertions do not dominate the test visually, as we want it to be easier to
focus on the details.

Similarly, the `UINT_TEST` and `UNIT_PARAM` macros exist to help with test
definition, as the casts are a little complicated.

The goal is to keep this set relatively small, but all of munit is there for
use, so do extend it if necessary.
