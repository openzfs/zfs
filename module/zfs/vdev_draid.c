/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2018 Intel Corporation.
 * Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_draid.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_rebuild.h>
#include <sys/abd.h>
#include <sys/zio.h>
#include <sys/nvpair.h>
#include <sys/zio_checksum.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <zfs_fletcher.h>

#ifdef ZFS_DEBUG
#include <sys/vdev.h>	/* For vdev_xlate() in vdev_draid_io_verify() */
#endif

/*
 * dRAID is a distributed spare implementation for ZFS. A dRAID vdev is
 * comprised of multiple raidz redundancy groups which are spread over the
 * dRAID children. To ensure an even distribution, and avoid hot spots, a
 * permutation mapping is applied to the order of the dRAID children.
 * This mixing effectively distributes the parity columns evenly over all
 * of the disks in the dRAID.
 *
 * This is beneficial because it means when resilvering all of the disks
 * can participate thereby increasing the available IOPs and bandwidth.
 * Furthermore, by reserving a small fraction of each child's total capacity
 * virtual distributed spare disks can be created. These spares similarly
 * benefit from the performance gains of spanning all of the children. The
 * consequence of which is that resilvering to a distributed spare can
 * substantially reduce the time required to restore full parity to pool
 * with a failed disks.
 *
 * === dRAID group layout ===
 *
 * First, let's define a "row" in the configuration to be a 16M chunk from
 * each physical drive at the same offset. This is the minimum allowable
 * size since it must be possible to store a full 16M block when there is
 * only a single data column. Next, we define a "group" to be a set of
 * sequential disks containing both the parity and data columns. We allow
 * groups to span multiple rows in order to align any group size to any
 * number of physical drives. Finally, a "slice" is comprised of the rows
 * which contain the target number of groups. The permutation mappings
 * are applied in a round robin fashion to each slice.
 *
 * Given D+P drives in a group (including parity drives) and C-S physical
 * drives (not including the spare drives), we can distribute the groups
 * across R rows without remainder by selecting the least common multiple
 * of D+P and C-S as the number of groups; i.e. ngroups = LCM(D+P, C-S).
 *
 * In the example below, there are C=14 physical drives in the configuration
 * with S=2 drives worth of spare capacity. Each group has a width of 9
 * which includes D=8 data and P=1 parity drive. There are 4 groups and
 * 3 rows per slice.  Each group has a size of 144M (16M * 9) and a slice
 * size is 576M (144M * 4). When allocating from a dRAID each group is
 * filled before moving on to the next as show in slice0 below.
 *
 *             data disks (8 data + 1 parity)          spares (2)
 *     +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *  ^  | 2 | 6 | 1 | 11| 4 | 0 | 7 | 10| 8 | 9 | 13| 5 | 12| 3 | device map 0
 *  |  +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *  |  |              group 0              |  group 1..|       |
 *  |  +-----------------------------------+-----------+-------|
 *  |  | 0   1   2   3   4   5   6   7   8 | 36  37  38|       |  r
 *  |  | 9   10  11  12  13  14  15  16  17| 45  46  47|       |  o
 *  |  | 18  19  20  21  22  23  24  25  26| 54  55  56|       |  w
 *     | 27  28  29  30  31  32  33  34  35| 63  64  65|       |  0
 *  s  +-----------------------+-----------------------+-------+
 *  l  |       ..group 1       |        group 2..      |       |
 *  i  +-----------------------+-----------------------+-------+
 *  c  | 39  40  41  42  43  44| 72  73  74  75  76  77|       |  r
 *  e  | 48  49  50  51  52  53| 81  82  83  84  85  86|       |  o
 *  0  | 57  58  59  60  61  62| 90  91  92  93  94  95|       |  w
 *     | 66  67  68  69  70  71| 99 100 101 102 103 104|       |  1
 *  |  +-----------+-----------+-----------------------+-------+
 *  |  |..group 2  |            group 3                |       |
 *  |  +-----------+-----------+-----------------------+-------+
 *  |  | 78  79  80|108 109 110 111 112 113 114 115 116|       |  r
 *  |  | 87  88  89|117 118 119 120 121 122 123 124 125|       |  o
 *  |  | 96  97  98|126 127 128 129 130 131 132 133 134|       |  w
 *  v  |105 106 107|135 136 137 138 139 140 141 142 143|       |  2
 *     +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *     | 9 | 11| 12| 2 | 4 | 1 | 3 | 0 | 10| 13| 8 | 5 | 6 | 7 | device map 1
 *  s  +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *  l  |              group 4              |  group 5..|       | row 3
 *  i  +-----------------------+-----------+-----------+-------|
 *  c  |       ..group 5       |        group 6..      |       | row 4
 *  e  +-----------+-----------+-----------------------+-------+
 *  1  |..group 6  |            group 7                |       | row 5
 *     +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *     | 3 | 5 | 10| 8 | 6 | 11| 12| 0 | 2 | 4 | 7 | 1 | 9 | 13| device map 2
 *  s  +===+===+===+===+===+===+===+===+===+===+===+===+===+===+
 *  l  |              group 8              |  group 9..|       | row 6
 *  i  +-----------------------------------------------+-------|
 *  c  |       ..group 9       |        group 10..     |       | row 7
 *  e  +-----------------------+-----------------------+-------+
 *  2  |..group 10 |            group 11               |       | row 8
 *     +-----------+-----------------------------------+-------+
 *
 * This layout has several advantages over requiring that each row contain
 * a whole number of groups.
 *
 * 1. The group count is not a relevant parameter when defining a dRAID
 *    layout. Only the group width is needed, and *all* groups will have
 *    the desired size.
 *
 * 2. All possible group widths (<= physical disk count) can be supported.
 *
 * 3. The logic within vdev_draid.c is simplified when the group width is
 *    the same for all groups (although some of the logic around computing
 *    permutation numbers and drive offsets is more complicated).
 *
 * N.B. The following array describes all valid dRAID permutation maps.
 * Each row is used to generate a permutation map for a different number
 * of children from a unique seed. The seeds were generated and carefully
 * evaluated by the 'draid' utility in order to provide balanced mappings.
 * In addition to the seed a checksum of the in-memory mapping is stored
 * for verification.
 *
 * The imbalance ratio of a given failure (e.g. 5 disks wide, child 3 failed,
 * with a given permutation map) is the ratio of the amounts of I/O that will
 * be sent to the least and most busy disks when resilvering. The average
 * imbalance ratio (of a given number of disks and permutation map) is the
 * average of the ratios of all possible single and double disk failures.
 *
 * In order to achieve a low imbalance ratio the number of permutations in
 * the mapping must be significantly larger than the number of children.
 * For dRAID the number of permutations has been limited to 512 to minimize
 * the map size. This does result in a gradually increasing imbalance ratio
 * as seen in the table below. Increasing the number of permutations for
 * larger child counts would reduce the imbalance ratio. However, in practice
 * when there are a large number of children each child is responsible for
 * fewer total IOs so it's less of a concern.
 *
 * Note these values are hard coded and must never be changed.  Existing
 * pools depend on the same mapping always being generated in order to
 * read and write from the correct locations.  Any change would make
 * existing pools completely inaccessible.
 */
