Container based Steam Runtime
=============================

This container-based release of the Steam Runtime is used for Proton 5.13+.

Known issues
------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/steamlinuxruntime-known-issues.md

Reporting bugs
--------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/reporting-steamlinuxruntime-bugs.md

Development and debugging
-------------------------

The runtime's behaviour can be changed by running the Steam client with
environment variables set.

`STEAM_LINUX_RUNTIME_LOG=1` will enable logging. Log files appear in
`SteamLinuxRuntime/var/slr-*.log`, with filenames containing the app ID.
`slr-latest.log` is a symbolic link to whichever one was created most
recently.

`STEAM_LINUX_RUNTIME_VERBOSE=1` produces more detailed log output,
either to a log file (if `STEAM_LINUX_RUNTIME_LOG=1` is also used) or to
the same place as `steam` output (otherwise).

Some more advanced environment variables (subject to change):

* `PRESSURE_VESSEL_SHELL=instead` runs an interactive shell in the
    container instead of running the game.

* See the pressure-vessel source code for more.

Licensing and copyright
-----------------------

The Steam Runtime contains many third-party software packages under
various open-source licenses.

For full source code, please see the version-numbered subdirectories of
<https://repo.steampowered.com/steamrt-images-scout/snapshots/> and
<https://repo.steampowered.com/steamrt-images-soldier/snapshots/>
corresponding to the version numbers listed in VERSIONS.txt.
