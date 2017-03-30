# Contributing to ZFS on Linux
<p align="center"><img src="http://zfsonlinux.org/images/zfs-linux.png"/></p>

*First of all, thank you for taking the time to contribute!*

By using the following guidelines, you can help us make ZFS on Linux even
better.

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

Helpful resources

  * [ZFS on Linux wiki](https://github.com/zfsonlinux/zfs/wiki)
  * [OpenZFS Documentation](http://open-zfs.org/wiki/Developer_resources)

## What should I know before I get started?

### Get ZFS
You can build zfs packages by following [these
instructions](https://github.com/zfsonlinux/zfs/wiki/Building-ZFS),
or install stable packages from [your distribution's
repository](https://github.com/zfsonlinux/zfs/wiki/Getting-Started).

### Debug ZFS
A variety of methods and tools are available to aid ZFS developers.
It's strongly recommended that when developing a patch the `--enable-debug`
configure option should be set. This will enable additional correctness
checks and all the ASSERTs to help quickly catch potential issues.

In addition, there are numerous utilities and debugging files which
provide visibility in to the inner workings of ZFS.  The most useful
of these tools are discussed in detail on the [debugging ZFS wiki
page](https://github.com/zfsonlinux/zfs/wiki/Debugging).

### Where can I ask for help?
The [mailing list](https://github.com/zfsonlinux/zfs/wiki/Mailing-Lists)
is the best place to ask for help.

## How Can I Contribute?

### Reporting Bugs
*Please* contact us via the [mailing
list](https://github.com/zfsonlinux/zfs/wiki/Mailing-Lists) if you aren't
certain that you are experiencing a bug.

If you run into an issue, please search our [issue
tracker](https://github.com/zfsonlinux/zfs/issues) *first* to ensure the
issue hasn't been reported before. Open a new issue only if you haven't
found anything similar to your issue.

You can open a new issue and search existing issues using the public [issue
tracker](https://github.com/zfsonlinux/zfs/issues).

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
ZFS on Linux is a widely deployed production filesystem which is under
active development. The team's primary focus is on fixing known issues,
improving performance, and adding compelling new features.

You can view the list of proposed features
by filtering the issue tracker by the ["Feature"
label](https://github.com/zfsonlinux/zfs/issues?q=is%3Aopen+is%3Aissue+label%3AFeature).
If you have an idea for a feature first check this list. If your idea already
appears then add a +1 to the top most comment, this helps us gauge interest
in that feature.

Otherwise, open a new issue and describe your proposed feature.  Why is this
feature needed?  What problem does it solve?

### Pull Requests
* All pull requests must be based on the current master branch and apply
without conflicts.
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
* Test cases should be provided when appropriate.
* If your pull request improves performance, please include some benchmarks.
* The pull request must pass all required [ZFS
Buildbot](http://build.zfsonlinux.org/) builders before
being accepted. If you are experiencing intermittent TEST
builder failures, you may be experiencing a [test suite
issue](https://github.com/zfsonlinux/zfs/issues?q=is%3Aissue+is%3Aopen+label%3A%22Test+Suite%22).
* All proposed changes must be approved by a ZFS on Linux organization member.

### Testing
All help is appreciated! If you're in a position to run the latest code
consider helping us by reporting any functional problems, performance
regressions or other suspected issues. By running the latest code to a wide
range of realistic workloads, configurations and architectures we're better
able quickly identify and resolve potential issues.

Users can also run the [ZFS Test
Suite](https://github.com/zfsonlinux/zfs/tree/master/tests) on their systems
to verify ZFS is behaving as intended.

## Style Guides

### Coding Conventions
We currently use [C  Style  and  Coding  Standards  for
SunOS](http://www.cis.upenn.edu/%7Elee/06cse480/data/cstyle.ms.pdf) as our
coding convention.

### Commit Message Formats
#### New Changes
Commit messages for new changes must meet the following guidelines:
* In 50 characters or less, provide a summary of the change as the
first line in the commit message.
* A body which provides a description of the change. If necessary,
please summarize important information such as why the proposed
approach was chosen or a brief description of the bug you are resolving.
Each line of the body must be 72 characters or less.
* The last line must be a `Signed-off-by:` line with the developer's
name followed by their email.

Git can append the `Signed-off-by` line to your commit messages. Simply
provide the `-s` or `--signoff` option when performing a `git commit`.
For more information about writing commit messages, visit [How to Write
a Git Commit Message](https://chris.beams.io/posts/git-commit/).
An example commit message is provided below.

```
This line is a brief summary of your change

Please provide at least a couple sentences describing the
change. If necessary, please summarize decisions such as
why the proposed approach was chosen or what bug you are
attempting to solve.

Signed-off-by: Contributor <contributor@email.com>
```

#### OpenZFS Patch Ports
If you are porting an OpenZFS patch, the commit message must meet
the following guidelines:
* The first line must be the summary line from the OpenZFS commit.
It must begin with `OpenZFS dddd - ` where `dddd` is the OpenZFS issue number.
* Provides a `Authored by:` line to attribute the patch to the original author.
* Provides the `Reviewed by:` and `Approved by:` lines from the original
OpenZFS commit.
* Provides a `Ported-by:` line with the developer's name followed by
their email.
* Provides a `OpenZFS-issue:` line which is a link to the original illumos
issue.
* Provides a `OpenZFS-commit:` line which links back to the original OpenZFS
commit.
* If necessary, provide some porting notes to describe any deviations from
the original OpenZFS commit.

An example OpenZFS patch port commit message is provided below.
```
OpenZFS 1234 - Summary from the original OpenZFS commit

Authored by: Original Author <original@email.com>
Reviewed by: Reviewer One <reviewer1@email.com>
Reviewed by: Reviewer Two <reviewer2@email.com>
Approved by: Approver One <approver1@email.com>
Ported-by: ZFS Contributor <contributor@email.com>

Provide some porting notes here if necessary.

OpenZFS-issue: https://www.illumos.org/issues/1234
OpenZFS-commit: https://github.com/openzfs/openzfs/commit/abcd1234
```