static const draid_map_t draid_maps[VDEV_DRAID_MAX_MAPS] = {
	{   2, 256, 0x89ef3dabbcc7de37, 0x00000000433d433d },	/* 1.000 */
	{   3, 256, 0x89a57f3de98121b4, 0x00000000bcd8b7b5 },	/* 1.000 */
	{   4, 256, 0xc9ea9ec82340c885, 0x00000001819d7c69 },	/* 1.000 */
	{   5, 256, 0xf46733b7f4d47dfd, 0x00000002a1648d74 },	/* 1.010 */
	{   6, 256, 0x88c3c62d8585b362, 0x00000003d3b0c2c4 },	/* 1.031 */
	{   7, 256, 0x3a65d809b4d1b9d5, 0x000000055c4183ee },	/* 1.043 */
	{   8, 256, 0xe98930e3c5d2e90a, 0x00000006edfb0329 },	/* 1.059 */
	{   9, 256, 0x5a5430036b982ccb, 0x00000008ceaf6934 },	/* 1.056 */
	{  10, 256, 0x92bf389e9eadac74, 0x0000000b26668c09 },	/* 1.072 */
	{  11, 256, 0x74ccebf1dcf3ae80, 0x0000000dd691358c },	/* 1.083 */
	{  12, 256, 0x8847e41a1a9f5671, 0x00000010a0c63c8e },	/* 1.097 */
	{  13, 256, 0x7481b56debf0e637, 0x0000001424121fe4 },	/* 1.100 */
	{  14, 256, 0x559b8c44065f8967, 0x00000016ab2ff079 },	/* 1.121 */
	{  15, 256, 0x34c49545a2ee7f01, 0x0000001a6028efd6 },	/* 1.103 */
	{  16, 256, 0xb85f4fa81a7698f7, 0x0000001e95ff5e66 },	/* 1.111 */
	{  17, 256, 0x6353e47b7e47aba0, 0x00000021a81fa0fe },	/* 1.133 */
	{  18, 256, 0xaa549746b1cbb81c, 0x00000026f02494c9 },	/* 1.131 */
	{  19, 256, 0x892e343f2f31d690, 0x00000029eb392835 },	/* 1.130 */
	{  20, 256, 0x76914824db98cc3f, 0x0000003004f31a7c },	/* 1.141 */
	{  21, 256, 0x4b3cbabf9cfb1d0f, 0x00000036363a2408 },	/* 1.139 */
	{  22, 256, 0xf45c77abb4f035d4, 0x00000038dd0f3e84 },	/* 1.150 */
	{  23, 256, 0x5e18bd7f3fd4baf4, 0x0000003f0660391f },	/* 1.174 */
	{  24, 256, 0xa7b3a4d285d6503b, 0x000000443dfc9ff6 },	/* 1.168 */
	{  25, 256, 0x56ac7dd967521f5a, 0x0000004b03a87eb7 },	/* 1.180 */
	{  26, 256, 0x3a42dfda4eb880f7, 0x000000522c719bba },	/* 1.226 */
	{  27, 256, 0xd200d2fc6b54bf60, 0x0000005760b4fdf5 },	/* 1.228 */
	{  28, 256, 0xc52605bbd486c546, 0x0000005e00d8f74c },	/* 1.217 */
	{  29, 256, 0xc761779e63cd762f, 0x00000067be3cd85c },	/* 1.239 */
	{  30, 256, 0xca577b1e07f85ca5, 0x0000006f5517f3e4 },	/* 1.238 */
	{  31, 256, 0xfd50a593c518b3d4, 0x0000007370e7778f },	/* 1.273 */
	{  32, 512, 0xc6c87ba5b042650b, 0x000000f7eb08a156 },	/* 1.191 */
	{  33, 512, 0xc3880d0c9d458304, 0x0000010734b5d160 },	/* 1.199 */
	{  34, 512, 0xe920927e4d8b2c97, 0x00000118c1edbce0 },	/* 1.195 */
	{  35, 512, 0x8da7fcda87bde316, 0x0000012a3e9f9110 },	/* 1.201 */
	{  36, 512, 0xcf09937491514a29, 0x0000013bd6a24bef },	/* 1.194 */
	{  37, 512, 0x9b5abbf345cbd7cc, 0x0000014b9d90fac3 },	/* 1.237 */
	{  38, 512, 0x506312a44668d6a9, 0x0000015e1b5f6148 },	/* 1.242 */
	{  39, 512, 0x71659ede62b4755f, 0x00000173ef029bcd },	/* 1.231 */
	{  40, 512, 0xa7fde73fb74cf2d7, 0x000001866fb72748 },	/* 1.233 */
	{  41, 512, 0x19e8b461a1dea1d3, 0x000001a046f76b23 },	/* 1.271 */
	{  42, 512, 0x031c9b868cc3e976, 0x000001afa64c49d3 },	/* 1.263 */
	{  43, 512, 0xbaa5125faa781854, 0x000001c76789e278 },	/* 1.270 */
	{  44, 512, 0x4ed55052550d721b, 0x000001d800ccd8eb },	/* 1.281 */
	{  45, 512, 0x0fd63ddbdff90677, 0x000001f08ad59ed2 },	/* 1.282 */
	{  46, 512, 0x36d66546de7fdd6f, 0x000002016f09574b },	/* 1.286 */
	{  47, 512, 0x99f997e7eafb69d7, 0x0000021e42e47cb6 },	/* 1.329 */
	{  48, 512, 0xbecd9c2571312c5d, 0x000002320fe2872b },	/* 1.286 */
	{  49, 512, 0xd97371329e488a32, 0x0000024cd73f2ca7 },	/* 1.322 */
	{  50, 512, 0x30e9b136670749ee, 0x000002681c83b0e0 },	/* 1.335 */
	{  51, 512, 0x11ad6bc8f47aaeb4, 0x0000027e9261b5d5 },	/* 1.305 */
	{  52, 512, 0x68e445300af432c1, 0x0000029aa0eb7dbf },	/* 1.330 */
	{  53, 512, 0x910fb561657ea98c, 0x000002b3dca04853 },	/* 1.365 */
	{  54, 512, 0xd619693d8ce5e7a5, 0x000002cc280e9c97 },	/* 1.334 */
	{  55, 512, 0x24e281f564dbb60a, 0x000002e9fa842713 },	/* 1.364 */
	{  56, 512, 0x947a7d3bdaab44c5, 0x000003046680f72e },	/* 1.374 */
	{  57, 512, 0x2d44fec9c093e0de, 0x00000324198ba810 },	/* 1.363 */
	{  58, 512, 0x87743c272d29bb4c, 0x0000033ec48c9ac9 },	/* 1.401 */
	{  59, 512, 0x96aa3b6f67f5d923, 0x0000034faead902c },	/* 1.392 */
	{  60, 512, 0x94a4f1faf520b0d3, 0x0000037d713ab005 },	/* 1.360 */
	{  61, 512, 0xb13ed3a272f711a2, 0x00000397368f3cbd },	/* 1.396 */
	{  62, 512, 0x3b1b11805fa4a64a, 0x000003b8a5e2840c },	/* 1.453 */
	{  63, 512, 0x4c74caad9172ba71, 0x000003d4be280290 },	/* 1.437 */
	{  64, 512, 0x035ff643923dd29e, 0x000003fad6c355e1 },	/* 1.402 */
	{  65, 512, 0x768e9171b11abd3c, 0x0000040eb07fed20 },	/* 1.459 */
	{  66, 512, 0x75880e6f78a13ddd, 0x000004433d6acf14 },	/* 1.423 */
	{  67, 512, 0x910b9714f698a877, 0x00000451ea65d5db },	/* 1.447 */
	{  68, 512, 0x87f5db6f9fdcf5c7, 0x000004732169e3f7 },	/* 1.450 */
	{  69, 512, 0x836d4968fbaa3706, 0x000004954068a380 },	/* 1.455 */
	{  70, 512, 0xc567d73a036421ab, 0x000004bd7cb7bd3d },	/* 1.463 */
	{  71, 512, 0x619df40f240b8fed, 0x000004e376c2e972 },	/* 1.463 */
	{  72, 512, 0x42763a680d5bed8e, 0x000005084275c680 },	/* 1.452 */
	{  73, 512, 0x5866f064b3230431, 0x0000052906f2c9ab },	/* 1.498 */
	{  74, 512, 0x9fa08548b1621a44, 0x0000054708019247 },	/* 1.526 */
	{  75, 512, 0xb6053078ce0fc303, 0x00000572cc5c72b0 },	/* 1.491 */
	{  76, 512, 0x4a7aad7bf3890923, 0x0000058e987bc8e9 },	/* 1.470 */
	{  77, 512, 0xe165613fd75b5a53, 0x000005c20473a211 },	/* 1.527 */
	{  78, 512, 0x3ff154ac878163a6, 0x000005d659194bf3 },	/* 1.509 */
	{  79, 512, 0x24b93ade0aa8a532, 0x0000060a201c4f8e },	/* 1.569 */
	{  80, 512, 0xc18e2d14cd9bb554, 0x0000062c55cfe48c },	/* 1.555 */
	{  81, 512, 0x98cc78302feb58b6, 0x0000066656a07194 },	/* 1.509 */
	{  82, 512, 0xc6c5fd5a2abc0543, 0x0000067cff94fbf8 },	/* 1.596 */
	{  83, 512, 0xa7962f514acbba21, 0x000006ab7b5afa2e },	/* 1.568 */
	{  84, 512, 0xba02545069ddc6dc, 0x000006d19861364f },	/* 1.541 */
	{  85, 512, 0x447c73192c35073e, 0x000006fce315ce35 },	/* 1.623 */
	{  86, 512, 0x48beef9e2d42b0c2, 0x00000720a8e38b6b },	/* 1.620 */
	{  87, 512, 0x4874cf98541a35e0, 0x00000758382a2273 },	/* 1.597 */
	{  88, 512, 0xad4cf8333a31127a, 0x00000781e1651b1b },	/* 1.575 */
	{  89, 512, 0x47ae4859d57888c1, 0x000007b27edbe5bc },	/* 1.627 */
	{  90, 512, 0x06f7723cfe5d1891, 0x000007dc2a96d8eb },	/* 1.596 */
	{  91, 512, 0xd4e44218d660576d, 0x0000080ac46f02d5 },	/* 1.622 */
	{  92, 512, 0x7066702b0d5be1f2, 0x00000832c96d154e },	/* 1.695 */
	{  93, 512, 0x011209b4f9e11fb9, 0x0000085eefda104c },	/* 1.605 */
	{  94, 512, 0x47ffba30a0b35708, 0x00000899badc32dc },	/* 1.625 */
	{  95, 512, 0x1a95a6ac4538aaa8, 0x000008b6b69a42b2 },	/* 1.687 */
	{  96, 512, 0xbda2b239bb2008eb, 0x000008f22d2de38a },	/* 1.621 */
	{  97, 512, 0x7ffa0bea90355c6c, 0x0000092e5b23b816 },	/* 1.699 */
	{  98, 512, 0x1d56ba34be426795, 0x0000094f482e5d1b },	/* 1.688 */
	{  99, 512, 0x0aa89d45c502e93d, 0x00000977d94a98ce },	/* 1.642 */
	{ 100, 512, 0x54369449f6857774, 0x000009c06c9b34cc },	/* 1.683 */
	{ 101, 512, 0xf7d4dd8445b46765, 0x000009e5dc542259 },	/* 1.755 */
	{ 102, 512, 0xfa8866312f169469, 0x00000a16b54eae93 },	/* 1.692 */
	{ 103, 512, 0xd8a5aea08aef3ff9, 0x00000a381d2cbfe7 },	/* 1.747 */
	{ 104, 512, 0x66bcd2c3d5f9ef0e, 0x00000a8191817be7 },	/* 1.751 */
	{ 105, 512, 0x3fb13a47a012ec81, 0x00000ab562b9a254 },	/* 1.751 */
	{ 106, 512, 0x43100f01c9e5e3ca, 0x00000aeee84c185f },	/* 1.726 */
	{ 107, 512, 0xca09c50ccee2d054, 0x00000b1c359c047d },	/* 1.788 */
	{ 108, 512, 0xd7176732ac503f9b, 0x00000b578bc52a73 },	/* 1.740 */
	{ 109, 512, 0xed206e51f8d9422d, 0x00000b8083e0d960 },	/* 1.780 */
	{ 110, 512, 0x17ead5dc6ba0dcd6, 0x00000bcfb1a32ca8 },	/* 1.836 */
	{ 111, 512, 0x5f1dc21e38a969eb, 0x00000c0171becdd6 },	/* 1.778 */
	{ 112, 512, 0xddaa973de33ec528, 0x00000c3edaba4b95 },	/* 1.831 */
	{ 113, 512, 0x2a5eccd7735a3630, 0x00000c630664e7df },	/* 1.825 */
	{ 114, 512, 0xafcccee5c0b71446, 0x00000cb65392f6e4 },	/* 1.826 */
	{ 115, 512, 0x8fa30c5e7b147e27, 0x00000cd4db391e55 },	/* 1.843 */
	{ 116, 512, 0x5afe0711fdfafd82, 0x00000d08cb4ec35d },	/* 1.826 */
	{ 117, 512, 0x533a6090238afd4c, 0x00000d336f115d1b },	/* 1.803 */
	{ 118, 512, 0x90cf11b595e39a84, 0x00000d8e041c2048 },	/* 1.857 */
	{ 119, 512, 0x0d61a3b809444009, 0x00000dcb798afe35 },	/* 1.877 */
	{ 120, 512, 0x7f34da0f54b0d114, 0x00000df3922664e1 },	/* 1.849 */
	{ 121, 512, 0xa52258d5b72f6551, 0x00000e4d37a9872d },	/* 1.867 */
	{ 122, 512, 0xc1de54d7672878db, 0x00000e6583a94cf6 },	/* 1.978 */
	{ 123, 512, 0x1d03354316a414ab, 0x00000ebffc50308d },	/* 1.947 */
	{ 124, 512, 0xcebdcc377665412c, 0x00000edee1997cea },	/* 1.865 */
	{ 125, 512, 0x4ddd4c04b1a12344, 0x00000f21d64b373f },	/* 1.881 */
	{ 126, 512, 0x64fc8f94e3973658, 0x00000f8f87a8896b },	/* 1.882 */
	{ 127, 512, 0x68765f78034a334e, 0x00000fb8fe62197e },	/* 1.867 */
	{ 128, 512, 0xaf36b871a303e816, 0x00000fec6f3afb1e },	/* 1.972 */
	{ 129, 512, 0x2a4cbf73866c3a28, 0x00001027febfe4e5 },	/* 1.896 */
	{ 130, 512, 0x9cb128aacdcd3b2f, 0x0000106aa8ac569d },	/* 1.965 */
	{ 131, 512, 0x5511d41c55869124, 0x000010bbd755ddf1 },	/* 1.963 */
	{ 132, 512, 0x42f92461937f284a, 0x000010fb8bceb3b5 },	/* 1.925 */
	{ 133, 512, 0xe2d89a1cf6f1f287, 0x0000114cf5331e34 },	/* 1.862 */
	{ 134, 512, 0xdc631a038956200e, 0x0000116428d2adc5 },	/* 2.042 */
	{ 135, 512, 0xb2e5ac222cd236be, 0x000011ca88e4d4d2 },	/* 1.935 */
	{ 136, 512, 0xbc7d8236655d88e7, 0x000011e39cb94e66 },	/* 2.005 */
	{ 137, 512, 0x073e02d88d2d8e75, 0x0000123136c7933c },	/* 2.041 */
	{ 138, 512, 0x3ddb9c3873166be0, 0x00001280e4ec6d52 },	/* 1.997 */
	{ 139, 512, 0x7d3b1a845420e1b5, 0x000012c2e7cd6a44 },	/* 1.996 */
	{ 140, 512, 0x60102308aa7b2a6c, 0x000012fc490e6c7d },	/* 2.053 */
	{ 141, 512, 0xdb22bb2f9eb894aa, 0x00001343f5a85a1a },	/* 1.971 */
	{ 142, 512, 0xd853f879a13b1606, 0x000013bb7d5f9048 },	/* 2.018 */
	{ 143, 512, 0x001620a03f804b1d, 0x000013e74cc794fd },	/* 1.961 */
	{ 144, 512, 0xfdb52dda76fbf667, 0x00001442d2f22480 },	/* 2.046 */
	{ 145, 512, 0xa9160110f66e24ff, 0x0000144b899f9dbb },	/* 1.968 */
	{ 146, 512, 0x77306a30379ae03b, 0x000014cb98eb1f81 },	/* 2.143 */
	{ 147, 512, 0x14f5985d2752319d, 0x000014feab821fc9 },	/* 2.064 */
	{ 148, 512, 0xa4b8ff11de7863f8, 0x0000154a0e60b9c9 },	/* 2.023 */
	{ 149, 512, 0x44b345426455c1b3, 0x000015999c3c569c },	/* 2.136 */
	{ 150, 512, 0x272677826049b46c, 0x000015c9697f4b92 },	/* 2.063 */
	{ 151, 512, 0x2f9216e2cd74fe40, 0x0000162b1f7bbd39 },	/* 1.974 */
	{ 152, 512, 0x706ae3e763ad8771, 0x00001661371c55e1 },	/* 2.210 */
	{ 153, 512, 0xf7fd345307c2480e, 0x000016e251f28b6a },	/* 2.006 */
	{ 154, 512, 0x6e94e3d26b3139eb, 0x000016f2429bb8c6 },	/* 2.193 */
	{ 155, 512, 0x5458bbfbb781fcba, 0x0000173efdeca1b9 },	/* 2.163 */
	{ 156, 512, 0xa80e2afeccd93b33, 0x000017bfdcb78adc },	/* 2.046 */
	{ 157, 512, 0x1e4ccbb22796cf9d, 0x00001826fdcc39c9 },	/* 2.084 */
	{ 158, 512, 0x8fba4b676aaa3663, 0x00001841a1379480 },	/* 2.264 */
	{ 159, 512, 0xf82b843814b315fa, 0x000018886e19b8a3 },	/* 2.074 */
	{ 160, 512, 0x7f21e920ecf753a3, 0x0000191812ca0ea7 },	/* 2.282 */
	{ 161, 512, 0x48bb8ea2c4caa620, 0x0000192f310faccf },	/* 2.148 */
	{ 162, 512, 0x5cdb652b4952c91b, 0x0000199e1d7437c7 },	/* 2.355 */
	{ 163, 512, 0x6ac1ba6f78c06cd4, 0x000019cd11f82c70 },	/* 2.164 */
	{ 164, 512, 0x9faf5f9ca2669a56, 0x00001a18d5431f6a },	/* 2.393 */
	{ 165, 512, 0xaa57e9383eb01194, 0x00001a9e7d253d85 },	/* 2.178 */
	{ 166, 512, 0x896967bf495c34d2, 0x00001afb8319b9fc },	/* 2.334 */
	{ 167, 512, 0xdfad5f05de225f1b, 0x00001b3a59c3093b },	/* 2.266 */
	{ 168, 512, 0xfd299a99f9f2abdd, 0x00001bb6f1a10799 },	/* 2.304 */
	{ 169, 512, 0xdda239e798fe9fd4, 0x00001bfae0c9692d },	/* 2.218 */
	{ 170, 512, 0x5fca670414a32c3e, 0x00001c22129dbcff },	/* 2.377 */
	{ 171, 512, 0x1bb8934314b087de, 0x00001c955db36cd0 },	/* 2.155 */
	{ 172, 512, 0xd96394b4b082200d, 0x00001cfc8619b7e6 },	/* 2.404 */
	{ 173, 512, 0xb612a7735b1c8cbc, 0x00001d303acdd585 },	/* 2.205 */
	{ 174, 512, 0x28e7430fe5875fe1, 0x00001d7ed5b3697d },	/* 2.359 */
	{ 175, 512, 0x5038e89efdd981b9, 0x00001dc40ec35c59 },	/* 2.158 */
	{ 176, 512, 0x075fd78f1d14db7c, 0x00001e31c83b4a2b },	/* 2.614 */
	{ 177, 512, 0xc50fafdb5021be15, 0x00001e7cdac82fbc },	/* 2.239 */
	{ 178, 512, 0xe6dc7572ce7b91c7, 0x00001edd8bb454fc },	/* 2.493 */
	{ 179, 512, 0x21f7843e7beda537, 0x00001f3a8e019d6c },	/* 2.327 */
	{ 180, 512, 0xc83385e20b43ec82, 0x00001f70735ec137 },	/* 2.231 */
	{ 181, 512, 0xca818217dddb21fd, 0x0000201ca44c5a3c },	/* 2.237 */
	{ 182, 512, 0xe6035defea48f933, 0x00002038e3346658 },	/* 2.691 */
	{ 183, 512, 0x47262a4f953dac5a, 0x000020c2e554314e },	/* 2.170 */
	{ 184, 512, 0xe24c7246260873ea, 0x000021197e618d64 },	/* 2.600 */
	{ 185, 512, 0xeef6b57c9b58e9e1, 0x0000217ea48ecddc },	/* 2.391 */
	{ 186, 512, 0x2becd3346e386142, 0x000021c496d4a5f9 },	/* 2.677 */
	{ 187, 512, 0x63c6207bdf3b40a3, 0x0000220e0f2eec0c },	/* 2.410 */
	{ 188, 512, 0x3056ce8989767d4b, 0x0000228eb76cd137 },	/* 2.776 */
	{ 189, 512, 0x91af61c307cee780, 0x000022e17e2ea501 },	/* 2.266 */
	{ 190, 512, 0xda359da225f6d54f, 0x00002358a2debc19 },	/* 2.717 */
	{ 191, 512, 0x0a5f7a2a55607ba0, 0x0000238a79dac18c },	/* 2.474 */
	{ 192, 512, 0x27bb75bf5224638a, 0x00002403a58e2351 },	/* 2.673 */
	{ 193, 512, 0x1ebfdb94630f5d0f, 0x00002492a10cb339 },	/* 2.420 */
	{ 194, 512, 0x6eae5e51d9c5f6fb, 0x000024ce4bf98715 },	/* 2.898 */
	{ 195, 512, 0x08d903b4daedc2e0, 0x0000250d1e15886c },	/* 2.363 */
	{ 196, 512, 0xc722a2f7fa7cd686, 0x0000258a99ed0c9e },	/* 2.747 */
	{ 197, 512, 0x8f71faf0e54e361d, 0x000025dee11976f5 },	/* 2.531 */
	{ 198, 512, 0x87f64695c91a54e7, 0x0000264e00a43da0 },	/* 2.707 */
	{ 199, 512, 0xc719cbac2c336b92, 0x000026d327277ac1 },	/* 2.315 */
	{ 200, 512, 0xe7e647afaf771ade, 0x000027523a5c44bf },	/* 3.012 */
	{ 201, 512, 0x12d4b5c38ce8c946, 0x0000273898432545 },	/* 2.378 */
	{ 202, 512, 0xf2e0cd4067bdc94a, 0x000027e47bb2c935 },	/* 2.969 */
	{ 203, 512, 0x21b79f14d6d947d3, 0x0000281e64977f0d },	/* 2.594 */
	{ 204, 512, 0x515093f952f18cd6, 0x0000289691a473fd },	/* 2.763 */
	{ 205, 512, 0xd47b160a1b1022c8, 0x00002903e8b52411 },	/* 2.457 */
	{ 206, 512, 0xc02fc96684715a16, 0x0000297515608601 },	/* 3.057 */
	{ 207, 512, 0xef51e68efba72ed0, 0x000029ef73604804 },	/* 2.590 */
	{ 208, 512, 0x9e3be6e5448b4f33, 0x00002a2846ed074b },	/* 3.047 */
	{ 209, 512, 0x81d446c6d5fec063, 0x00002a92ca693455 },	/* 2.676 */
	{ 210, 512, 0xff215de8224e57d5, 0x00002b2271fe3729 },	/* 2.993 */
	{ 211, 512, 0xe2524d9ba8f69796, 0x00002b64b99c3ba2 },	/* 2.457 */
	{ 212, 512, 0xf6b28e26097b7e4b, 0x00002bd768b6e068 },	/* 3.182 */
	{ 213, 512, 0x893a487f30ce1644, 0x00002c67f722b4b2 },	/* 2.563 */
	{ 214, 512, 0x386566c3fc9871df, 0x00002cc1cf8b4037 },	/* 3.025 */
	{ 215, 512, 0x1e0ed78edf1f558a, 0x00002d3948d36c7f },	/* 2.730 */
	{ 216, 512, 0xe3bc20c31e61f113, 0x00002d6d6b12e025 },	/* 3.036 */
	{ 217, 512, 0xd6c3ad2e23021882, 0x00002deff7572241 },	/* 2.722 */
	{ 218, 512, 0xb4a9f95cf0f69c5a, 0x00002e67d537aa36 },	/* 3.356 */
	{ 219, 512, 0x6e98ed6f6c38e82f, 0x00002e9720626789 },	/* 2.697 */
	{ 220, 512, 0x2e01edba33fddac7, 0x00002f407c6b0198 },	/* 2.979 */
	{ 221, 512, 0x559d02e1f5f57ccc, 0x00002fb6a5ab4f24 },	/* 2.858 */
	{ 222, 512, 0xac18f5a916adcd8e, 0x0000304ae1c5c57e },	/* 3.258 */
	{ 223, 512, 0x15789fbaddb86f4b, 0x0000306f6e019c78 },	/* 2.693 */
	{ 224, 512, 0xf4a9c36d5bc4c408, 0x000030da40434213 },	/* 3.259 */
	{ 225, 512, 0xf640f90fd2727f44, 0x00003189ed37b90c },	/* 2.733 */
	{ 226, 512, 0xb5313d390d61884a, 0x000031e152616b37 },	/* 3.235 */
	{ 227, 512, 0x4bae6b3ce9160939, 0x0000321f40aeac42 },	/* 2.983 */
	{ 228, 512, 0x838c34480f1a66a1, 0x000032f389c0f78e },	/* 3.308 */
	{ 229, 512, 0xb1c4a52c8e3d6060, 0x0000330062a40284 },	/* 2.715 */
	{ 230, 512, 0xe0f1110c6d0ed822, 0x0000338be435644f },	/* 3.540 */
	{ 231, 512, 0x9f1a8ccdcea68d4b, 0x000034045a4e97e1 },	/* 2.779 */
	{ 232, 512, 0x3261ed62223f3099, 0x000034702cfc401c },	/* 3.084 */
	{ 233, 512, 0xf2191e2311022d65, 0x00003509dd19c9fc },	/* 2.987 */
	{ 234, 512, 0xf102a395c2033abc, 0x000035654dc96fae },	/* 3.341 */
	{ 235, 512, 0x11fe378f027906b6, 0x000035b5193b0264 },	/* 2.793 */
	{ 236, 512, 0xf777f2c026b337aa, 0x000036704f5d9297 },	/* 3.518 */
	{ 237, 512, 0x1b04e9c2ee143f32, 0x000036dfbb7af218 },	/* 2.962 */
	{ 238, 512, 0x2fcec95266f9352c, 0x00003785c8df24a9 },	/* 3.196 */
	{ 239, 512, 0xfe2b0e47e427dd85, 0x000037cbdf5da729 },	/* 2.914 */
	{ 240, 512, 0x72b49bf2225f6c6d, 0x0000382227c15855 },	/* 3.408 */
	{ 241, 512, 0x50486b43df7df9c7, 0x0000389b88be6453 },	/* 2.903 */
	{ 242, 512, 0x5192a3e53181c8ab, 0x000038ddf3d67263 },	/* 3.778 */
	{ 243, 512, 0xe9f5d8365296fd5e, 0x0000399f1c6c9e9c },	/* 3.026 */
	{ 244, 512, 0xc740263f0301efa8, 0x00003a147146512d },	/* 3.347 */
	{ 245, 512, 0x23cd0f2b5671e67d, 0x00003ab10bcc0d9d },	/* 3.212 */
	{ 246, 512, 0x002ccc7e5cd41390, 0x00003ad6cd14a6c0 },	/* 3.482 */
	{ 247, 512, 0x9aafb3c02544b31b, 0x00003b8cb8779fb0 },	/* 3.146 */
	{ 248, 512, 0x72ba07a78b121999, 0x00003c24142a5a3f },	/* 3.626 */
	{ 249, 512, 0x3d784aa58edfc7b4, 0x00003cd084817d99 },	/* 2.952 */
	{ 250, 512, 0xaab750424d8004af, 0x00003d506a8e098e },	/* 3.463 */
	{ 251, 512, 0x84403fcf8e6b5ca2, 0x00003d4c54c2aec4 },	/* 3.131 */
	{ 252, 512, 0x71eb7455ec98e207, 0x00003e655715cf2c },	/* 3.538 */
	{ 253, 512, 0xd752b4f19301595b, 0x00003ecd7b2ca5ac },	/* 2.974 */
	{ 254, 512, 0xc4674129750499de, 0x00003e99e86d3e95 },	/* 3.843 */
	{ 255, 512, 0x9772baff5cd12ef5, 0x00003f895c019841 },	/* 3.088 */
};

