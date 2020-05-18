xiswayland
==========

`xisxwayland` is a tool to be used within shell scripts to determine whether
the X server in use is Xwayland. It exits with status 0 if the server is an
Xwayland server and 1 otherwise. Any error results in an exit code of 3.


The usual use-case would be like this:
```
if [ -z "$DISPLAY" ]; then
   echo "cannot check, DISPlAY is unset"
   exit 1
fi

if xisxwayland; then
   echo "yay, xwayland"
fi
```

Source code
-----------

The source code of xiswayland can be found at:
https://gitlab.freedesktop.org/xorg/app/xisxwayland

Reporting Bugs
--------------

Bugs can be filed in the freedesktop.org GitLab instance:
https://gitlab.freedesktop.org/xorg/app/xisxwayland/issues/

License
-------

`xisxwayland` is licensed under the MIT license. See the COPYING file for full
license information.
