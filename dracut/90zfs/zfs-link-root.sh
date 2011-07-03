#!/bin/sh

# Create dummy symlink for /dev/root so Dracut's hook scripts finish.
[ -e /dev/null ] && { ln -s /dev/null /dev/root 2>/dev/null ; return 0 ; }
return 1