/*
 * Verify the map is valid. Each device index must appear exactly
 * once in every row, and the permutation array checksum must match.
 */
static int
verify_perms(uint8_t *perms, uint64_t children, uint64_t nperms,
    uint64_t checksum)
{
	int countssz = sizeof (uint16_t) * children;
	uint16_t *counts = kmem_zalloc(countssz, KM_SLEEP);

	for (int i = 0; i < nperms; i++) {
		for (int j = 0; j < children; j++) {
			uint8_t val = perms[(i * children) + j];

			if (val >= children || counts[val] != i) {
				kmem_free(counts, countssz);
				return (EINVAL);
			}

			counts[val]++;
		}
	}

	if (checksum != 0) {
		int permssz = sizeof (uint8_t) * children * nperms;
		zio_cksum_t cksum;

		fletcher_4_native_varsize(perms, permssz, &cksum);

		if (checksum != cksum.zc_word[0]) {
			kmem_free(counts, countssz);
			return (ECKSUM);
		}
	}

	kmem_free(counts, countssz);

	return (0);
}

/*
 * Generate the permutation array for the draid_map_t.  These maps control
 * the placement of all data in a dRAID.  Therefore it's critical that the
 * seed always generates the same mapping.  We provide our own pseudo-random
 * number generator for this purpose.
 */
int
vdev_draid_generate_perms(const draid_map_t *map, uint8_t **permsp)
{
	VERIFY3U(map->dm_children, >=, VDEV_DRAID_MIN_CHILDREN);
	VERIFY3U(map->dm_children, <=, VDEV_DRAID_MAX_CHILDREN);
	VERIFY3U(map->dm_seed, !=, 0);
	VERIFY3U(map->dm_nperms, !=, 0);
	VERIFY3P(map->dm_perms, ==, NULL);

#ifdef _KERNEL
	/*
	 * The kernel code always provides both a map_seed and checksum.
	 * Only the tests/zfs-tests/cmd/draid/draid.c utility will provide
	 * a zero checksum when generating new candidate maps.
	 */
	VERIFY3U(map->dm_checksum, !=, 0);
#endif
	uint64_t children = map->dm_children;
	uint64_t nperms = map->dm_nperms;
	int rowsz = sizeof (uint8_t) * children;
	int permssz = rowsz * nperms;
	uint8_t *perms;

	/* Allocate the permutation array */
	perms = vmem_alloc(permssz, KM_SLEEP);

	/* Setup an initial row with a known pattern */
	uint8_t *initial_row = kmem_alloc(rowsz, KM_SLEEP);
	for (int i = 0; i < children; i++)
		initial_row[i] = i;

	uint64_t draid_seed[2] = { VDEV_DRAID_SEED, map->dm_seed };
	uint8_t *current_row, *previous_row = initial_row;

	/*
	 * Perform a Fisher-Yates shuffle of each row using the previous
	 * row as the starting point.  An initial_row with known pattern
	 * is used as the input for the first row.
	 */
	for (int i = 0; i < nperms; i++) {
		current_row = &perms[i * children];
		memcpy(current_row, previous_row, rowsz);

		for (int j = children - 1; j > 0; j--) {
			uint64_t k = vdev_draid_rand(draid_seed) % (j + 1);
			uint8_t val = current_row[j];
			current_row[j] = current_row[k];
			current_row[k] = val;
		}

		previous_row = current_row;
	}

	kmem_free(initial_row, rowsz);

	int error = verify_perms(perms, children, nperms, map->dm_checksum);
	if (error) {
		vmem_free(perms, permssz);
		return (error);
	}

	*permsp = perms;

	return (0);
}

/*
 * Lookup the fixed draid_map_t for the requested number of children.
 */
int
vdev_draid_lookup_map(uint64_t children, const draid_map_t **mapp)
{
	for (int i = 0; i <= VDEV_DRAID_MAX_MAPS; i++) {
		if (draid_maps[i].dm_children == children) {
			*mapp = &draid_maps[i];
			return (0);
		}
	}

	return (ENOENT);
}

/*
 * Lookup the permutation array and iteration id for the provided offset.
 */
static void
vdev_draid_get_perm(vdev_draid_config_t *vdc, uint64_t pindex,
    uint8_t **base, uint64_t *iter)
{
	uint64_t ncols = vdc->vdc_children;
	uint64_t poff = pindex % (vdc->vdc_nperms * ncols);

	*base = vdc->vdc_perms + (poff / ncols) * ncols;
	*iter = poff % ncols;
}

static inline uint64_t
vdev_draid_permute_id(vdev_draid_config_t *vdc,
    uint8_t *base, uint64_t iter, uint64_t index)
{
	return ((base[index] + iter) % vdc->vdc_children);
}

/*
 * Return the asize which is the psize rounded up to a full group width.
 * i.e. vdev_draid_psize_to_asize().
 */
static uint64_t
vdev_draid_asize(vdev_t *vd, uint64_t psize)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;
	uint64_t ashift = vd->vdev_ashift;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	uint64_t rows = ((psize - 1) / (vdc->vdc_ndata << ashift)) + 1;
	uint64_t asize = (rows * vdc->vdc_groupwidth) << ashift;

	ASSERT3U(asize, !=, 0);
	ASSERT3U(asize % (vdc->vdc_groupwidth), ==, 0);

	return (asize);
}

/*
 * Deflate the asize to the psize, this includes stripping parity.
 */
uint64_t
vdev_draid_asize_to_psize(vdev_t *vd, uint64_t asize)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT0(asize % vdc->vdc_groupwidth);

	return ((asize / vdc->vdc_groupwidth) * vdc->vdc_ndata);
}

/*
 * Convert a logical offset to the corresponding group number.
 */
