# Introduction to the Steam container runtime framework

<!-- This document:
Copyright 2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

The Steam container runtime framework, often referred to as
*Steam Linux Runtime*, is a collection of container environments
which can be used to run Steam games on Linux in a relatively predictable
container environment, even when running on an arbitrary Linux
distribution which might be old, new or unusually set up.

Instead of forming a `LD_LIBRARY_PATH` that merges the host OS's shared
libraries with the shared libraries provided by Valve, these new runtimes
use Linux namespace (container) technology to build a more predictable
environment.

It is implemented as a collection of Steam Play compatibility tools.

More technical background on this work is available in a talk recorded at
FOSDEM 2020:
<https://archive.fosdem.org/2020/schedule/event/containers_steam/>.
Many of the features described as future work in that talk have now been
implemented and are in active use.

## <a name="pressure-vessel"></a>`pressure-vessel`

The core of all of these compatibility tools is
[pressure-vessel][],
which combines application-level libraries from the Steam Runtime
with graphics drivers from the host operating system, resulting in a
system that is as compatible as possible with the Steam Runtime
while still having all the necessary graphics drivers to work with recent
GPU hardware.

The Steam Play compatibility tools automatically run pressure-vessel
when necessary.

## <a name="soldier"></a>Steam Linux Runtime 2.0 (soldier)

[soldier]: #soldier

Steam Runtime 2, `soldier`, is a newer runtime than
[scout][ldlp], based on Debian 10 (released in 2019).
Most of its libraries are taken directly from Debian, and can benefit
from Debian's long-term security support.
Selected libraries that are particularly important for games, such as
SDL and Vulkan-Loader, have been upgraded to newer versions backported
from newer branches of Debian.

