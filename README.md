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
