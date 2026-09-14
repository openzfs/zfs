InvariantDisks
==============

InvariantDisks is a small program maintaining a mapping from invariant labels to the potentially
varying /dev/diskXsY entries. It is started like:

`InvariantDisks -p $PREFIX`

At the moment, three linker modules are available. The media path module creates links in
$prefix/by-path based on the reported media path, based on the physical location of the
device. Example:

 * `./by-path/PCI0@0-SATA@1F,2-PRT0@0-PMP@0-@0:0`

The UUID module creates links in $prefix/by-id based on the volume and media UUID. Some volumes
have both a volume and a media UUID, others only one of the two. This depends on the partitioning
scheme and the filesystem. Example:

 * `./by-id/volume-AAFA3521-C1B4-3154-B16E-143D133FA21A`
 * `./by-id/media-9722675C-36BB-499D-8609-120D6BDF8609`

The Serial number module creates links to whole drives based on the product type and product
serial number.

 * `./by-serial/WDC_WD30EZRX-00MMMB-WD-WCAWZ12345`

The Problem and some solutions on Linux are described on
http://zfsonlinux.org/faq.html#WhatDevNamesShouldIUseWhenCreatingMyPool

License
=======

This program is copyrighted by me, because I wrote it.
This program is licensed under the "3-clause BSD" License. See the BSD.LICENSE.md file for details.
If desired, it is also licensed under the CDDL OpenSolaris license, see OPENSOLARIS.LICENSE.txt
for details. Other licenses are available on request.