static uint64_t
vdev_draid_offset_to_group(vdev_t *vd, uint64_t offset)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	return (offset / vdc->vdc_groupsz);
}

/*
 * Convert a group number to the logical starting offset for that group.
 */
static uint64_t
vdev_draid_group_to_offset(vdev_t *vd, uint64_t group)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	return (group * vdc->vdc_groupsz);
}

/*
 * Full stripe writes.  When writing, all columns (D+P) are required.  Parity
 * is calculated over all the columns, including empty zero filled sectors,
 * and each is written to disk.  While only the data columns are needed for
 * a normal read, all of the columns are required for reconstruction when
 * performing a sequential resilver.
 *
 * For "big columns" it's sufficient to map the correct range of the zio ABD.
 * Partial columns require allocating a gang ABD in order to zero fill the
 * empty sectors.  When the column is empty a zero filled sector must be
 * mapped.  In all cases the data ABDs must be the same size as the parity
 * ABDs (e.g. rc->rc_size == parity_size).
 */
static void
vdev_draid_map_alloc_write(zio_t *zio, uint64_t abd_offset, raidz_row_t *rr)
{
	uint64_t skip_size = 1ULL << zio->io_vd->vdev_top->vdev_ashift;
	uint64_t parity_size = rr->rr_col[0].rc_size;
	uint64_t abd_off = abd_offset;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
	ASSERT3U(parity_size, ==, abd_get_size(rr->rr_col[0].rc_abd));

	for (uint64_t c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size == 0) {
			/* empty data column (small write), add a skip sector */
			ASSERT3U(skip_size, ==, parity_size);
			rc->rc_abd = abd_get_zeros(skip_size);
		} else if (rc->rc_size == parity_size) {
			/* this is a "big column" */
			rc->rc_abd = abd_get_offset_struct(&rc->rc_abdstruct,
			    zio->io_abd, abd_off, rc->rc_size);
		} else {
			/* short data column, add a skip sector */
			ASSERT3U(rc->rc_size + skip_size, ==, parity_size);
			rc->rc_abd = abd_alloc_gang();
			abd_gang_add(rc->rc_abd, abd_get_offset_size(
			    zio->io_abd, abd_off, rc->rc_size), B_TRUE);
			abd_gang_add(rc->rc_abd, abd_get_zeros(skip_size),
			    B_TRUE);
		}

		ASSERT3U(abd_get_size(rc->rc_abd), ==, parity_size);

		abd_off += rc->rc_size;
		rc->rc_size = parity_size;
	}

	IMPLY(abd_offset != 0, abd_off == zio->io_size);
}

/*
 * Scrub/resilver reads.  In order to store the contents of the skip sectors
 * an additional ABD is allocated.  The columns are handled in the same way
 * as a full stripe write except instead of using the zero ABD the newly
 * allocated skip ABD is used to back the skip sectors.  In all cases the
 * data ABD must be the same size as the parity ABDs.
 */
static void
vdev_draid_map_alloc_scrub(zio_t *zio, uint64_t abd_offset, raidz_row_t *rr)
{
	uint64_t skip_size = 1ULL << zio->io_vd->vdev_top->vdev_ashift;
	uint64_t parity_size = rr->rr_col[0].rc_size;
	uint64_t abd_off = abd_offset;
	uint64_t skip_off = 0;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);
	ASSERT3P(rr->rr_abd_empty, ==, NULL);

	if (rr->rr_nempty > 0) {
		rr->rr_abd_empty = abd_alloc_linear(rr->rr_nempty * skip_size,
		    B_FALSE);
	}

	for (uint64_t c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size == 0) {
			/* empty data column (small read), add a skip sector */
			ASSERT3U(skip_size, ==, parity_size);
			ASSERT3U(rr->rr_nempty, !=, 0);
			rc->rc_abd = abd_get_offset_size(rr->rr_abd_empty,
			    skip_off, skip_size);
			skip_off += skip_size;
		} else if (rc->rc_size == parity_size) {
			/* this is a "big column" */
			rc->rc_abd = abd_get_offset_struct(&rc->rc_abdstruct,
			    zio->io_abd, abd_off, rc->rc_size);
		} else {
			/* short data column, add a skip sector */
			ASSERT3U(rc->rc_size + skip_size, ==, parity_size);
			ASSERT3U(rr->rr_nempty, !=, 0);
			rc->rc_abd = abd_alloc_gang();
			abd_gang_add(rc->rc_abd, abd_get_offset_size(
			    zio->io_abd, abd_off, rc->rc_size), B_TRUE);
			abd_gang_add(rc->rc_abd, abd_get_offset_size(
			    rr->rr_abd_empty, skip_off, skip_size), B_TRUE);
			skip_off += skip_size;
		}

		uint64_t abd_size = abd_get_size(rc->rc_abd);
		ASSERT3U(abd_size, ==, abd_get_size(rr->rr_col[0].rc_abd));

		/*
		 * Increase rc_size so the skip ABD is included in subsequent
		 * parity calculations.
		 */
		abd_off += rc->rc_size;
		rc->rc_size = abd_size;
	}

	IMPLY(abd_offset != 0, abd_off == zio->io_size);
	ASSERT3U(skip_off, ==, rr->rr_nempty * skip_size);
}

/*
 * Normal reads.  In this common case only the columns containing data
 * are read in to the zio ABDs.  Neither the parity columns or empty skip
 * sectors are read unless the checksum fails verification.  In which case
 * vdev_raidz_read_all() will call vdev_draid_map_alloc_empty() to expand
 * the raid map in order to allow reconstruction using the parity data and
 * skip sectors.
 */
static void
vdev_draid_map_alloc_read(zio_t *zio, uint64_t abd_offset, raidz_row_t *rr)
{
	uint64_t abd_off = abd_offset;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	for (uint64_t c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size > 0) {
			rc->rc_abd = abd_get_offset_struct(&rc->rc_abdstruct,
			    zio->io_abd, abd_off, rc->rc_size);
			abd_off += rc->rc_size;
		}
	}

	IMPLY(abd_offset != 0, abd_off == zio->io_size);
}

/*
 * Converts a normal "read" raidz_row_t to a "scrub" raidz_row_t. The key
 * difference is that an ABD is allocated to back skip sectors so they may
 * be read in to memory, verified, and repaired if needed.
 */
void
vdev_draid_map_alloc_empty(zio_t *zio, raidz_row_t *rr)
{
	uint64_t skip_size = 1ULL << zio->io_vd->vdev_top->vdev_ashift;
	uint64_t parity_size = rr->rr_col[0].rc_size;
	uint64_t skip_off = 0;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);
	ASSERT3P(rr->rr_abd_empty, ==, NULL);

	if (rr->rr_nempty > 0) {
		rr->rr_abd_empty = abd_alloc_linear(rr->rr_nempty * skip_size,
		    B_FALSE);
	}

	for (uint64_t c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size == 0) {
			/* empty data column (small read), add a skip sector */
			ASSERT3U(skip_size, ==, parity_size);
			ASSERT3U(rr->rr_nempty, !=, 0);
			ASSERT3P(rc->rc_abd, ==, NULL);
			rc->rc_abd = abd_get_offset_size(rr->rr_abd_empty,
			    skip_off, skip_size);
			skip_off += skip_size;
		} else if (rc->rc_size == parity_size) {
			/* this is a "big column", nothing to add */
			ASSERT3P(rc->rc_abd, !=, NULL);
		} else {
			/*
			 * short data column, add a skip sector and clear
			 * rc_tried to force the entire column to be re-read
			 * thereby including the missing skip sector data
			 * which is needed for reconstruction.
			 */
			ASSERT3U(rc->rc_size + skip_size, ==, parity_size);
			ASSERT3U(rr->rr_nempty, !=, 0);
			ASSERT3P(rc->rc_abd, !=, NULL);
			ASSERT(!abd_is_gang(rc->rc_abd));
			abd_t *read_abd = rc->rc_abd;
			rc->rc_abd = abd_alloc_gang();
			abd_gang_add(rc->rc_abd, read_abd, B_TRUE);
			abd_gang_add(rc->rc_abd, abd_get_offset_size(
			    rr->rr_abd_empty, skip_off, skip_size), B_TRUE);
			skip_off += skip_size;
			rc->rc_tried = 0;
		}

		/*
		 * Increase rc_size so the empty ABD is included in subsequent
		 * parity calculations.
		 */
		rc->rc_size = parity_size;
	}

	ASSERT3U(skip_off, ==, rr->rr_nempty * skip_size);
}

/*
 * Given a logical address within a dRAID configuration, return the physical
 * address on the first drive in the group that this address maps to
 * (at position 'start' in permutation number 'perm').
 */
static uint64_t
vdev_draid_logical_to_physical(vdev_t *vd, uint64_t logical_offset,
    uint64_t *perm, uint64_t *start)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	/* b is the dRAID (parent) sector offset. */
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t b_offset = logical_offset >> ashift;

	/*
	 * The height of a row in units of the vdev's minimum sector size.
	 * This is the amount of data written to each disk of each group
	 * in a given permutation.
	 */
	uint64_t rowheight_sectors = VDEV_DRAID_ROWHEIGHT >> ashift;

	/*
	 * We cycle through a disk permutation every groupsz * ngroups chunk
	 * of address space. Note that ngroups * groupsz must be a multiple
	 * of the number of data drives (ndisks) in order to guarantee
	 * alignment. So, for example, if our row height is 16MB, our group
	 * size is 10, and there are 13 data drives in the draid, then ngroups
	 * will be 13, we will change permutation every 2.08GB and each
	 * disk will have 160MB of data per chunk.
	 */
	uint64_t groupwidth = vdc->vdc_groupwidth;
	uint64_t ngroups = vdc->vdc_ngroups;
	uint64_t ndisks = vdc->vdc_ndisks;

	/*
	 * groupstart is where the group this IO will land in "starts" in
	 * the permutation array.
	 */
	uint64_t group = logical_offset / vdc->vdc_groupsz;
	uint64_t groupstart = (group * groupwidth) % ndisks;
	ASSERT3U(groupstart + groupwidth, <=, ndisks + groupstart);
	*start = groupstart;

	/* b_offset is the sector offset within a group chunk */
	b_offset = b_offset % (rowheight_sectors * groupwidth);
	ASSERT0(b_offset % groupwidth);

	/*
	 * Find the starting byte offset on each child vdev:
	 * - within a permutation there are ngroups groups spread over the
	 *   rows, where each row covers a slice portion of the disk
	 * - each permutation has (groupwidth * ngroups) / ndisks rows
	 * - so each permutation covers rows * slice portion of the disk
	 * - so we need to find the row where this IO group target begins
	 */
	*perm = group / ngroups;
	uint64_t row = (*perm * ((groupwidth * ngroups) / ndisks)) +
	    (((group % ngroups) * groupwidth) / ndisks);

	return (((rowheight_sectors * row) +
	    (b_offset / groupwidth)) << ashift);
}

static uint64_t
vdev_draid_map_alloc_row(zio_t *zio, raidz_row_t **rrp, uint64_t io_offset,
    uint64_t abd_offset, uint64_t abd_size)
{
	vdev_t *vd = zio->io_vd;
	vdev_draid_config_t *vdc = vd->vdev_tsd;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t io_size = abd_size;
	uint64_t io_asize = vdev_draid_asize(vd, io_size);
	uint64_t group = vdev_draid_offset_to_group(vd, io_offset);
	uint64_t start_offset = vdev_draid_group_to_offset(vd, group + 1);

	/*
	 * Limit the io_size to the space remaining in the group.  A second
	 * row in the raidz_map_t is created for the remainder.
	 */
	if (io_offset + io_asize > start_offset) {
		io_size = vdev_draid_asize_to_psize(vd,
		    start_offset - io_offset);
	}

	/*
	 * At most a block may span the logical end of one group and the start
	 * of the next group. Therefore, at the end of a group the io_size must
	 * span the group width evenly and the remainder must be aligned to the
	 * start of the next group.
	 */
	IMPLY(abd_offset == 0 && io_size < zio->io_size,
	    (io_asize >> ashift) % vdc->vdc_groupwidth == 0);
	IMPLY(abd_offset != 0,
	    vdev_draid_group_to_offset(vd, group) == io_offset);

	/* Lookup starting byte offset on each child vdev */
	uint64_t groupstart, perm;
	uint64_t physical_offset = vdev_draid_logical_to_physical(vd,
	    io_offset, &perm, &groupstart);

	/*
	 * If there is less than groupwidth drives available after the group
	 * start, the group is going to wrap onto the next row. 'wrap' is the
	 * group disk number that starts on the next row.
	 */
	uint64_t ndisks = vdc->vdc_ndisks;
	uint64_t groupwidth = vdc->vdc_groupwidth;
	uint64_t wrap = groupwidth;

	if (groupstart + groupwidth > ndisks)
		wrap = ndisks - groupstart;

	/* The io size in units of the vdev's minimum sector size. */
	const uint64_t psize = io_size >> ashift;

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 */
	uint64_t q = psize / vdc->vdc_ndata;

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	uint64_t r = psize - q * vdc->vdc_ndata;

	/* The number of "big columns" - those which contain remainder data. */
	uint64_t bc = (r == 0 ? 0 : r + vdc->vdc_nparity);
	ASSERT3U(bc, <, groupwidth);

	/* The total number of data and parity sectors for this I/O. */
	uint64_t tot = psize + (vdc->vdc_nparity * (q + (r == 0 ? 0 : 1)));

	raidz_row_t *rr;
	rr = kmem_alloc(offsetof(raidz_row_t, rr_col[groupwidth]), KM_SLEEP);
	rr->rr_cols = groupwidth;
	rr->rr_scols = groupwidth;
	rr->rr_bigcols = bc;
	rr->rr_missingdata = 0;
	rr->rr_missingparity = 0;
	rr->rr_firstdatacol = vdc->vdc_nparity;
	rr->rr_abd_empty = NULL;
#ifdef ZFS_DEBUG
	rr->rr_offset = io_offset;
	rr->rr_size = io_size;
#endif
	*rrp = rr;

	uint8_t *base;
	uint64_t iter, asize = 0;
	vdev_draid_get_perm(vdc, perm, &base, &iter);
	for (uint64_t i = 0; i < groupwidth; i++) {
		raidz_col_t *rc = &rr->rr_col[i];
		uint64_t c = (groupstart + i) % ndisks;

		/* increment the offset if we wrap to the next row */
		if (i == wrap)
			physical_offset += VDEV_DRAID_ROWHEIGHT;

		rc->rc_devidx = vdev_draid_permute_id(vdc, base, iter, c);
		rc->rc_offset = physical_offset;
		rc->rc_abd = NULL;
		rc->rc_orig_data = NULL;
		rc->rc_error = 0;
		rc->rc_tried = 0;
		rc->rc_skipped = 0;
		rc->rc_force_repair = 0;
		rc->rc_allow_repair = 1;
		rc->rc_need_orig_restore = B_FALSE;

		if (q == 0 && i >= bc)
			rc->rc_size = 0;
		else if (i < bc)
			rc->rc_size = (q + 1) << ashift;
		else
			rc->rc_size = q << ashift;

		asize += rc->rc_size;
	}

	ASSERT3U(asize, ==, tot << ashift);
	rr->rr_nempty = roundup(tot, groupwidth) - tot;
	IMPLY(bc > 0, rr->rr_nempty == groupwidth - bc);

	/* Allocate buffers for the parity columns */
	for (uint64_t c = 0; c < rr->rr_firstdatacol; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		rc->rc_abd = abd_alloc_linear(rc->rc_size, B_FALSE);
	}

	/*
	 * Map buffers for data columns and allocate/map buffers for skip
	 * sectors.  There are three distinct cases for dRAID which are
	 * required to support sequential rebuild.
	 */
	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_draid_map_alloc_write(zio, abd_offset, rr);
	} else if ((rr->rr_nempty > 0) &&
	    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
		vdev_draid_map_alloc_scrub(zio, abd_offset, rr);
	} else {
		ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);
		vdev_draid_map_alloc_read(zio, abd_offset, rr);
	}

	return (io_size);
}