soldier is designed to be used as a container runtime for `pressure-vessel`,
and [cannot be used](#why) as a
[`LD_LIBRARY_PATH` runtime][ldlp].

soldier is used as a runtime environment for Proton versions 5.13 to
7.0, which are compiled against the newer library stack and would not
be compatible with scout.
Newer versions of Proton use a newer runtime (see below).

Native Linux games that require soldier cannot currently be released on Steam.
The next-generation runtime for native Linux games is intended to be
Steam Runtime 3 `sniper` (see [below][sniper]).

The *Steam Linux Runtime 2.0 (soldier)* compatibility tool, app ID 1391110,
is automatically downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime_soldier` when you select a version
of Proton that requires it, or the *Steam Linux Runtime 1.0 (scout)*
compatibility tool which requires it (see below).
It can also be installed by running this command:

    steam steam://install/1391110

Documentation in the `steamrt` "metapackage" provides
[more information about soldier](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/soldier/README.md).

## <a name="scout-on-soldier"></a>Steam Linux Runtime 1.0 (scout)

[scout-on-soldier]: #scout-on-soldier

Steam offers a large number of older native Linux games.
Some of these games, such as Team Fortress 2, were carefully compiled in
a strict `scout` environment, so that they can run in the
[scout `LD_LIBRARY_PATH` runtime][ldlp],
or in any environment that provides at least the same libraries as scout.

Unfortunately, many native Linux games have been compiled in a newer
environment, and will only work in the `LD_LIBRARY_PATH` runtime
if the host operating system happens to provide libraries that are newer
than the ones in `scout`, while still being compatible with the game's
assumptions.
This is not a stable situation: a game that happened to work in Ubuntu
20.04 could easily be broken by a routine upgrade to Ubuntu 22.04.

The *Steam Linux Runtime 1.0 (scout)* compatibility tool, app ID 1070560,
uses the same container technology as `soldier` to mitigate this problem.
It will automatically be downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime` if it is selected to run a particular
game, or if a game requires it.
It can also be installed by running this command:

    steam steam://install/1070560

Unlike the [soldier](#soldier) and [sniper](#sniper) container runtimes,
it is implemented by entering a [`soldier`](#soldier) container, and then
setting up a [`scout` `LD_LIBRARY_PATH` runtime][ldlp] inside that container.

Since [November 2024][Steam client 2024-11-05],
it is the default for most native Linux games,
unless the game developer has configured the game to use a different
runtime environment.
Before November 2024,
it was used by default on Steam Deck but not on desktop systems.

Users can also specifically opt-in to running specific games in the
Steam Linux Runtime 1.0 (scout) container via *Properties* → *Compatibility*.

[Steam client 2024-11-05]: https://store.steampowered.com/news/collection/steam/?emclan=103582791457287600&emgid=4472730495692571024

## <a name="sniper"></a>Steam Linux Runtime 3.0 (sniper)

[sniper]: #sniper

Steam Runtime 3, `sniper`, is another newer runtime based on Debian 11
(released in 2021).
It is very similar to [`soldier`](#soldier), except for its base distribution
being 2 years newer: this means its core libraries and compiler are also
approximately 2 years newer.
Proton 8.0 moved from soldier to sniper to take advantage of this newer base.

Native Linux games that require sniper can be released on Steam.
Since October 2024, this is available as a "self-service"
feature via the Steamworks partner web interface, which can be used by
any game that benefits from a newer library stack.

Early adopters of this mechanism include
[Retroarch][] since [August 2022][Retroarch on sniper],
[Endless Sky][] since [early/mid 2023][Endless Sky on Sniper] and
[Dota 2][] since [mid 2023][Dota 2 sniper],

The *Steam Linux Runtime 3.0 - sniper* compatibility tool, app ID 1628350,
will automatically be downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime_sniper` if a game requires it.
It can also be installed by running this command:

    steam steam://install/1628350

In Steam client betas since January 2024, a private copy of the sniper
runtime is installed into `~/.steam/root/ubuntu12_64/steam-runtime-sniper`
and used to run the Steam client user interface (`steamwebhelper`).

Documentation in the `steamrt` "metapackage" provides
[more information about sniper](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/sniper/README.md).

## <a name="medic"></a>Steam Runtime 4, `medic`

[medic]: #medic

Steam Runtime 4, `medic`, is a prototype runtime currently based on Debian 12
(released in 2023).
Like [`sniper`][sniper], it is structurally similar to `soldier`, but with a newer
base distribution.

`medic` is not yet available as a compatibility tool, and its
contents are subject to change depending on testing results and game
requirements.

## <a name="steamrt5"></a>Steam Runtime 5

[steamrt5]: #steamrt5

Steam Runtime 5 is a prototype runtime currently based on Debian 13
(which is expected to be released in mid 2025).
Like [`sniper`][sniper], it is structurally similar to `soldier`, but with a newer
base distribution.

`steamrt5` is not yet available as a compatibility tool, and its
contents are subject to change depending on testing results and game
requirements.

## <a name="why"></a>Why the container runtimes are necessary

[why]: #why

The [traditional `LD_LIBRARY_PATH` runtime][ldlp]
only works because modern host OSs are strictly newer than it.
Making a `LD_LIBRARY_PATH`-based runtime reliable is difficult, especially
since we want it to be runnable on host OSs that have some packages that
are older than the runtime, allowing users of older LTS distributions to
run the latest games.

Some libraries cannot be bundled in a `LD_LIBRARY_PATH` for technical
reasons (mainly glibc and graphics drivers). A `LD_LIBRARY_PATH` runtime
needs to get these from the host system, and they need to be at least the
version it was compiled against. This is fine for scout, which is very
old, but would not be OK for a Debian 10-based runtime, which wouldn't work
on (for example) Ubuntu 18.04.

Some libraries *can* be bundled, but need to be patched to search for
plugins in different places (either inside the runtime itself, or in
multiple distro-dependent places), which is not really sustainable.
Avoiding the need to patch these libraries greatly reduces the time
required to update them, ensuring that we can apply security and
bug-fix updates as needed.

Using namespace (container) technology to replace `/usr` with the
runtime's libraries sidesteps both these problems.

## Reporting issues

Bugs and issues in the Steam Runtime should be reported to the
[steam-runtime project on Github][Steam Runtime issues].

## Acknowledgements

The libraries included in the container runtimes are derived
from [Debian][] and [Ubuntu][]
packages, and indirectly from various upstream projects.
See the copyright information included in the Steam Runtime for details.

The container technology used in `pressure-vessel` is heavily based on
code from [Flatpak][], and makes use of the
lower-level components [bubblewrap][] and [libcapsule][].
libcapsule is heavily based on internal code from glibc's dynamic linker,
and of course, all of this container/namespace juggling relies on features
contributed to the Linux kernel.

<!-- References -->

[Debian]: https://www.debian.org/
[Dota 2 scout SLR]: https://store.steampowered.com/news/app/570/view/4978168332488878344
[Dota 2 sniper]: https://mastodon.social/@TTimo/110578711292322771
[Dota 2]: https://store.steampowered.com/app/570/Dota_2/
[Endless Sky on sniper]: https://github.com/ValveSoftware/steam-runtime/issues/556
[Endless Sky]: https://endless-sky.github.io/
[Flatpak]: https://flatpak.org/
[Retroarch on sniper]: https://github.com/libretro/RetroArch/issues/14266
[Retroarch]: https://www.retroarch.com/
[Steam Runtime issues]: https://github.com/ValveSoftware/steam-runtime/issues
[Ubuntu]: https://ubuntu.com/
[bubblewrap]: https://github.com/containers/bubblewrap
[ldlp]: ld-library-path-runtime.md
[libcapsule]: https://gitlab.collabora.com/vivek/libcapsule
[pressure-vessel]: pressure-vessel.md
