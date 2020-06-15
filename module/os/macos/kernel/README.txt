
Not all symbols are exported by default in OS X, and we have to do
a little magic to get around that.

This uses the OpenSource kextsymbol.c utility, and a dump of all the
symbols in the kernel, to produce a link helper kext.

We most likely need to make it better to handle kernel versions
a little more flexibly.