/*
 * Allocate the raidz mapping to be applied to the dRAID I/O.  The parity
 * calculations for dRAID are identical to raidz however there are a few
 * differences in the layout.
 *
 * - dRAID always allocates a full stripe width. Any extra sectors due
 *   this padding are zero filled and written to disk. They will be read
 *   back during a scrub or repair operation since they are included in
 *   the parity calculation. This property enables sequential resilvering.
 *
 * - When the block at the logical offset spans redundancy groups then two
 *   rows are allocated in the raidz_map_t. One row resides at the end of
 *   the first group and the other at the start of the following group.
 */
static raidz_map_t *
vdev_draid_map_alloc(zio_t *zio)
{
	raidz_row_t *rr[2];
	uint64_t abd_offset = 0;
	uint64_t abd_size = zio->io_size;
	uint64_t io_offset = zio->io_offset;
	uint64_t size;
	int nrows = 1;

	size = vdev_draid_map_alloc_row(zio, &rr[0], io_offset,
	    abd_offset, abd_size);
	if (size < abd_size) {
		vdev_t *vd = zio->io_vd;

		io_offset += vdev_draid_asize(vd, size);
		abd_offset += size;
		abd_size -= size;
		nrows++;

		ASSERT3U(io_offset, ==, vdev_draid_group_to_offset(
		    vd, vdev_draid_offset_to_group(vd, io_offset)));
		ASSERT3U(abd_offset, <, zio->io_size);
		ASSERT3U(abd_size, !=, 0);

		size = vdev_draid_map_alloc_row(zio, &rr[1],
		    io_offset, abd_offset, abd_size);
		VERIFY3U(size, ==, abd_size);
	}

	raidz_map_t *rm;
	rm = kmem_zalloc(offsetof(raidz_map_t, rm_row[nrows]), KM_SLEEP);
	rm->rm_ops = vdev_raidz_math_get_ops();
	rm->rm_nrows = nrows;
	rm->rm_row[0] = rr[0];
	if (nrows == 2)
		rm->rm_row[1] = rr[1];

	return (rm);
}

/*
 * Given an offset into a dRAID return the next group width aligned offset
 * which can be used to start an allocation.
 */
static uint64_t
vdev_draid_get_astart(vdev_t *vd, const uint64_t start)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	return (roundup(start, vdc->vdc_groupwidth << vd->vdev_ashift));
}

/*
 * Allocatable space for dRAID is (children - nspares) * sizeof(smallest child)
 * rounded down to the last full slice.  So each child must provide at least
 * 1 / (children - nspares) of its asize.
 */
static uint64_t
vdev_draid_min_asize(vdev_t *vd)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	return (VDEV_DRAID_REFLOW_RESERVE +
	    (vd->vdev_min_asize + vdc->vdc_ndisks - 1) / (vdc->vdc_ndisks));
}

/*
 * When using dRAID the minimum allocation size is determined by the number
 * of data disks in the redundancy group.  Full stripes are always used.
 */
static uint64_t
vdev_draid_min_alloc(vdev_t *vd)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	return (vdc->vdc_ndata << vd->vdev_ashift);
}

/*
 * Returns true if the txg range does not exist on any leaf vdev.
 *
 * A dRAID spare does not fit into the DTL model. While it has child vdevs
 * there is no redundancy among them, and the effective child vdev is
 * determined by offset. Essentially we do a vdev_dtl_reassess() on the
 * fly by replacing a dRAID spare with the child vdev under the offset.
 * Note that it is a recursive process because the child vdev can be
 * another dRAID spare and so on.
 */
boolean_t
vdev_draid_missing(vdev_t *vd, uint64_t physical_offset, uint64_t txg,
    uint64_t size)
{
	if (vd->vdev_ops == &vdev_spare_ops ||
	    vd->vdev_ops == &vdev_replacing_ops) {
		/*
		 * Check all of the readable children, if any child
		 * contains the txg range the data it is not missing.
		 */
		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];

			if (!vdev_readable(cvd))
				continue;

			if (!vdev_draid_missing(cvd, physical_offset,
			    txg, size))
				return (B_FALSE);
		}

		return (B_TRUE);
	}

	if (vd->vdev_ops == &vdev_draid_spare_ops) {
		/*
		 * When sequentially resilvering we don't have a proper
		 * txg range so instead we must presume all txgs are
		 * missing on this vdev until the resilver completes.
		 */
		if (vd->vdev_rebuild_txg != 0)
			return (B_TRUE);

		/*
		 * DTL_MISSING is set for all prior txgs when a resilver
		 * is started in spa_vdev_attach().
		 */
		if (vdev_dtl_contains(vd, DTL_MISSING, txg, size))
			return (B_TRUE);

		/*
		 * Consult the DTL on the relevant vdev. Either a vdev
		 * leaf or spare/replace mirror child may be returned so
		 * we must recursively call vdev_draid_missing_impl().
		 */
		vd = vdev_draid_spare_get_child(vd, physical_offset);
		if (vd == NULL)
			return (B_TRUE);

		return (vdev_draid_missing(vd, physical_offset,
		    txg, size));
	}

	return (vdev_dtl_contains(vd, DTL_MISSING, txg, size));
}

/*
 * Returns true if the txg is only partially replicated on the leaf vdevs.
 */
static boolean_t
vdev_draid_partial(vdev_t *vd, uint64_t physical_offset, uint64_t txg,
    uint64_t size)
{
	if (vd->vdev_ops == &vdev_spare_ops ||
	    vd->vdev_ops == &vdev_replacing_ops) {
		/*
		 * Check all of the readable children, if any child is
		 * missing the txg range then it is partially replicated.
		 */
		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];

			if (!vdev_readable(cvd))
				continue;

			if (vdev_draid_partial(cvd, physical_offset, txg, size))
				return (B_TRUE);
		}

		return (B_FALSE);
	}

	if (vd->vdev_ops == &vdev_draid_spare_ops) {
		/*
		 * When sequentially resilvering we don't have a proper
		 * txg range so instead we must presume all txgs are
		 * missing on this vdev until the resilver completes.
		 */
		if (vd->vdev_rebuild_txg != 0)
			return (B_TRUE);

		/*
		 * DTL_MISSING is set for all prior txgs when a resilver
		 * is started in spa_vdev_attach().
		 */
		if (vdev_dtl_contains(vd, DTL_MISSING, txg, size))
			return (B_TRUE);

		/*
		 * Consult the DTL on the relevant vdev. Either a vdev
		 * leaf or spare/replace mirror child may be returned so
		 * we must recursively call vdev_draid_missing_impl().
		 */
		vd = vdev_draid_spare_get_child(vd, physical_offset);
		if (vd == NULL)
			return (B_TRUE);

		return (vdev_draid_partial(vd, physical_offset, txg, size));
	}

	return (vdev_dtl_contains(vd, DTL_MISSING, txg, size));
}

/*
 * Determine if the vdev is readable at the given offset.
 */
boolean_t
vdev_draid_readable(vdev_t *vd, uint64_t physical_offset)
{
	if (vd->vdev_ops == &vdev_draid_spare_ops) {
		vd = vdev_draid_spare_get_child(vd, physical_offset);
		if (vd == NULL)
			return (B_FALSE);
	}

	if (vd->vdev_ops == &vdev_spare_ops ||
	    vd->vdev_ops == &vdev_replacing_ops) {

		for (int c = 0; c < vd->vdev_children; c++) {
			vdev_t *cvd = vd->vdev_child[c];

			if (!vdev_readable(cvd))
				continue;

			if (vdev_draid_readable(cvd, physical_offset))
				return (B_TRUE);
		}

		return (B_FALSE);
	}

	return (vdev_readable(vd));
}

/*
 * Returns the first distributed spare found under the provided vdev tree.
 */
static vdev_t *
vdev_draid_find_spare(vdev_t *vd)
{
	if (vd->vdev_ops == &vdev_draid_spare_ops)
		return (vd);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *svd = vdev_draid_find_spare(vd->vdev_child[c]);
		if (svd != NULL)
			return (svd);
	}

	return (NULL);
}

/*
 * Returns B_TRUE if the passed in vdev is currently "faulted".
 * Faulted, in this context, means that the vdev represents a
 * replacing or sparing vdev tree.
 */
static boolean_t
vdev_draid_faulted(vdev_t *vd, uint64_t physical_offset)
{
	if (vd->vdev_ops == &vdev_draid_spare_ops) {
		vd = vdev_draid_spare_get_child(vd, physical_offset);
		if (vd == NULL)
			return (B_FALSE);

		/*
		 * After resolving the distributed spare to a leaf vdev
		 * check the parent to determine if it's "faulted".
		 */
		vd = vd->vdev_parent;
	}

	return (vd->vdev_ops == &vdev_replacing_ops ||
	    vd->vdev_ops == &vdev_spare_ops);
}

/*
 * Determine if the dRAID block at the logical offset is degraded.
 * Used by sequential resilver.
 */
static boolean_t
vdev_draid_group_degraded(vdev_t *vd, uint64_t offset)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);
	ASSERT3U(vdev_draid_get_astart(vd, offset), ==, offset);

	uint64_t groupstart, perm;
	uint64_t physical_offset = vdev_draid_logical_to_physical(vd,
	    offset, &perm, &groupstart);

	uint8_t *base;
	uint64_t iter;
	vdev_draid_get_perm(vdc, perm, &base, &iter);

	for (uint64_t i = 0; i < vdc->vdc_groupwidth; i++) {
		uint64_t c = (groupstart + i) % vdc->vdc_ndisks;
		uint64_t cid = vdev_draid_permute_id(vdc, base, iter, c);
		vdev_t *cvd = vd->vdev_child[cid];

		/* Group contains a faulted vdev. */
		if (vdev_draid_faulted(cvd, physical_offset))
			return (B_TRUE);

		/*
		 * Always check groups with active distributed spares
		 * because any vdev failure in the pool will affect them.
		 */
		if (vdev_draid_find_spare(cvd) != NULL)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Determine if the txg is missing.  Used by healing resilver.
 */
static boolean_t
vdev_draid_group_missing(vdev_t *vd, uint64_t offset, uint64_t txg,
    uint64_t size)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);
	ASSERT3U(vdev_draid_get_astart(vd, offset), ==, offset);

	uint64_t groupstart, perm;
	uint64_t physical_offset = vdev_draid_logical_to_physical(vd,
	    offset, &perm, &groupstart);

	uint8_t *base;
	uint64_t iter;
	vdev_draid_get_perm(vdc, perm, &base, &iter);

	for (uint64_t i = 0; i < vdc->vdc_groupwidth; i++) {
		uint64_t c = (groupstart + i) % vdc->vdc_ndisks;
		uint64_t cid = vdev_draid_permute_id(vdc, base, iter, c);
		vdev_t *cvd = vd->vdev_child[cid];

		/* Transaction group is known to be partially replicated. */
		if (vdev_draid_partial(cvd, physical_offset, txg, size))
			return (B_TRUE);

		/*
		 * Always check groups with active distributed spares
		 * because any vdev failure in the pool will affect them.
		 */
		if (vdev_draid_find_spare(cvd) != NULL)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Find the smallest child asize and largest sector size to calculate the
 * available capacity.  Distributed spares are ignored since their capacity
 * is also based of the minimum child size in the top-level dRAID.
 */
static void
vdev_draid_calculate_asize(vdev_t *vd, uint64_t *asizep, uint64_t *max_asizep,
    uint64_t *logical_ashiftp, uint64_t *physical_ashiftp)
{
	uint64_t logical_ashift = 0, physical_ashift = 0;
	uint64_t asize = 0, max_asize = 0;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_ops == &vdev_draid_spare_ops)
			continue;

		asize = MIN(asize - 1, cvd->vdev_asize - 1) + 1;
		max_asize = MIN(max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		logical_ashift = MAX(logical_ashift, cvd->vdev_ashift);
		physical_ashift = MAX(physical_ashift,
		    cvd->vdev_physical_ashift);
	}

	*asizep = asize;
	*max_asizep = max_asize;
	*logical_ashiftp = logical_ashift;
	*physical_ashiftp = physical_ashift;
}

