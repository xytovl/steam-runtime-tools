---
title: steam-runtime-dialog-ui
section: 1
...

<!-- This document:
Copyright 2024 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-dialog-ui - user interface implementation for **steam-runtime-dialog**(1)

# SYNOPSIS

**steam-runtime-dialog-ui**
**--check-features=**_FEATURES_

**steam-runtime-dialog-ui**
**--error**|**--warning**|**--info**
[*OPTIONS*...]

**steam-runtime-dialog-ui**
**--progress**
[*OPTIONS*...]

# DESCRIPTION

**steam-runtime-dialog-ui** is a sample implementation of a user interface
dialog for **steam-runtime-dialog**(1).

It is intended to be invoked via **steam-runtime-dialog**(1) and not directly.

# MODES

Exactly one mode argument must be provided.
See **steam-runtime-dialog**(1) for a list of the possible modes.

# OPTIONS

See **steam-runtime-dialog**(1) for a list of the possible options.

# ENVIRONMENT

`STEAM_RUNTIME_DIALOG_FULLSCREEN`
:   If set to a non-empty value other than `0`, the user interface will be
    displayed full-screen.

`XDG_CURRENT_DESKTOP`
:   If set to `gamescope`, the user interface will be displayed full-screen.

# EXIT STATUS

0
:   The user interface was presented successfully.

Any other value
:   The user interface was not presented successfully, or was presented
    successfully but cancelled by the user.

<!-- vim:set sw=4 sts=4 et: -->

