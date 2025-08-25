# Contributing to OpenZFS
<p align="center">
  <img alt="OpenZFS Logo"
    src="https://openzfs.github.io/openzfs-docs/_static/img/logo/480px-Open-ZFS-Secondary-Logo-Colour-halfsize.png"/>
</p>

*First of all, thank you for taking the time to contribute!*

By using the following guidelines, you can help us make OpenZFS even better.

## Table Of Contents
[What should I know before I get
started?](#what-should-i-know-before-i-get-started)

  * [Get ZFS](#get-zfs)
  * [Debug ZFS](#debug-zfs)
  * [Where can I ask for help?](#where-can-I-ask-for-help)

[How Can I Contribute?](#how-can-i-contribute)

  * [Reporting Bugs](#reporting-bugs)
  * [Suggesting Enhancements](#suggesting-enhancements)
  * [Pull Requests](#pull-requests)
  * [Testing](#testing)

[Style Guides](#style-guides)

  * [Coding Conventions](#coding-conventions)
  * [Commit Message Formats](#commit-message-formats)
    * [New Changes](#new-changes)
    * [OpenZFS Patch Ports](#openzfs-patch-ports)
    * [Coverity Defect Fixes](#coverity-defect-fixes)
    * [Signed Off By](#signed-off-by)

Helpful resources

  * [OpenZFS Documentation](https://openzfs.github.io/openzfs-docs/)
  * [OpenZFS Developer Resources](http://open-zfs.org/wiki/Developer_resources)
  * [Git and GitHub for beginners](https://openzfs.github.io/openzfs-docs/Developer%20Resources/Git%20and%20GitHub%20for%20beginners.html)

## What should I know before I get started?

### Get ZFS
You can build zfs packages by following [these
instructions](https://openzfs.github.io/openzfs-docs/Developer%20Resources/Building%20ZFS.html),
or install stable packages from [your distribution's
repository](https://openzfs.github.io/openzfs-docs/Getting%20Started/index.html).

### Debug ZFS
A variety of methods and tools are available to aid ZFS developers.
It's strongly recommended that when developing a patch the `--enable-debug`
configure option should be set. This will enable additional correctness
checks and all the ASSERTs to help quickly catch potential issues.

In addition, there are numerous utilities and debugging files which
provide visibility into the inner workings of ZFS.  The most useful
of these tools are discussed in detail on the [Troubleshooting
page](https://openzfs.github.io/openzfs-docs/Basic%20Concepts/Troubleshooting.html).

### Where can I ask for help?
The [zfs-discuss mailing
list](https://openzfs.github.io/openzfs-docs/Project%20and%20Community/Mailing%20Lists.html)
or IRC are the best places to ask for help. Please do not file
support requests on the GitHub issue tracker.

## How Can I Contribute?

### Reporting Bugs
*Please* contact us via the [zfs-discuss mailing
list](https://openzfs.github.io/openzfs-docs/Project%20and%20Community/Mailing%20Lists.html)
or IRC if you aren't certain that you are experiencing a bug.

If you run into an issue, please search our [issue
tracker](https://github.com/openzfs/zfs/issues) *first* to ensure the
issue hasn't been reported before. Open a new issue only if you haven't
found anything similar to your issue.

You can open a new issue and search existing issues using the public [issue
tracker](https://github.com/openzfs/zfs/issues).

#### When opening a new issue, please include the following information at the top of the issue:
* What distribution (with version) you are using.
* The spl and zfs versions you are using, installation method (repository
or manual compilation).
* Describe the issue you are experiencing.
* Describe how to reproduce the issue.
* Including any warning/errors/backtraces from the system logs.

When a new issue is opened, it is not uncommon for developers to request
additional information.

In general, the more detail you share about a problem the quicker a
developer can resolve it. For example, providing a simple test case is always
exceptionally helpful.

Be prepared to work with the developers investigating your issue. Your
assistance is crucial in providing a quick solution. They may ask for
information like:

* Your pool configuration as reported by `zdb` or `zpool status`.
* Your hardware configuration, such as
  * Number of CPUs.
  * Amount of memory.
  * Whether your system has ECC memory.
  * Whether it is running under a VMM/Hypervisor.
  * Kernel version.
  * Values of the spl/zfs module parameters.
* Stack traces which may be logged to `dmesg`.

### Suggesting Enhancements
OpenZFS is a widely deployed production filesystem which is under active
development. The team's primary focus is on fixing known issues, improving
performance, and adding compelling new features.

You can view the list of proposed features
by filtering the issue tracker by the ["Type: Feature"
label](https://github.com/openzfs/zfs/issues?q=is%3Aopen+is%3Aissue+label%3A%22Type%3A+Feature%22).
If you have an idea for a feature first check this list. If your idea already
appears then add a +1 to the top most comment, this helps us gauge interest
in that feature.

Otherwise, open a new issue and describe your proposed feature.  Why is this
feature needed?  What problem does it solve?

### Pull Requests

#### General

* All pull requests, except backports and releases, must be based on the current master branch 
and should apply without conflicts.
* Please attempt to limit pull requests to a single commit which resolves
one specific issue.
* Make sure your commit messages are in the correct format. See the
[Commit Message Formats](#commit-message-formats) section for more information.
* When updating a pull request squash multiple commits by performing a
[rebase](https://git-scm.com/docs/git-rebase) (squash).
* For large pull requests consider structuring your changes as a stack of
logically independent patches which build on each other.  This makes large
changes easier to review and approve which speeds up the merging process.
* Try to keep pull requests simple. Simple code with comments is much easier
to review and approve.
* All proposed changes must be approved by an OpenZFS organization member.
* If you have an idea you'd like to discuss or which requires additional testing, consider opening it as a draft pull request.
Once everything is in good shape and the details have been worked out you can remove its draft status.
Any required reviews can then be finalized and the pull request merged.

#### Tests and Benchmarks
* Every pull request is tested using a GitHub Actions workflow on multiple platforms by running the [zfs-tests.sh and zloop.sh](
https://openzfs.github.io/openzfs-docs/Developer%20Resources/Building%20ZFS.html#running-zloop-sh-and-zfs-tests-sh) test suites.
`.github/workflows/scripts/generate-ci-type.py` is used to determine whether the pull request is nonbehavior, i.e., not introducing behavior changes of any code, configuration or tests. If so, the CI will run on fewer platforms and only essential sanity tests will run. You can always override this by adding `ZFS-CI-Type` line to your commit message:
  * If your last commit (or `HEAD` in git terms) contains a line `ZFS-CI-Type: quick`, quick mode is forced regardless of what files are changed.
  * Otherwise, if any commit in a PR contains a line `ZFS-CI-Type: full`, full mode is forced.
* To verify your changes conform to the [style guidelines](
https://github.com/openzfs/zfs/blob/master/.github/CONTRIBUTING.md#style-guides
), please run `make checkstyle` and resolve any warnings.
* Code analysis is performed by [CodeQL](https://codeql.github.com/) for each pull request.
* Test cases should be provided when appropriate.  This includes making sure new features have adequate code coverage.
* If your pull request improves performance, please include some benchmarks.
* The pull request must pass all CI checks before being accepted.

### Testing
All help is appreciated! If you're in a position to run the latest code
consider helping us by reporting any functional problems, performance
regressions or other suspected issues. By running the latest code to a wide
range of realistic workloads, configurations and architectures we're better
able quickly identify and resolve potential issues.

Users can also run the [ZFS Test
Suite](https://github.com/openzfs/zfs/tree/master/tests) on their systems
to verify ZFS is behaving as intended.

## Style Guides

### Repository Structure

OpenZFS uses a standardised branching structure.
- The "development and main branch", is the branch all development should be based on.
- "Release branches" contain the latest released code for said version.
- "Staging branches" contain selected commits prior to being released.

**Branch Names:**
- Development and Main branch: `master`
- Release branches: `zfs-$VERSION-release`
- Staging branches: `zfs-$VERSION-staging`

`$VERSION` should be replaced with the `major.minor` version number.  
_(This is the version number without the `.patch` version at the end)_

### Coding Conventions
We currently use [C  Style  and  Coding  Standards  for
SunOS](http://www.cis.upenn.edu/%7Elee/06cse480/data/cstyle.ms.pdf) as our
coding convention.

This repository has an `.editorconfig` file. If your editor [supports
editorconfig](https://editorconfig.org/#download), it will
automatically respect most of this project's whitespace preferences.

Additionally, Git can help warn on whitespace problems as well:

```
git config --local core.whitespace trailing-space,space-before-tab,indent-with-non-tab,-tab-in-indent
```

### Commit Message Formats
#### New Changes
Commit messages for new changes must meet the following guidelines:
* In 72 characters or less, provide a summary of the change as the
first line in the commit message.
* A body which provides a description of the change. If necessary,
please summarize important information such as why the proposed
approach was chosen or a brief description of the bug you are resolving.
Each line of the body must be 72 characters or less.
* The last line must be a `Signed-off-by:` tag. See the
[Signed Off By](#signed-off-by) section for more information.

An example commit message for new changes is provided below.

```
This line is a brief summary of your change

Please provide at least a couple sentences describing the
change. If necessary, please summarize decisions such as
why the proposed approach was chosen or what bug you are
attempting to solve.

Signed-off-by: Contributor <contributor@email.com>
```

#### Coverity Defect Fixes
If you are submitting a fix to a
[Coverity defect](https://scan.coverity.com/projects/zfsonlinux-zfs),
the commit message should meet the following guidelines:
* Provides a subject line in the format of
`Fix coverity defects: CID dddd, dddd...` where `dddd` represents
each CID fixed by the commit.
* Provides a body which lists each Coverity defect and how it was corrected.
* The last line must be a `Signed-off-by:` tag. See the
[Signed Off By](#signed-off-by) section for more information.

An example Coverity defect fix commit message is provided below.
```
Fix coverity defects: CID 12345, 67890

CID 12345: Logically dead code (DEADCODE)

Removed the if(var != 0) block because the condition could never be
satisfied.

CID 67890: Resource Leak (RESOURCE_LEAK)

Ensure free is called after allocating memory in function().

Signed-off-by: Contributor <contributor@email.com>
```

#### Signed Off By
A line tagged as `Signed-off-by:` must contain the developer's
name followed by their email. This is the developer's certification
that they have the right to submit the patch for inclusion into
the code base and indicates agreement to the [Developer's Certificate
of Origin](https://www.kernel.org/doc/html/latest/process/submitting-patches.html#sign-your-work-the-developer-s-certificate-of-origin).
Code without a proper signoff cannot be merged.

Git can append the `Signed-off-by` line to your commit messages. Simply
provide the `-s` or `--signoff` option when performing a `git commit`.
For more information about writing commit messages, visit [How to Write
a Git Commit Message](https://chris.beams.io/posts/git-commit/).

#### Co-authored By
If someone else had part in your pull request, please add the following to the commit:
`Co-authored-by: Name <gitregistered@email.address>`
This is useful if their authorship was lost during squashing, rebasing, etc.,
but may be used in any situation where there are co-authors.

The email address used here should be the same as on the GitHub profile of said user.
If said user does not have their email address public, please use the following instead:
`Co-authored-by: Name <[username]@users.noreply.github.com>`