/*
 * Open spare vdevs.
 */
static boolean_t
vdev_draid_open_spares(vdev_t *vd)
{
	return (vd->vdev_ops == &vdev_draid_spare_ops ||
	    vd->vdev_ops == &vdev_replacing_ops ||
	    vd->vdev_ops == &vdev_spare_ops);
}

/*
 * Open all children, excluding spares.
 */
static boolean_t
vdev_draid_open_children(vdev_t *vd)
{
	return (!vdev_draid_open_spares(vd));
}

/*
 * Open a top-level dRAID vdev.
 */
static int
vdev_draid_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_draid_config_t *vdc =  vd->vdev_tsd;
	uint64_t nparity = vdc->vdc_nparity;
	int open_errors = 0;

	if (nparity > VDEV_DRAID_MAXPARITY ||
	    vd->vdev_children < nparity + 1) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * First open the normal children then the distributed spares.  This
	 * ordering is important to ensure the distributed spares calculate
	 * the correct psize in the event that the dRAID vdevs were expanded.
	 */
	vdev_open_children_subset(vd, vdev_draid_open_children);
	vdev_open_children_subset(vd, vdev_draid_open_spares);

	/* Verify enough of the children are available to continue. */
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c]->vdev_open_error != 0) {
			if ((++open_errors) > nparity) {
				vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
				return (SET_ERROR(ENXIO));
			}
		}
	}

	/*
	 * Allocatable capacity is the sum of the space on all children less
	 * the number of distributed spares rounded down to last full row
	 * and then to the last full group. An additional 32MB of scratch
	 * space is reserved at the end of each child for use by the dRAID
	 * expansion feature.
	 */
	uint64_t child_asize, child_max_asize;
	vdev_draid_calculate_asize(vd, &child_asize, &child_max_asize,
	    logical_ashift, physical_ashift);

	/*
	 * Should be unreachable since the minimum child size is 64MB, but
	 * we want to make sure an underflow absolutely cannot occur here.
	 */
	if (child_asize < VDEV_DRAID_REFLOW_RESERVE ||
	    child_max_asize < VDEV_DRAID_REFLOW_RESERVE) {
		return (SET_ERROR(ENXIO));
	}

	child_asize = ((child_asize - VDEV_DRAID_REFLOW_RESERVE) /
	    VDEV_DRAID_ROWHEIGHT) * VDEV_DRAID_ROWHEIGHT;
	child_max_asize = ((child_max_asize - VDEV_DRAID_REFLOW_RESERVE) /
	    VDEV_DRAID_ROWHEIGHT) * VDEV_DRAID_ROWHEIGHT;

	*asize = (((child_asize * vdc->vdc_ndisks) / vdc->vdc_groupsz) *
	    vdc->vdc_groupsz);
	*max_asize = (((child_max_asize * vdc->vdc_ndisks) / vdc->vdc_groupsz) *
	    vdc->vdc_groupsz);

	return (0);
}

/*
 * Close a top-level dRAID vdev.
 */
static void
vdev_draid_close(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] != NULL)
			vdev_close(vd->vdev_child[c]);
	}
}

/*
 * Return the maximum asize for a rebuild zio in the provided range
 * given the following constraints.  A dRAID chunks may not:
 *
 * - Exceed the maximum allowed block size (SPA_MAXBLOCKSIZE), or
 * - Span dRAID redundancy groups.
 */
static uint64_t
vdev_draid_rebuild_asize(vdev_t *vd, uint64_t start, uint64_t asize,
    uint64_t max_segment)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	uint64_t ashift = vd->vdev_ashift;
	uint64_t ndata = vdc->vdc_ndata;
	uint64_t psize = MIN(P2ROUNDUP(max_segment * ndata, 1 << ashift),
	    SPA_MAXBLOCKSIZE);

	ASSERT3U(vdev_draid_get_astart(vd, start), ==, start);
	ASSERT3U(asize % (vdc->vdc_groupwidth << ashift), ==, 0);

	/* Chunks must evenly span all data columns in the group. */
	psize = (((psize >> ashift) / ndata) * ndata) << ashift;
	uint64_t chunk_size = MIN(asize, vdev_psize_to_asize(vd, psize));

	/* Reduce the chunk size to the group space remaining. */
	uint64_t group = vdev_draid_offset_to_group(vd, start);
	uint64_t left = vdev_draid_group_to_offset(vd, group + 1) - start;
	chunk_size = MIN(chunk_size, left);

	ASSERT3U(chunk_size % (vdc->vdc_groupwidth << ashift), ==, 0);
	ASSERT3U(vdev_draid_offset_to_group(vd, start), ==,
	    vdev_draid_offset_to_group(vd, start + chunk_size - 1));

	return (chunk_size);
}

/*
 * Align the start of the metaslab to the group width and slightly reduce
 * its size to a multiple of the group width.  Since full stripe writes are
 * required by dRAID this space is unallocable.  Furthermore, aligning the
 * metaslab start is important for vdev initialize and TRIM which both operate
 * on metaslab boundaries which vdev_xlate() expects to be aligned.
 */
static void
vdev_draid_metaslab_init(vdev_t *vd, uint64_t *ms_start, uint64_t *ms_size)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);

	uint64_t sz = vdc->vdc_groupwidth << vd->vdev_ashift;
	uint64_t astart = vdev_draid_get_astart(vd, *ms_start);
	uint64_t asize = ((*ms_size - (astart - *ms_start)) / sz) * sz;

	*ms_start = astart;
	*ms_size = asize;

	ASSERT0(*ms_start % sz);
	ASSERT0(*ms_size % sz);
}

/*
 * Add virtual dRAID spares to the list of valid spares. In order to accomplish
 * this the existing array must be freed and reallocated with the additional
 * entries.
 */
int
vdev_draid_spare_create(nvlist_t *nvroot, vdev_t *vd, uint64_t *ndraidp,
    uint64_t next_vdev_id)
{
	uint64_t draid_nspares = 0;
	uint64_t ndraid = 0;
	int error;

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_t *cvd = vd->vdev_child[i];

		if (cvd->vdev_ops == &vdev_draid_ops) {
			vdev_draid_config_t *vdc = cvd->vdev_tsd;
			draid_nspares += vdc->vdc_nspares;
			ndraid++;
		}
	}

	if (draid_nspares == 0) {
		*ndraidp = ndraid;
		return (0);
	}

	nvlist_t **old_spares, **new_spares;
	uint_t old_nspares;
	error = nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &old_spares, &old_nspares);
	if (error)
		old_nspares = 0;

	/* Allocate memory and copy of the existing spares. */
	new_spares = kmem_alloc(sizeof (nvlist_t *) *
	    (draid_nspares + old_nspares), KM_SLEEP);
	for (uint_t i = 0; i < old_nspares; i++)
		new_spares[i] = fnvlist_dup(old_spares[i]);

	/* Add new distributed spares to ZPOOL_CONFIG_SPARES. */
	uint64_t n = old_nspares;
	for (uint64_t vdev_id = 0; vdev_id < vd->vdev_children; vdev_id++) {
		vdev_t *cvd = vd->vdev_child[vdev_id];
		char path[64];

		if (cvd->vdev_ops != &vdev_draid_ops)
			continue;

		vdev_draid_config_t *vdc = cvd->vdev_tsd;
		uint64_t nspares = vdc->vdc_nspares;
		uint64_t nparity = vdc->vdc_nparity;

		for (uint64_t spare_id = 0; spare_id < nspares; spare_id++) {
			bzero(path, sizeof (path));
			(void) snprintf(path, sizeof (path) - 1,
			    "%s%llu-%llu-%llu", VDEV_TYPE_DRAID,
			    (u_longlong_t)nparity,
			    (u_longlong_t)next_vdev_id + vdev_id,
			    (u_longlong_t)spare_id);

			nvlist_t *spare = fnvlist_alloc();
			fnvlist_add_string(spare, ZPOOL_CONFIG_PATH, path);
			fnvlist_add_string(spare, ZPOOL_CONFIG_TYPE,
			    VDEV_TYPE_DRAID_SPARE);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_TOP_GUID,
			    cvd->vdev_guid);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_SPARE_ID,
			    spare_id);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_IS_LOG, 0);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_IS_SPARE, 1);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_WHOLE_DISK, 1);
			fnvlist_add_uint64(spare, ZPOOL_CONFIG_ASHIFT,
			    cvd->vdev_ashift);

			new_spares[n] = spare;
			n++;
		}
	}

	if (n > 0) {
		(void) nvlist_remove_all(nvroot, ZPOOL_CONFIG_SPARES);
		fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    (const nvlist_t **)new_spares, n);
	}

	for (int i = 0; i < n; i++)
		nvlist_free(new_spares[i]);

	kmem_free(new_spares, sizeof (*new_spares) * n);
	*ndraidp = ndraid;

	return (0);
}

/*
 * Determine if any portion of the provided block resides on a child vdev
 * with a dirty DTL and therefore needs to be resilvered.
 */
static boolean_t
vdev_draid_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t asize = vdev_draid_asize(vd, psize);

	if (phys_birth == TXG_UNKNOWN) {
		/*
		 * Sequential resilver.  There is no meaningful phys_birth
		 * for this block, we can only determine if block resides
		 * in a degraded group in which case it must be resilvered.
		 */
		ASSERT3U(vdev_draid_offset_to_group(vd, offset), ==,
		    vdev_draid_offset_to_group(vd, offset + asize - 1));

		return (vdev_draid_group_degraded(vd, offset));
	} else {
		/*
		 * Healing resilver.  TXGs not in DTL_PARTIAL are intact,
		 * as are blocks in non-degraded groups.
		 */
		if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
			return (B_FALSE);

		if (vdev_draid_group_missing(vd, offset, phys_birth, 1))
			return (B_TRUE);

		/* The block may span groups in which case check both. */
		if (vdev_draid_offset_to_group(vd, offset) !=
		    vdev_draid_offset_to_group(vd, offset + asize - 1)) {
			if (vdev_draid_group_missing(vd,
			    offset + asize, phys_birth, 1))
				return (B_TRUE);
		}

		return (B_FALSE);
	}
}

static boolean_t
vdev_draid_rebuilding(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf && vd->vdev_rebuild_txg)
		return (B_TRUE);

	for (int i = 0; i < vd->vdev_children; i++) {
		if (vdev_draid_rebuilding(vd->vdev_child[i])) {
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

static void
vdev_draid_io_verify(vdev_t *vd, raidz_row_t *rr, int col)
{
#ifdef ZFS_DEBUG
	range_seg64_t logical_rs, physical_rs, remain_rs;
	logical_rs.rs_start = rr->rr_offset;
	logical_rs.rs_end = logical_rs.rs_start +
	    vdev_draid_asize(vd, rr->rr_size);

	raidz_col_t *rc = &rr->rr_col[col];
	vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

	vdev_xlate(cvd, &logical_rs, &physical_rs, &remain_rs);
	ASSERT(vdev_xlate_is_empty(&remain_rs));
	ASSERT3U(rc->rc_offset, ==, physical_rs.rs_start);
	ASSERT3U(rc->rc_offset, <, physical_rs.rs_end);
	ASSERT3U(rc->rc_offset + rc->rc_size, ==, physical_rs.rs_end);
#endif
}

/*
 * For write operations:
 * 1. Generate the parity data
 * 2. Create child zio write operations to each column's vdev, for both
 *    data and parity.  A gang ABD is allocated by vdev_draid_map_alloc()
 *    if a skip sector needs to be added to a column.
 */
static void
vdev_draid_io_start_write(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;
	raidz_map_t *rm = zio->io_vsd;

	vdev_raidz_generate_parity_row(rm, rr);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/*
		 * Empty columns are zero filled and included in the parity
		 * calculation and therefore must be written.
		 */
		ASSERT3U(rc->rc_size, !=, 0);

		/* Verify physical to logical translation */
		vdev_draid_io_verify(vd, rr, c);

		zio_nowait(zio_vdev_child_io(zio, NULL,
		    vd->vdev_child[rc->rc_devidx], rc->rc_offset,
		    rc->rc_abd, rc->rc_size, zio->io_type, zio->io_priority,
		    0, vdev_raidz_child_done, rc));
	}
}

/*
 * For read operations:
 * 1. The vdev_draid_map_alloc() function will create a minimal raidz
 *    mapping for the read based on the zio->io_flags.  There are two
 *    possible mappings either 1) a normal read, or 2) a scrub/resilver.
 * 2. Create the zio read operations.  This will include all parity
 *    columns and skip sectors for a scrub/resilver.
 */
static void
vdev_draid_io_start_read(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;

	/* Sequential rebuild must do IO at redundancy group boundary. */
	IMPLY(zio->io_priority == ZIO_PRIORITY_REBUILD, rr->rr_nempty == 0);

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last.  Any errors along the way will force us to read the parity.
	 * For scrub/resilver IOs which verify skip sectors, a gang ABD will
	 * have been allocated to store them and rc->rc_size is increased.
	 */
	for (int c = rr->rr_cols - 1; c >= 0; c--) {
		raidz_col_t *rc = &rr->rr_col[c];
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

		if (!vdev_draid_readable(cvd, rc->rc_offset)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ENXIO);
			rc->rc_tried = 1;
			rc->rc_skipped = 1;
			continue;
		}

		if (vdev_draid_missing(cvd, rc->rc_offset, zio->io_txg, 1)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ESTALE);
			rc->rc_skipped = 1;
			continue;
		}

		/*
		 * Empty columns may be read during vdev_draid_io_done().
		 * Only skip them after the readable and missing checks
		 * verify they are available.
		 */
		if (rc->rc_size == 0) {
			rc->rc_skipped = 1;
			continue;
		}

		if (zio->io_flags & ZIO_FLAG_RESILVER) {
			vdev_t *svd;

			/*
			 * Sequential rebuilds need to always consider the data
			 * on the child being rebuilt to be stale.  This is
			 * important when all columns are available to aid
			 * known reconstruction in identifing which columns
			 * contain incorrect data.
			 *
			 * Furthermore, all repairs need to be constrained to
			 * the devices being rebuilt because without a checksum
			 * we cannot verify the data is actually correct and
			 * performing an incorrect repair could result in
			 * locking in damage and making the data unrecoverable.
			 */
			if (zio->io_priority == ZIO_PRIORITY_REBUILD) {
				if (vdev_draid_rebuilding(cvd)) {
					if (c >= rr->rr_firstdatacol)
						rr->rr_missingdata++;
					else
						rr->rr_missingparity++;
					rc->rc_error = SET_ERROR(ESTALE);
					rc->rc_skipped = 1;
					rc->rc_allow_repair = 1;
					continue;
				} else {
					rc->rc_allow_repair = 0;
				}
			} else {
				rc->rc_allow_repair = 1;
			}

			/*
			 * If this child is a distributed spare then the
			 * offset might reside on the vdev being replaced.
			 * In which case this data must be written to the
			 * new device.  Failure to do so would result in
			 * checksum errors when the old device is detached
			 * and the pool is scrubbed.
			 */
			if ((svd = vdev_draid_find_spare(cvd)) != NULL) {
				svd = vdev_draid_spare_get_child(svd,
				    rc->rc_offset);
				if (svd && (svd->vdev_ops == &vdev_spare_ops ||
				    svd->vdev_ops == &vdev_replacing_ops)) {
					rc->rc_force_repair = 1;

					if (vdev_draid_rebuilding(svd))
						rc->rc_allow_repair = 1;
				}
			}

			/*
			 * Always issue a repair IO to this child when its
			 * a spare or replacing vdev with an active rebuild.
			 */
			if ((cvd->vdev_ops == &vdev_spare_ops ||
			    cvd->vdev_ops == &vdev_replacing_ops) &&
			    vdev_draid_rebuilding(cvd)) {
				rc->rc_force_repair = 1;
				rc->rc_allow_repair = 1;
			}
		}
	}

	/*
	 * Either a parity or data column is missing this means a repair
	 * may be attempted by vdev_draid_io_done().  Expand the raid map
	 * to read in empty columns which are needed along with the parity
	 * during reconstruction.
	 */
	if ((rr->rr_missingdata > 0 || rr->rr_missingparity > 0) &&
	    rr->rr_nempty > 0 && rr->rr_abd_empty == NULL) {
		vdev_draid_map_alloc_empty(zio, rr);
	}

	for (int c = rr->rr_cols - 1; c >= 0; c--) {
		raidz_col_t *rc = &rr->rr_col[c];
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

		if (rc->rc_error || rc->rc_size == 0)
			continue;

		if (c >= rr->rr_firstdatacol || rr->rr_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}
}

