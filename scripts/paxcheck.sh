#!/bin/sh

if ! command -v scanelf > /dev/null; then
    echo "scanelf (from pax-utils) is required for these checks." >&2
    exit 3
fi

RET=0

# check for exec stacks
OUT=$(scanelf -qyRAF '%e %p' "$1")

if [ x"${OUT}" != x ]; then
    RET=2
    echo "The following files contain writable and executable sections"
    echo " Files with such sections will not work properly (or at all!) on some"
    echo " architectures/operating systems."
    echo " For more information, see:"
    echo "   https://wiki.gentoo.org/wiki/Hardened/GNU_stack_quickstart"
    echo
    echo "${OUT}"
    echo
fi


# check for TEXTRELS
OUT=$(scanelf -qyRAF '%T %p' "$1")

if [ x"${OUT}" != x ]; then
    RET=2
    echo "The following files contain runtime text relocations"
    echo " Text relocations force the dynamic linker to perform extra"
    echo " work at startup, waste system resources, and may pose a security"
    echo " risk.  On some architectures, the code may not even function"
    echo " properly, if at all."
    echo " For more information, see:"
    echo "   https://wiki.gentoo.org/wiki/Hardened/HOWTO_locate_and_fix_textrels"
    echo
    echo "${OUT}"
    echo
fi

exit $RET
