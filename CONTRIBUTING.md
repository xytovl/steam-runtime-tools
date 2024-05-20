# Contributing and maintenance

<!-- This file:
Copyright 2019-2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

## Reporting bugs

There is currently no issue tracker for libcapsule itself.

For issues encountered when using libcapsule as part of the Steam Runtime,
steam-runtime-tools or pressure-vessel, please report bugs to
<https://github.com/ValveSoftware/steam-runtime/issues>.
Before reporting an issue, please take a look at the
[bug reporting information](https://github.com/ValveSoftware/steam-runtime/blob/HEAD/doc/reporting-steamlinuxruntime-bugs.md)
to make sure your issue report contains all the information we need.

## Contributing code

At the moment our Gitlab installation, `gitlab.collabora.com`, is not
set up to receive merge requests from third-party contributors.
However, git is a distributed version control system, so it is possible
to push a clone of the libcapsule git repository to some other git
hosting platform (such as Github or `gitlab.com`) and send a link to a
proposed branch.

When contributing code to libcapsule, please include a `Signed-off-by`
message in your commits to indicate acceptance of the
[Developer's Certificate of Origin](https://developercertificate.org/) terms.

## Compiling libcapsule

libcapsule is a typical Autotools project, using gtk-doc for documentation.
Please refer to general Autotools and gtk-doc documentation for details.
`ci/gitlab-ci.yml` illustrates several ways to build it, and the
convenience script `./autogen.sh` automates bootstrapping the build
system in a git checkout.

### Compiling on very old distributions

Building libcapsule from git requires GNU autoconf-archive macros. The
oldest supported version is 20180313-1 from Debian 9 'stretch'. If your
development system or container is older, unpack or clone a suitable
version of autoconf-archive and use commands like:

   libcapsule$ ACLOCAL_PATH=/path/to/autoconf-archive/m4 NOCONFIGURE=1 ./autogen.sh
   libcapsule$ ./configure
   libcapsule$ make

Building from tarball releases removes this requirement.

## Release procedure

* The version number is *EPOCH*.*YYYYMMDD*.*MICRO* where:

    - *EPOCH* increases on major (incompatible) changes, or if you change
      the version-numbering scheme
    - *YYYYMMDD* is today's date if you are doing a feature release, or
      the date of the version you are branching from if you are applying
      "stable branch" hotfixes to an older version
    - *MICRO* starts at 0, and increases if you need to apply
      "stable branch" hotfixes to an old version (or if you need to do
      two feature releases on the same day)

* Update `debian/changelog`: `gbp dch --full`, edit to clarify the
  changelog if desired, `dch -r`, set the version number.

* Commit everything.

* Add an annotated git tag v*VERSION*.

* Do a final release build, for example using [deb-build-snapshot][]:

    ```
    deb-build-snapshot -d ~/tmp/build-area --release localhost
    ```

    or with `make distcheck`.

* [Create a new release][gitlab-release]

* Upload the `.dsc`, `.debian.tar.*`, `.orig.tar.*` as attachments

[deb-build-snapshot]: https://salsa.debian.org/smcv/deb-build-snapshot
[gitlab-release]: https://gitlab.collabora.com/vivek/libcapsule/-/releases/new)