/*
 * Start an IO operation to a dRAID vdev.
 */
static void
vdev_draid_io_start(zio_t *zio)
{
	vdev_t *vd __maybe_unused = zio->io_vd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);
	ASSERT3U(zio->io_offset, ==, vdev_draid_get_astart(vd, zio->io_offset));

	raidz_map_t *rm = vdev_draid_map_alloc(zio);
	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;

	if (zio->io_type == ZIO_TYPE_WRITE) {
		for (int i = 0; i < rm->rm_nrows; i++) {
			vdev_draid_io_start_write(zio, rm->rm_row[i]);
		}
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_READ);

		for (int i = 0; i < rm->rm_nrows; i++) {
			vdev_draid_io_start_read(zio, rm->rm_row[i]);
		}
	}

	zio_execute(zio);
}

/*
 * Complete an IO operation on a dRAID vdev.  The raidz logic can be applied
 * to dRAID since the layout is fully described by the raidz_map_t.
 */
static void
vdev_draid_io_done(zio_t *zio)
{
	vdev_raidz_io_done(zio);
}

static void
vdev_draid_state_change(vdev_t *vd, int faulted, int degraded)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;
	ASSERT(vd->vdev_ops == &vdev_draid_ops);

	if (faulted > vdc->vdc_nparity)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

static void
vdev_draid_xlate(vdev_t *cvd, const range_seg64_t *logical_rs,
    range_seg64_t *physical_rs, range_seg64_t *remain_rs)
{
	vdev_t *raidvd = cvd->vdev_parent;
	ASSERT(raidvd->vdev_ops == &vdev_draid_ops);

	vdev_draid_config_t *vdc = raidvd->vdev_tsd;
	uint64_t ashift = raidvd->vdev_top->vdev_ashift;

	/* Make sure the offsets are block-aligned */
	ASSERT0(logical_rs->rs_start % (1 << ashift));
	ASSERT0(logical_rs->rs_end % (1 << ashift));

	uint64_t logical_start = logical_rs->rs_start;
	uint64_t logical_end = logical_rs->rs_end;

	/*
	 * Unaligned ranges must be skipped. All metaslabs are correctly
	 * aligned so this should not happen, but this case is handled in
	 * case it's needed by future callers.
	 */
	uint64_t astart = vdev_draid_get_astart(raidvd, logical_start);
	if (astart != logical_start) {
		physical_rs->rs_start = logical_start;
		physical_rs->rs_end = logical_start;
		remain_rs->rs_start = MIN(astart, logical_end);
		remain_rs->rs_end = logical_end;
		return;
	}

	/*
	 * Unlike with mirrors and raidz a dRAID logical range can map
	 * to multiple non-contiguous physical ranges. This is handled by
	 * limiting the size of the logical range to a single group and
	 * setting the remain argument such that it describes the remaining
	 * unmapped logical range. This is stricter than absolutely
	 * necessary but helps simplify the logic below.
	 */
	uint64_t group = vdev_draid_offset_to_group(raidvd, logical_start);
	uint64_t nextstart = vdev_draid_group_to_offset(raidvd, group + 1);
	if (logical_end > nextstart)
		logical_end = nextstart;

	/* Find the starting offset for each vdev in the group */
	uint64_t perm, groupstart;
	uint64_t start = vdev_draid_logical_to_physical(raidvd,
	    logical_start, &perm, &groupstart);
	uint64_t end = start;

	uint8_t *base;
	uint64_t iter, id;
	vdev_draid_get_perm(vdc, perm, &base, &iter);

	/*
	 * Check if the passed child falls within the group.  If it does
	 * update the start and end to reflect the physical range.
	 * Otherwise, leave them unmodified which will result in an empty
	 * (zero-length) physical range being returned.
	 */
	for (uint64_t i = 0; i < vdc->vdc_groupwidth; i++) {
		uint64_t c = (groupstart + i) % vdc->vdc_ndisks;

		if (c == 0 && i != 0) {
			/* the group wrapped, increment the start */
			start += VDEV_DRAID_ROWHEIGHT;
			end = start;
		}

		id = vdev_draid_permute_id(vdc, base, iter, c);
		if (id == cvd->vdev_id) {
			uint64_t b_size = (logical_end >> ashift) -
			    (logical_start >> ashift);
			ASSERT3U(b_size, >, 0);
			end = start + ((((b_size - 1) /
			    vdc->vdc_groupwidth) + 1) << ashift);
			break;
		}
	}
	physical_rs->rs_start = start;
	physical_rs->rs_end = end;

	/*
	 * Only top-level vdevs are allowed to set remain_rs because
	 * when .vdev_op_xlate() is called for their children the full
	 * logical range is not provided by vdev_xlate().
	 */
	remain_rs->rs_start = logical_end;
	remain_rs->rs_end = logical_rs->rs_end;

	ASSERT3U(physical_rs->rs_start, <=, logical_start);
	ASSERT3U(physical_rs->rs_end - physical_rs->rs_start, <=,
	    logical_end - logical_start);
}

/*
 * Add dRAID specific fields to the config nvlist.
 */
static void
vdev_draid_config_generate(vdev_t *vd, nvlist_t *nv)
{
	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, vdc->vdc_nparity);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA, vdc->vdc_ndata);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NSPARES, vdc->vdc_nspares);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NGROUPS, vdc->vdc_ngroups);
}

/*
 * Initialize private dRAID specific fields from the nvlist.
 */
static int
vdev_draid_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	uint64_t ndata, nparity, nspares, ngroups;
	int error;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA, &ndata))
		return (SET_ERROR(EINVAL));

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY, &nparity) ||
	    nparity == 0 || nparity > VDEV_DRAID_MAXPARITY) {
		return (SET_ERROR(EINVAL));
	}

	uint_t children;
	nvlist_t **child;
	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0 || children == 0 ||
	    children > VDEV_DRAID_MAX_CHILDREN) {
		return (SET_ERROR(EINVAL));
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NSPARES, &nspares) ||
	    nspares > 100 || nspares > (children - (ndata + nparity))) {
		return (SET_ERROR(EINVAL));
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NGROUPS, &ngroups) ||
	    ngroups == 0 || ngroups > VDEV_DRAID_MAX_CHILDREN) {
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Validate the minimum number of children exist per group for the
	 * specified parity level (draid1 >= 2, draid2 >= 3, draid3 >= 4).
	 */
	if (children < (ndata + nparity + nspares))
		return (SET_ERROR(EINVAL));

	/*
	 * Create the dRAID configuration using the pool nvlist configuration
	 * and the fixed mapping for the correct number of children.
	 */
	vdev_draid_config_t *vdc;
	const draid_map_t *map;

	error = vdev_draid_lookup_map(children, &map);
	if (error)
		return (SET_ERROR(EINVAL));

	vdc = kmem_zalloc(sizeof (*vdc), KM_SLEEP);
	vdc->vdc_ndata = ndata;
	vdc->vdc_nparity = nparity;
	vdc->vdc_nspares = nspares;
	vdc->vdc_children = children;
	vdc->vdc_ngroups = ngroups;
	vdc->vdc_nperms = map->dm_nperms;

	error = vdev_draid_generate_perms(map, &vdc->vdc_perms);
	if (error) {
		kmem_free(vdc, sizeof (*vdc));
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Derived constants.
	 */
	vdc->vdc_groupwidth = vdc->vdc_ndata + vdc->vdc_nparity;
	vdc->vdc_ndisks = vdc->vdc_children - vdc->vdc_nspares;
	vdc->vdc_groupsz = vdc->vdc_groupwidth * VDEV_DRAID_ROWHEIGHT;
	vdc->vdc_devslicesz = (vdc->vdc_groupsz * vdc->vdc_ngroups) /
	    vdc->vdc_ndisks;

	ASSERT3U(vdc->vdc_groupwidth, >=, 2);
	ASSERT3U(vdc->vdc_groupwidth, <=, vdc->vdc_ndisks);
	ASSERT3U(vdc->vdc_groupsz, >=, 2 * VDEV_DRAID_ROWHEIGHT);
	ASSERT3U(vdc->vdc_devslicesz, >=, VDEV_DRAID_ROWHEIGHT);
	ASSERT3U(vdc->vdc_devslicesz % VDEV_DRAID_ROWHEIGHT, ==, 0);
	ASSERT3U((vdc->vdc_groupwidth * vdc->vdc_ngroups) %
	    vdc->vdc_ndisks, ==, 0);

	*tsd = vdc;

	return (0);
}

static void
vdev_draid_fini(vdev_t *vd)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	vmem_free(vdc->vdc_perms, sizeof (uint8_t) *
	    vdc->vdc_children * vdc->vdc_nperms);
	kmem_free(vdc, sizeof (*vdc));
}

static uint64_t
vdev_draid_nparity(vdev_t *vd)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	return (vdc->vdc_nparity);
}

static uint64_t
vdev_draid_ndisks(vdev_t *vd)
{
	vdev_draid_config_t *vdc = vd->vdev_tsd;

	return (vdc->vdc_ndisks);
}

vdev_ops_t vdev_draid_ops = {
	.vdev_op_init = vdev_draid_init,
	.vdev_op_fini = vdev_draid_fini,
	.vdev_op_open = vdev_draid_open,
	.vdev_op_close = vdev_draid_close,
	.vdev_op_asize = vdev_draid_asize,
	.vdev_op_min_asize = vdev_draid_min_asize,
	.vdev_op_min_alloc = vdev_draid_min_alloc,
	.vdev_op_io_start = vdev_draid_io_start,
	.vdev_op_io_done = vdev_draid_io_done,
	.vdev_op_state_change = vdev_draid_state_change,
	.vdev_op_need_resilver = vdev_draid_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_draid_xlate,
	.vdev_op_rebuild_asize = vdev_draid_rebuild_asize,
	.vdev_op_metaslab_init = vdev_draid_metaslab_init,
	.vdev_op_config_generate = vdev_draid_config_generate,
	.vdev_op_nparity = vdev_draid_nparity,
	.vdev_op_ndisks = vdev_draid_ndisks,
	.vdev_op_type = VDEV_TYPE_DRAID,
	.vdev_op_leaf = B_FALSE,
};


/*
 * A dRAID distributed spare is a virtual leaf vdev which is included in the
 * parent dRAID configuration.  The last N columns of the dRAID permutation
 * table are used to determine on which dRAID children a specific offset
 * should be written.  These spare leaf vdevs can only be used to replace
 * faulted children in the same dRAID configuration.
 */

/*
 * Distributed spare state.  All fields are set when the distributed spare is
 * first opened and are immutable.
 */
typedef struct {
	vdev_t *vds_draid_vdev;		/* top-level parent dRAID vdev */
	uint64_t vds_top_guid;		/* top-level parent dRAID guid */
	uint64_t vds_spare_id;		/* spare id (0 - vdc->vdc_nspares-1) */
} vdev_draid_spare_t;

/*
 * Returns the parent dRAID vdev to which the distributed spare belongs.
 * This may be safely called even when the vdev is not open.
 */
vdev_t *
vdev_draid_spare_get_parent(vdev_t *vd)
{
	vdev_draid_spare_t *vds = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_spare_ops);

	if (vds->vds_draid_vdev != NULL)
		return (vds->vds_draid_vdev);

	return (vdev_lookup_by_guid(vd->vdev_spa->spa_root_vdev,
	    vds->vds_top_guid));
}

/*
 * A dRAID space is active when it's the child of a vdev using the
 * vdev_spare_ops, vdev_replacing_ops or vdev_draid_ops.
 */
