# Steam compatibility tool interface

<!-- This document:
Copyright 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

## Compatibility tool declaration

Compatibility tools are declared by a file `compatibilitytool.vdf`,
which is a [VDF][] text file in this format:

```
"compatibilitytools"
{
  "compat tools"
  {
    "my_compat_tool"
    {
      "install_path" "."
      "display_name" "My Compatibility Tool"
      "from_oslist" "windows"
      "to_oslist" "linux"
    }
  }
}
```

`compat tools` can presumably contain one or more compatibility tools,
each identified by a unique name, in this case `my_compat_tool`.

Each compatibility tool can have these fields:

* `install_path`:

    Installation directory containing `toolmanifest.vdf` and other
    necessary files, for example
    `/path/to/steamapps/common/Proton - Experimental` for Proton

* `display_name`:

    Name to display in the Steam user interface

* `from_oslist`:

    Operating system(s?) whose executables can be run by this
    compatibility tool, for example `windows` for Proton

* `to_oslist`:

    Operating system(s?) that can run this compatibility tool,
    for example `linux` for Proton

For the compatibility tools provided by Steam itself, such as
Steam Linux Runtime and Proton, the equivalent of this information is
downloaded out-of-band and stored in `~/.steam/root/appcache/appinfo.vdf`,
so the depot downloaded into the Steam library will not contain this file.
As of early 2024, details of these built-in compatibility tools
can be viewed on the third-party SteamDB site as part of
[the app info of app 891390 "SteamPlay 2.0 Manifests][app891390-info].

Third-party compatibility tools can be installed in various locations
such as `~/.steam/root/compatibilitytools.d`.

Each subdirectory of `compatibilitytools.d` that contains
`compatibilitytool.vdf` is assumed to be a compatibility tool. The
installation path should be `"."` in this case.

Alternatively, the manifest can be placed directly in
`compatibilitytools.d` with the `install_path` set to the relative or
absolute path to the tool's installation directory.

## Tool manifest

Each compatibility tool has a *tool manifest*, `toolmanifest.vdf`, which
is a [VDF][] text file with one top-level entry, `manifest`.

`manifest` has the following known fields:

* `version`:

    The version number of the file format. The current version is `2`.
    The default is `1`.

* `commandline`:

    The command-line for the entry point.

* `compatmanager_layer_name`:

    The name of this compatibility tool within a stack of compatibility
    tools, used to target debugging commands at a particular layer
    in the stack without needing to know a specific Steam app ID.
    Compatibility tools that might be layered using `require_tool_appid`
    should have different names here.
    Compatibility tools that do not make sense to layer over each other,
    such as different versions of Proton, can share a single name here.

    Some example values for this field are:

    * `container-runtime`:

        Any self-contained Steam container runtime environment such as
        "[Steam Linux Runtime 2.0 (soldier)][soldier]" (app ID 1391110) or
        "[Steam Linux Runtime 3.0 (sniper)][sniper]" (app ID 1628350)

    * `proton`:

        Any official or unofficial version of Proton

    * `scout-in-container`:

        The container runtime environment that sets up the
        Steam Runtime 1 'scout'
        [`LD_LIBRARY_PATH` runtime][ldlp]
        as a layer over a `container-runtime`
        (see [Steam Linux Runtime 1.0 (scout)][scout-on-soldier])

* `filter_exclusive_priority`:

    A positive integer indicating that this tool is mutually exclusive
    with other similar tools.
    Compatibility tools that have this field are put together in a group,
    and when the Steam user interface shows users various options for
    running games, only the one that has the highest priority in the group
    is offered.
    Larger positive numbers are interpreted as higher-priority.
    This allows Steam to offer only "Steam Linux Runtime 1.0 (scout)"
    for historical native Linux games (which require/assume the scout ABI),
    while keeping later SLR branches available internally.
    Games' metadata can override this field to select a different
    compatibility tool as higher-priority: for example, when configuring
    a game that requires "Steam Linux Runtime 3.0 (sniper)", the priority
    for SLR 3.0 is raised higher than SLR 1.0 so that it will take
    precedence, resulting in SLR 3.0 being offered in the Steam UI and
    SLR 1.0 being hidden.

* `require_tool_appid`:

    If set, this compatibility tool needs to be wrapped in another
    compatibility tool, specified by its numeric Steam app ID. For
    example, Proton 5.13 needs to be wrapped by
    "Steam Linux Runtime 2.0 (soldier)", so it sets this field to `1391110`.

* `unlisted`:

    If `1`, this compatibility tool will not be shown to users as an option for
    running games. For example, until Steam is able to distinguish between
    Steam Runtime 1 'scout' and Steam Runtime 2 'soldier' as distinct
    platforms, it is not useful to run ordinary Steam games under 'soldier',
    due to it being incompatible with 'scout'.

* `use_sessions`:

    If set to `1`, older versions of Steam would use the "session mode"
    described below.

* `use_tool_subprocess_reaper`:

    If set to `1`, Steam will send `SIGINT` to the compatibility tool
    instead of proceeding directly to `SIGKILL`, to give it a chance
    to do graceful cleanup.

## The legacy `LD_LIBRARY_PATH` Steam Runtime

Older native Linux games on desktop systems (but usually not on Steam Deck)
are run in the [scout `LD_LIBRARY_PATH` runtime][ldlp].
This is an older mechanism than the compatibility tool interface, and does
not have a tool manifest or a compatibility tool declaration.

## Command-line

In a version 1 manifest, the `commandline` is a shell expression.
The first word should start with `/` and is interpreted as being
relative to the directory containing the compat tool, so `/run`
really means something like
`~/.local/share/Steam/steamapps/common/MyCompatTool/run`.

The game's command-line will be appended to the tool's command-line,
so if you are using `getopt` or a compatible command-line parser, you
might want to use a `commandline` like `/run --option --other-option --`
to get the game's command-line to appear after a `--` separator.

In a version 2 manifest, the special token `%verb%` can appear in the
`commandline`. It is replaced by either `run` or `waitforexitandrun`.

If a compatibility tool has `require_tool_appid` set, then the command-line
is built up by taking the required tool, then appending the tool that
requires it, and finally appending the actual game: so if a compat tool
named `Inner` requires a compat tool named `Outer`, and both tools have
`/run %verb% --` as their `commandline`, then the final command line will
look something like this:

    /path/to/Outer/run waitforexitandrun -- \
    /path/to/Inner/run waitforexitandrun -- \
    /path/to/game.exe

(In reality it would all be one line, rather than having escaped newlines
as shown here.)

For historical reasons, this command-line is normally run in the
[scout `LD_LIBRARY_PATH` runtime][ldlp] environment.
Most compatibility tools that do not already depend on another
compatibility tool will want to start by "escaping" from the scout
environment to get back to something more closely resembling the host
operating system environment: one convenient way to do this is to run a
command wrapped with
`"$STEAM_RUNTIME/scripts/switch-runtime.sh" --runtime="" --`.
See the `undo_steamrt` function in that script for more details of what
that means in practice.

Conversely, for non-Steam game shortcuts, this command-line is normally
run in an execution environment resembling the host operating system
environment.
Compatibility tools that do not already depend on another compatibility
tool should be prepared to operate correctly in this situation.

## Launch options

If a Steam game or a shortcut to a non-Steam game
has user-specified launch options configured in its
Properties, and they contain the special token `%command%`, then
`%command%` is replaced by the entire command-line built up from the
compatibility tools. For example, if a compat tool named `Inner` requires
a compat tool named `Outer`, both tools have `/run %verb% --` as their
`commandline`, and the user has set the Launch Options in the game's
properties to

    DEBUG=yes taskset --cpu-list 0,2 %command% -console

then the final command line will look something like this:

    DEBUG=yes taskset --cpu-list 0,2 \
    /path/to/Outer/run waitforexitandrun -- \
    /path/to/Inner/run waitforexitandrun -- \
    /path/to/game.exe -console

(In reality it would all be one line, rather than having escaped newlines
as shown here.)

If the user-specified launch options do not contain
`%command%`, then they are simply appended to the command-line. For
example, setting the launch options to `--debug` is equivalent to
`%command% --debug`.

Please note that not all invocations of compatibility tools take the
user-specified launch options into account: they are used when running
the actual game, but they are not used for install scripts.

Because the launch options are part of the command-line, they are run
in the [scout `LD_LIBRARY_PATH` runtime][ldlp] environment.

## Environment

Some environment variables are set by Steam, including:

* `LD_LIBRARY_PATH`:

    Used to load Steam Runtime libraries.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is particularly important.

* `LD_PRELOAD`:

    Used to load the Steam Overlay.

* `STEAM_COMPAT_APP_ID`:

    The decimal app-ID of the game, for example 440 for Team Fortress 2.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not set.
    Falling back to `$SteamAppId` can be used as an alternative.

* `STEAM_COMPAT_CLIENT_INSTALL_PATH`:

    The absolute path to the directory where Steam is installed. This is the
    same as the target of the `~/.steam/root` symbolic link.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not set.
    Falling back to dereferencing `~/.steam/root` can be used as an
    alternative.

* `STEAM_COMPAT_DATA_PATH`:

    The absolute path to a directory that compatibility tools can use to
    store game-specific data. For example, Proton puts its `WINEPREFIX`
    in `$STEAM_COMPAT_DATA_PATH/pfx`.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not set.

* `STEAM_COMPAT_FLAGS`:

    A comma-separated sequence of tokens set by Steam according to the
    game's metadata, affecting the functioning of various compatibility
    tools:

    * `runtime-sdl2`: Use the [`$SDL_DYNAMIC_API` mechanism][SDL_DYNAMIC_API]
        to load the version of SDL 2 included in
        Steam Linux Runtime 2 (soldier) or later,
        in preference to a bundled or statically linked SDL 2 that might be
        included in the game itself.
        For example, this can be used to ensure that older Source-engine
        titles like [Fistful of Frags][]
        runs with a version of SDL that supports game controller hotplug
        inside a container, even though they include a bundled version
        that does not.
        Setting `STEAM_COMPAT_RUNTIME_SDL2=1` is equivalent to this,
        and is more convenient to use in Launch Options, by setting the
        Launch Options to `STEAM_COMPAT_RUNTIME_SDL2=1 %command%`.

    * `runtime-sdl3`: Same as `runtime-sdl2`, but for SDL 3 (if available),
        using [`$SDL3_DYNAMIC_API`][SDL3_DYNAMIC_API].
        Setting `STEAM_COMPAT_RUNTIME_SDL3=1` is equivalent to this.

    * `search-cwd`: Instructs the legacy `LD_LIBRARY_PATH` runtime to
        append the `${STEAM_COMPAT_INSTALL_PATH}` to the `LD_LIBRARY_PATH`,
        for backward compatibility with older games that might rely on this
        behaviour (which was originally a bug in `run.sh`).
        This is currently only supported by scout-compatible environments,
        and not by Steam Runtime 2 'soldier' or later.

    * `search-cwd-first`: Same as **search-cwd**,
        but the `${STEAM_COMPAT_INSTALL_PATH}` is prepended to the
        `LD_LIBRARY_PATH` instead of appending.

    * Other tokens are likely to be added in future.
        Games and compatibility tools should ignore unknown flags.

* `STEAM_COMPAT_INSTALL_PATH`:

    The absolute path to the game's [install folder][], for example
    `/home/you/.local/share/Steam/steamapps/common/Estranged Act I`,
    even if the current working directory is a subdirectory of this
    (as is the case for Estranged: Act I (261820) for example).

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not set.

* `STEAM_COMPAT_LAUNCHER_SERVICE`:

    May be set to the `compatmanager_layer_name` of a single compatibility
    tool, to request that the tool is configured to allow debugging
    commands to be inserted into its runtime environment via IPC.

    The intention is that a future version of Steam will automatically
    set this to the `compatmanager_layer_name` of the last compatibility
    tool in the stack, if a debugging interface is requested.
    Until that feature becomes available, this variable can be set manually
    in a game's Steam "Launch Options" or in the environment variables
    passed to Steam itself.

    For compatibility tools that provide a Linux environment, such as
    "[Steam Linux Runtime 2.0 (soldier)][soldier]", the debugging commands
    should receive an execution environment that is equivalent to the
    execution environment of the game itself.
    For compatibility tools that provide a non-Linux environment,
    such as Proton, the debugging commands should be run at the closest
    possible point to the actual game that will still accept arbitrary
    Linux commands.
    For example, in Proton, Proton's special environment variables should
    be set for the debugging commands, but they are run as Linux programs
    (and must use Wine explicitly if running a Windows executable is
    desired), even though the actual game will be run under Wine.

    If unset, empty or set to an unrecognised string, then no debugging
    commands will be inserted.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not used.

* `STEAM_COMPAT_LIBRARY_PATHS`:

    Colon-delimited list of paths to Steam Library directories containing
    the game, the compatibility tools  if  any,  and any  other resources
    that the game will need, such as DirectX installers.

    In the legacy [scout `LD_LIBRARY_PATH` runtime][ldlp], this variable
    is not set.

* `STEAM_COMPAT_MOUNTS`:

    Colon-delimited list of paths to additional directories that are to be
    made available read/write in a pressure-vessel container.

* `STEAM_COMPAT_RUNTIME_SDL2`:

    May be set to 1 for the same effect as adding `runtime-sdl2` to
    `$STEAM_COMPAT_FLAGS`.
    (For example, a game's Launch Options can be set to
    `STEAM_COMPAT_RUNTIME_SDL2=1 %command%`)

* `STEAM_COMPAT_RUNTIME_SDL3`:

    May be set to 1 for the same effect as adding `runtime-sdl3` to
    `$STEAM_COMPAT_FLAGS`.
    (For example, a game's Launch Options can be set to
    `STEAM_COMPAT_RUNTIME_SDL3=1 %command%`)

* `STEAM_COMPAT_SESSION_ID`:

    In the historical session mode (see below), this is used to link together
    multiple container invocations into a logical "session".

* `STEAM_COMPAT_SHADER_PATH`:

    The absolute path to the variable data directory used for cached
    shaders, if any.

* `STEAM_COMPAT_TOOL_PATHS`:

    Colon-delimited list of paths to Steam compatibility tools in use.
    Typically this will be a single path, but when using tools with
    `require_tool_appid` set, such as Proton, it is a colon-separated
    list with the "innermost" tool first and the "outermost" tool last.

* `STEAM_COMPAT_TRACING`:

    Set to 1 if the Steam Deck's system tracing feature, or a similar
    system-wide tracing mechanism, is enabled.

* `SteamAppId`:

    The same as `STEAM_COMPAT_APP_ID`, but only when running the actual
    game; not set when running
    [install scripts][]
    or other setup commands. The Steamworks API assumes that every process
    with this environment variable set is part of the actual game.

When Steam invokes a tool with `require_tool_appid` set, such as Proton,
the environment variables are set as described for the "outermost" tool,
which can change them if required before it invokes the "innermost" tool
(although most should not be changed).

For example, when using Proton with "Steam Linux Runtime 2.0 (soldier)", the
"outer" compatibility tool "Steam Linux Runtime 2.0 (soldier)" is run with
`LD_LIBRARY_PATH` set by the Steam Runtime to point to mixed host and
scout libraries. After setting up the container, it runs the "inner"
compatibility tool (Proton) with an entirely new `LD_LIBRARY_PATH`
pointing to mixed host and soldier libraries.

## Native Linux Steam games

This is the simplest situation and can be considered to be the baseline.

In recent versions of Steam, the game process is wrapped in a `reaper`
process which sets itself as a subreaper using `PR_SET_CHILD_SUBREAPER`
(see [**prctl**(2)][prctl] for details).

Version 1 compat tools are not invoked specially.

Version 2 compat tools are invoked with a `%verb%` in the `commandline`
(if any) replaced by `waitforexitandrun`.

The game is expected to run in the usual way. Conventionally, it does not
double-fork to put itself in the background, but if it does, any background
processes will be reparented to have the subreaper as their parent.

If the game's Steam metadata has a non-empty working directory
(the `Working Dir` in its [Steamworks launch options][]),
then the current working directory is set to that directory before
launching any compatibility tools. For example, Estranged: Act 1
(app ID 261820) has `Working Dir: estrangedact1` and its
[install folder][] is set to `Estranged Act I`, so it runs with
current working directory `.../Estranged Act I/estrangedact1`.

If the working directory is left blank, then the current working directory
is set to the game's top-level [install folder][].
For example, Dota 2 (app ID 570) runs from its top-level install folder
`dota 2 beta`, even though its main executable is in the `game`
subdirectory.

Either way, the compatibility tool is expected to preserve this
working directory when running the actual game.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
of the game. The environment variable `SteamAppId` has the same value.

The [launch options](#launch-options) are used.

## Windows games with no install script

Some Windows games, such as Soldat (638490), do not have an
"install script" in their metadata. Running these games behaves much the
same as a native Linux game: it is wrapped in a subreaper, version 1
compat tools are not invoked specially, and version 2 compat tools
are invoked with a `%verb%` in the `commandline` (if any) replaced by
`waitforexitandrun`.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.
Historically, it was set to a unique token that was not used in any
previous invocation.

The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
of the game. The environment variable `SteamAppId` has the same value.

The current working directory is the working directory configured in the
game's Steam metadata, as usual.

The [launch options](#launch-options) are used.

## Windows games with an install script (since Steam beta 1623823138)

Other Windows games, such as Everquest (205710), have an
[install script][]
conventionally named `installscript.vdf`, which must be run by an
interpreter.

Since Steam beta 1623823138, these games are run like this:

* Run the install script. For version 1 compat tools, the compat
    tool is run as usual, except that the command appended to the
    command-line is the install script interpreter, not the actual game.
    For version 2 compat tools, the `%verb%` is set to `run` instead
    of the usual `waitforexitandrun`.

    The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

    The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
    of the game.

    The environment variable `SteamAppId` is *not* set in this case.

    The current working directory is *not* the game's directory in this case.
    As currently implemented, it appears to be the Steam installation
    (usually `~/.local/share/Steam` or `~/.steam/debian-installation`)
    but this is probably not guaranteed.

    The [launch options](#launch-options) are **not** used here.

* Run the actual game in the usual way.

    The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

    The environment variables `STEAM_COMPAT_APP_ID` and `SteamAppId`
    are both set to the app-ID of the game as usual.

    The current working directory is the working directory configured in
    the game's Steam metadata, as usual.

    The [launch options](#launch-options) are used, as usual.

## Historical behaviour of Windows games (session mode v2)

Before Steam beta 1623823138, Windows games had more complicated
launch behaviour.

<details><summary>Historical details</summary>

Before Steam beta 1623823138, each Windows game had several setup commands
to run before it could be launched. They were invoked like this:

* Run the first setup command. For version 1 compat tools, the compat
    tool is run as usual, except that the command appended to the
    command-line is the setup command, not the actual game.
    For version 2 compat tools, additionally the `%verb%` is set to `run`
    instead of `waitforexitandrun`.

    The environment variable `STEAM_COMPAT_SESSION_ID` is set to a
    unique token (in practice a 64-bit number encoded in hex).

    The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
    of the game.

    The environment variable `SteamAppId` is *not* set in this case.

    The current working directory is *not* the game's directory in this case.
    As currently implemented, it appears to be the Steam installation
    (usually `~/.local/share/Steam` or `~/.steam/debian-installation`)
    but this is probably not guaranteed.

    The [launch options](#launch-options) are **not** used here.

    The compat tool is expected to do any setup that it needs to do
    (for example pressure-vessel must start a container), then run
    the given command and exit. The compat tool may leave processes
    running in the background.

* Run the second setup command, in the same way as the first, and so on.
    More than one command can be run in parallel, so tools that need to
    do setup (such as pressure-vessel) must use a lock to ensure the setup
    is only done by whichever setup command wins the race to be the first.

    All environment variables are the same as for the first setup command,
    and in particular the `STEAM_COMPAT_SESSION_ID` is the same. This is
    how compatibility tools can know which containers or subprocesses
    to reuse.

* When all setup commands have finished, run the actual game. For
    version 2 compat tools, the `%verb%` is set to `waitforexitandrun`.
    The compat tool is expected to terminate any background processes
    (for example pressure-vessel terminates the container and Proton
    terminates the `wineserver`) and wait for them to exit, then launch
    the actual game.

    The environment variable `STEAM_COMPAT_SESSION_ID` is set to the
    same unique token as for all the setup commands, so that the compat
    tool can identify which subprocesses or containers to tear down.

    The environment variables `STEAM_COMPAT_APP_ID` and `SteamAppId`
    are both set to the app-ID of the game as usual.

    The [launch options](#launch-options) are used, as usual.

</details>

## Non-Steam games

Version 1 compat tools are not invoked specially.

Version 2 compat tools are invoked with a `%verb%` in the `commandline`
(if any) replaced by `waitforexitandrun`.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.
Historically, it was set to a unique token that was not used in any
previous invocation.

`STEAM_COMPAT_APP_ID` is set to `0`. `SteamAppId` is not set.

`STEAM_COMPAT_DATA_PATH` is set to a unique directory per non-Steam game.
It is currently in the same format as Steam games' equivalent directories,
but with a large arbitrary integer replacing the Steam app ID.

`STEAM_COMPAT_INSTALL_PATH` is not set.

`LD_LIBRARY_PATH` is usually set to the empty string to "escape" from
the Steam Runtime library stack for non-Steam games, although games
configured in Steam via development tools can be flagged to use the
Steam Runtime library stack (this is done for games that are intended to
be released onto Steam as native Linux games).

The current working directory defaults to the directory containing the
main executable, but can be reconfigured through the shortcut's properties.
Steam Linux Runtime (pressure-vessel) containers will not work as expected
unless this is set to the game's top-level directory.

[Launch options](#launch-options) behave the same as for Steam games.

<!-- References: -->

[SDL_DYNAMIC_API]: https://github.com/libsdl-org/SDL/blob/SDL2/docs/README-dynapi.md
[SDL3_DYNAMIC_API]: https://github.com/libsdl-org/SDL/blob/main/docs/README-dynapi.md
[Steamworks launch options]: https://partner.steamgames.com/doc/sdk/uploading
[VDF]: https://developer.valvesoftware.com/wiki/KeyValues
[app891390-info]: https://steamdb.info/app/891390/info/
[install folder]: https://partner.steamgames.com/doc/store/application/depots
[install script]: https://partner.steamgames.com/doc/sdk/installscripts
[install scripts]: https://partner.steamgames.com/doc/sdk/installscripts
[ldlp]: ld-library-path-runtime.md
[prctl]: https://manpages.debian.org/unstable/manpages-dev/prctl.2.en.html
[scout-on-soldier]: container-runtime.md#steam-linux-runtime-scout-on-soldier
[sniper]: container-runtime.md#steam-runtime-3-sniper
[soldier]: container-runtime.md#steam-runtime-2-soldier
