Subprojects built as part of steam-runtime-tools
================================================

<!-- This document:
Copyright 2023-2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

See the commit history for more detailed rationale for why components
are vendored.

In addition to the subprojects in these directories, several files in the
`pressure-vessel/` subdirectory are modified versions of equivalent code
from Flatpak, and `helpers/` contains code modified from libwaffle.

A complete relocatable build of pressure-vessel also includes several
independent shared libraries which are not built as subprojects: see
`pressure-vessel/THIRD-PARTY.md` for details of those.

Several of these projects are managed using `git-subtree(1)`.
We do not use `git-submodule(1)` because that would make the project
less self-contained.


bubblewrap
----------

Upstream: <https://github.com/containers/bubblewrap>

pressure-vessel uses bubblewrap to create containers.

We build our own copy of bubblewrap so that we can make use of features
of newer versions even on older host operating systems, and so that we can
build it with a suitable `DT_RPATH` to be included in relocatable builds
of pressure-vessel.

To compare with upstream:

    git remote add --no-tags bubblewrap https://github.com/containers/bubblewrap
    git fetch bubblewrap
    git diff HEAD:subprojects/bubblewrap bubblewrap/main

To merge from upstream (do this after an upstream release):

    git fetch bubblewrap
    git subtree merge -P subprojects/bubblewrap bubblewrap/main

If you configure `.git/config` to replace

    tagopt = --no-tags

with

    fetch = refs/tags/*:refs/tags/bubblewrap/*

then you can refer to bubblewrap releases via tag names like
`bubblewrap/v0.9.0`, and merge them with commands like:

    git subtree merge -P subprojects/bubblewrap bubblewrap/v0.9.0

container-runtime
-----------------

Upstream: this project

Scripts in this directory combine pressure-vessel and a Steam Runtime
sysroot to form the `SteamLinuxRuntime`, `SteamLinuxRuntime_soldier` and
`SteamLinuxRuntime_sniper` Steampipe depots.

This is arranged like a subproject for historical reasons, but has no
independent existence, and should gradually be integrated into the parent
project.

libcapsule
----------

Upstream: <https://gitlab.collabora.com/vivek/libcapsule/>

pressure-vessel uses libcapsule's capsule-capture-libs tool to modify
its containers to integrate graphics drivers and their dependencies from
the host system.

We don't actually use libcapsule itself: the long term plan is to do so,
but that will require future glibc enhancements.

We build our own copy of the capsule-capture-libs tool to make it
easier to test capsule-capture-libs improvements without spending time on
libcapsule release management (steam-runtime-tools is its only significant
user right now), and so that we can build it with a suitable `DT_RPATH`
to be included in relocatable builds of pressure-vessel.

To compare with upstream or merge from upstream, the procedure is similar
to bubblewrap (see above).

libglnx
-------

Upstream: <https://gitlab.gnome.org/GNOME/libglnx/>

libglnx is used throughout steam-runtime-tools for Linux- or gcc-specific
utility code, and for backports of useful functions from newer GLib releases.

This is a "copylib", similar to gnulib, which only supports being
integrated as a subproject.

To compare with upstream or merge from upstream, the procedure is similar
to bubblewrap (see above).

xisxwayland
-----------

Upstream: <https://gitlab.freedesktop.org/xorg/app/xisxwayland/>

steam-runtime-system-info uses code from xisxwayland to detect whether
the X11 display is actually Xwayland.

We build our own copy to give it a wrapper with stable machine-readable
output (`helpers/is-x-server-xwayland.c`).

To compare with upstream or merge from upstream, the procedure is similar
to bubblewrap (see above).