static boolean_t
vdev_draid_spare_is_active(vdev_t *vd)
{
	vdev_t *pvd = vd->vdev_parent;

	if (pvd != NULL && (pvd->vdev_ops == &vdev_spare_ops ||
	    pvd->vdev_ops == &vdev_replacing_ops ||
	    pvd->vdev_ops == &vdev_draid_ops)) {
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

/*
 * Given a dRAID distribute spare vdev, returns the physical child vdev
 * on which the provided offset resides.  This may involve recursing through
 * multiple layers of distributed spares.  Note that offset is relative to
 * this vdev.
 */
vdev_t *
vdev_draid_spare_get_child(vdev_t *vd, uint64_t physical_offset)
{
	vdev_draid_spare_t *vds = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_spare_ops);

	/* The vdev is closed */
	if (vds->vds_draid_vdev == NULL)
		return (NULL);

	vdev_t *tvd = vds->vds_draid_vdev;
	vdev_draid_config_t *vdc = tvd->vdev_tsd;

	ASSERT3P(tvd->vdev_ops, ==, &vdev_draid_ops);
	ASSERT3U(vds->vds_spare_id, <, vdc->vdc_nspares);

	uint8_t *base;
	uint64_t iter;
	uint64_t perm = physical_offset / vdc->vdc_devslicesz;

	vdev_draid_get_perm(vdc, perm, &base, &iter);

	uint64_t cid = vdev_draid_permute_id(vdc, base, iter,
	    (tvd->vdev_children - 1) - vds->vds_spare_id);
	vdev_t *cvd = tvd->vdev_child[cid];

	if (cvd->vdev_ops == &vdev_draid_spare_ops)
		return (vdev_draid_spare_get_child(cvd, physical_offset));

	return (cvd);
}

/* ARGSUSED */
static void
vdev_draid_spare_close(vdev_t *vd)
{
	vdev_draid_spare_t *vds = vd->vdev_tsd;
	vds->vds_draid_vdev = NULL;
}

/*
 * Opening a dRAID spare device is done by looking up the associated dRAID
 * top-level vdev guid from the spare configuration.
 */
static int
vdev_draid_spare_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_draid_spare_t *vds = vd->vdev_tsd;
	vdev_t *rvd = vd->vdev_spa->spa_root_vdev;
	uint64_t asize, max_asize;

	vdev_t *tvd = vdev_lookup_by_guid(rvd, vds->vds_top_guid);
	if (tvd == NULL) {
		/*
		 * When spa_vdev_add() is labeling new spares the
		 * associated dRAID is not attached to the root vdev
		 * nor does this spare have a parent.  Simulate a valid
		 * device in order to allow the label to be initialized
		 * and the distributed spare added to the configuration.
		 */
		if (vd->vdev_parent == NULL) {
			*psize = *max_psize = SPA_MINDEVSIZE;
			*logical_ashift = *physical_ashift = ASHIFT_MIN;
			return (0);
		}

		return (SET_ERROR(EINVAL));
	}

	vdev_draid_config_t *vdc = tvd->vdev_tsd;
	if (tvd->vdev_ops != &vdev_draid_ops || vdc == NULL)
		return (SET_ERROR(EINVAL));

	if (vds->vds_spare_id >= vdc->vdc_nspares)
		return (SET_ERROR(EINVAL));

	/*
	 * Neither tvd->vdev_asize or tvd->vdev_max_asize can be used here
	 * because the caller may be vdev_draid_open() in which case the
	 * values are stale as they haven't yet been updated by vdev_open().
	 * To avoid this always recalculate the dRAID asize and max_asize.
	 */
	vdev_draid_calculate_asize(tvd, &asize, &max_asize,
	    logical_ashift, physical_ashift);

	*psize = asize + VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE;
	*max_psize = max_asize + VDEV_LABEL_START_SIZE + VDEV_LABEL_END_SIZE;

	vds->vds_draid_vdev = tvd;

	return (0);
}

/*
 * Completed distributed spare IO.  Store the result in the parent zio
 * as if it had performed the operation itself.  Only the first error is
 * preserved if there are multiple errors.
 */
static void
vdev_draid_spare_child_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;

	/*
	 * IOs are issued to non-writable vdevs in order to keep their
	 * DTLs accurate.  However, we don't want to propagate the
	 * error in to the distributed spare's DTL.  When resilvering
	 * vdev_draid_need_resilver() will consult the relevant DTL
	 * to determine if the data is missing and must be repaired.
	 */
	if (!vdev_writeable(zio->io_vd))
		return;

	if (pio->io_error == 0)
		pio->io_error = zio->io_error;
}

/*
 * Returns a valid label nvlist for the distributed spare vdev.  This is
 * used to bypass the IO pipeline to avoid the complexity of constructing
 * a complete label with valid checksum to return when read.
 */
nvlist_t *
vdev_draid_read_config_spare(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	spa_aux_vdev_t *sav = &spa->spa_spares;
	uint64_t guid = vd->vdev_guid;

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_IS_SPARE, 1);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_CREATE_TXG, vd->vdev_crtxg);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_VERSION, spa_version(spa));
	fnvlist_add_string(nv, ZPOOL_CONFIG_POOL_NAME, spa_name(spa));
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_POOL_GUID, spa_guid(spa));
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_POOL_TXG, spa->spa_config_txg);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_TOP_GUID, vd->vdev_top->vdev_guid);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_POOL_STATE,
	    vdev_draid_spare_is_active(vd) ?
	    POOL_STATE_ACTIVE : POOL_STATE_SPARE);

	/* Set the vdev guid based on the vdev list in sav_count. */
	for (int i = 0; i < sav->sav_count; i++) {
		if (sav->sav_vdevs[i]->vdev_ops == &vdev_draid_spare_ops &&
		    strcmp(sav->sav_vdevs[i]->vdev_path, vd->vdev_path) == 0) {
			guid = sav->sav_vdevs[i]->vdev_guid;
			break;
		}
	}

	fnvlist_add_uint64(nv, ZPOOL_CONFIG_GUID, guid);

	return (nv);
}

/*
 * Handle any ioctl requested of the distributed spare.  Only flushes
 * are supported in which case all children must be flushed.
 */
static int
vdev_draid_spare_ioctl(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	int error = 0;

	if (zio->io_cmd == DKIOCFLUSHWRITECACHE) {
		for (int c = 0; c < vd->vdev_children; c++) {
			zio_nowait(zio_vdev_child_io(zio, NULL,
			    vd->vdev_child[c], zio->io_offset, zio->io_abd,
			    zio->io_size, zio->io_type, zio->io_priority, 0,
			    vdev_draid_spare_child_done, zio));
		}
	} else {
		error = SET_ERROR(ENOTSUP);
	}

	return (error);
}

/*
 * Initiate an IO to the distributed spare.  For normal IOs this entails using
 * the zio->io_offset and permutation table to calculate which child dRAID vdev
 * is responsible for the data.  Then passing along the zio to that child to
 * perform the actual IO.  The label ranges are not stored on disk and require
 * some special handling which is described below.
 */
static void
vdev_draid_spare_io_start(zio_t *zio)
{
	vdev_t *cvd = NULL, *vd = zio->io_vd;
	vdev_draid_spare_t *vds = vd->vdev_tsd;
	uint64_t offset = zio->io_offset - VDEV_LABEL_START_SIZE;

	/*
	 * If the vdev is closed, it's likely in the REMOVED or FAULTED state.
	 * Nothing to be done here but return failure.
	 */
	if (vds == NULL) {
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:
		zio->io_error = vdev_draid_spare_ioctl(zio);
		break;

	case ZIO_TYPE_WRITE:
		if (VDEV_OFFSET_IS_LABEL(vd, zio->io_offset)) {
			/*
			 * Accept probe IOs and config writers to simulate the
			 * existence of an on disk label.  vdev_label_sync(),
			 * vdev_uberblock_sync() and vdev_copy_uberblocks()
			 * skip the distributed spares.  This only leaves
			 * vdev_label_init() which is allowed to succeed to
			 * avoid adding special cases the function.
			 */
			if (zio->io_flags & ZIO_FLAG_PROBE ||
			    zio->io_flags & ZIO_FLAG_CONFIG_WRITER) {
				zio->io_error = 0;
			} else {
				zio->io_error = SET_ERROR(EIO);
			}
		} else {
			cvd = vdev_draid_spare_get_child(vd, offset);

			if (cvd == NULL) {
				zio->io_error = SET_ERROR(ENXIO);
			} else {
				zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
				    offset, zio->io_abd, zio->io_size,
				    zio->io_type, zio->io_priority, 0,
				    vdev_draid_spare_child_done, zio));
			}
		}
		break;

	case ZIO_TYPE_READ:
		if (VDEV_OFFSET_IS_LABEL(vd, zio->io_offset)) {
			/*
			 * Accept probe IOs to simulate the existence of a
			 * label.  vdev_label_read_config() bypasses the
			 * pipeline to read the label configuration and
			 * vdev_uberblock_load() skips distributed spares
			 * when attempting to locate the best uberblock.
			 */
			if (zio->io_flags & ZIO_FLAG_PROBE) {
				zio->io_error = 0;
			} else {
				zio->io_error = SET_ERROR(EIO);
			}
		} else {
			cvd = vdev_draid_spare_get_child(vd, offset);

			if (cvd == NULL || !vdev_readable(cvd)) {
				zio->io_error = SET_ERROR(ENXIO);
			} else {
				zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
				    offset, zio->io_abd, zio->io_size,
				    zio->io_type, zio->io_priority, 0,
				    vdev_draid_spare_child_done, zio));
			}
		}
		break;

	case ZIO_TYPE_TRIM:
		/* The vdev label ranges are never trimmed */
		ASSERT0(VDEV_OFFSET_IS_LABEL(vd, zio->io_offset));

		cvd = vdev_draid_spare_get_child(vd, offset);

		if (cvd == NULL || !cvd->vdev_has_trim) {
			zio->io_error = SET_ERROR(ENXIO);
		} else {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    offset, zio->io_abd, zio->io_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_draid_spare_child_done, zio));
		}
		break;

	default:
		zio->io_error = SET_ERROR(ENOTSUP);
		break;
	}

	zio_execute(zio);
}

/* ARGSUSED */
static void
vdev_draid_spare_io_done(zio_t *zio)
{
}

/*
 * Lookup the full spare config in spa->spa_spares.sav_config and
 * return the top_guid and spare_id for the named spare.
 */
static int
vdev_draid_spare_lookup(spa_t *spa, nvlist_t *nv, uint64_t *top_guidp,
    uint64_t *spare_idp)
{
	nvlist_t **spares;
	uint_t nspares;
	int error;

	if ((spa->spa_spares.sav_config == NULL) ||
	    (nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) != 0)) {
		return (SET_ERROR(ENOENT));
	}

	char *spare_name;
	error = nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &spare_name);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	for (int i = 0; i < nspares; i++) {
		nvlist_t *spare = spares[i];
		uint64_t top_guid, spare_id;
		char *type, *path;

		/* Skip non-distributed spares */
		error = nvlist_lookup_string(spare, ZPOOL_CONFIG_TYPE, &type);
		if (error != 0 || strcmp(type, VDEV_TYPE_DRAID_SPARE) != 0)
			continue;

		/* Skip spares with the wrong name */
		error = nvlist_lookup_string(spare, ZPOOL_CONFIG_PATH, &path);
		if (error != 0 || strcmp(path, spare_name) != 0)
			continue;

		/* Found the matching spare */
		error = nvlist_lookup_uint64(spare,
		    ZPOOL_CONFIG_TOP_GUID, &top_guid);
		if (error == 0) {
			error = nvlist_lookup_uint64(spare,
			    ZPOOL_CONFIG_SPARE_ID, &spare_id);
		}

		if (error != 0) {
			return (SET_ERROR(EINVAL));
		} else {
			*top_guidp = top_guid;
			*spare_idp = spare_id;
			return (0);
		}
	}

	return (SET_ERROR(ENOENT));
}

/*
 * Initialize private dRAID spare specific fields from the nvlist.
 */
static int
vdev_draid_spare_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	vdev_draid_spare_t *vds;
	uint64_t top_guid = 0;
	uint64_t spare_id;

	/*
	 * In the normal case check the list of spares stored in the spa
	 * to lookup the top_guid and spare_id for provided spare config.
	 * When creating a new pool or adding vdevs the spare list is not
	 * yet populated and the values are provided in the passed config.
	 */
	if (vdev_draid_spare_lookup(spa, nv, &top_guid, &spare_id) != 0) {
		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_TOP_GUID,
		    &top_guid) != 0)
			return (SET_ERROR(EINVAL));

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_SPARE_ID,
		    &spare_id) != 0)
			return (SET_ERROR(EINVAL));
	}

	vds = kmem_alloc(sizeof (vdev_draid_spare_t), KM_SLEEP);
	vds->vds_draid_vdev = NULL;
	vds->vds_top_guid = top_guid;
	vds->vds_spare_id = spare_id;

	*tsd = vds;

	return (0);
}

static void
vdev_draid_spare_fini(vdev_t *vd)
{
	kmem_free(vd->vdev_tsd, sizeof (vdev_draid_spare_t));
}

static void
vdev_draid_spare_config_generate(vdev_t *vd, nvlist_t *nv)
{
	vdev_draid_spare_t *vds = vd->vdev_tsd;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_spare_ops);

	fnvlist_add_uint64(nv, ZPOOL_CONFIG_TOP_GUID, vds->vds_top_guid);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_SPARE_ID, vds->vds_spare_id);
}

vdev_ops_t vdev_draid_spare_ops = {
	.vdev_op_init = vdev_draid_spare_init,
	.vdev_op_fini = vdev_draid_spare_fini,
	.vdev_op_open = vdev_draid_spare_open,
	.vdev_op_close = vdev_draid_spare_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_draid_spare_io_start,
	.vdev_op_io_done = vdev_draid_spare_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = vdev_draid_spare_config_generate,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DRAID_SPARE,
	.vdev_op_leaf = B_TRUE,
};
