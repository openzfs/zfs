// SPDX-License-Identifier: CDDL-1.0

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/blake3.h>

#include <sys/zfs_impl.h>

/*
 * set it to a define for debugging
 */
#undef	BLAKE3_DEBUG

/*
 * C version of:
 * https://github.com/BLAKE3-team/BLAKE3/tree/master/test_vectors
 */
typedef struct {
	/* input length for this entry */
	const int input_len;

	/* hash value */
	const char *hash;

	/* salted hash value */
	const char *shash;
} blake3_test_t;

/* BLAKE3 is variable here */
#define	TEST_DIGEST_LEN 262

/*
 * key for the keyed hashing
 */
static const char *salt = "whats the Elvish word for friend";

static blake3_test_t TestArray[] = {
	{
	    0,
	    "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262e0"
	    "0f03e7b69af26b7faaf09fcd333050338ddfe085b8cc869ca98b206c08243a26f5"
	    "487789e8f660afe6c99ef9e0c52b92e7393024a80459cf91f476f9ffdbda7001c2"
	    "2e159b402631f277ca96f2defdf1078282314e763699a31c5363165421cce14d",
	    "92b2b75604ed3c761f9d6f62392c8a9227ad0ea3f09573e783f1498a4ed60d26b1"
	    "8171a2f22a4b94822c701f107153dba24918c4bae4d2945c20ece13387627d3b73"
	    "cbf97b797d5e59948c7ef788f54372df45e45e4293c7dc18c1d41144a9758be589"
	    "60856be1eabbe22c2653190de560ca3b2ac4aa692a9210694254c371e851bc8f",
	},
	{
	    1,
	    "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213c3"
	    "a6cb8bf623e20cdb535f8d1a5ffb86342d9c0b64aca3bce1d31f60adfa137b358a"
	    "d4d79f97b47c3d5e79f179df87a3b9776ef8325f8329886ba42f07fb138bb502f4"
	    "081cbcec3195c5871e6c23e2cc97d3c69a613eba131e5f1351f3f1da786545e5",
	    "6d7878dfff2f485635d39013278ae14f1454b8c0a3a2d34bc1ab38228a80c95b65"
	    "68c0490609413006fbd428eb3fd14e7756d90f73a4725fad147f7bf70fd61c4e0c"
	    "f7074885e92b0e3f125978b4154986d4fb202a3f331a3fb6cf349a3a70e49990f9"
	    "8fe4289761c8602c4e6ab1138d31d3b62218078b2f3ba9a88e1d08d0dd4cea11",
	},
	{
	    2,
	    "7b7015bb92cf0b318037702a6cdd81dee41224f734684c2c122cd6359cb1ee63d8"
	    "386b22e2ddc05836b7c1bb693d92af006deb5ffbc4c70fb44d0195d0c6f252faac"
	    "61659ef86523aa16517f87cb5f1340e723756ab65efb2f91964e14391de2a43226"
	    "3a6faf1d146937b35a33621c12d00be8223a7f1919cec0acd12097ff3ab00ab1",
	    "5392ddae0e0a69d5f40160462cbd9bd889375082ff224ac9c758802b7a6fd20a9f"
	    "fbf7efd13e989a6c246f96d3a96b9d279f2c4e63fb0bdff633957acf50ee1a5f65"
	    "8be144bab0f6f16500dee4aa5967fc2c586d85a04caddec90fffb7633f46a60786"
	    "024353b9e5cebe277fcd9514217fee2267dcda8f7b31697b7c54fab6a939bf8f",
	},
	{
	    3,
	    "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f5b"
	    "49b82f805a538c68915c1ae8035c900fd1d4b13902920fd05e1450822f36de9454"
	    "b7e9996de4900c8e723512883f93f4345f8a58bfe64ee38d3ad71ab027765d25cd"
	    "d0e448328a8e7a683b9a6af8b0af94fa09010d9186890b096a08471e4230a134",
	    "39e67b76b5a007d4921969779fe666da67b5213b096084ab674742f0d5ec62b9b9"
	    "142d0fab08e1b161efdbb28d18afc64d8f72160c958e53a950cdecf91c1a1bbab1"
	    "a9c0f01def762a77e2e8545d4dec241e98a89b6db2e9a5b070fc110caae2622690"
	    "bd7b76c02ab60750a3ea75426a6bb8803c370ffe465f07fb57def95df772c39f",
	},
	{
	    4,
	    "f30f5ab28fe047904037f77b6da4fea1e27241c5d132638d8bedce9d40494f328f"
	    "603ba4564453e06cdcee6cbe728a4519bbe6f0d41e8a14b5b225174a566dbfa61b"
	    "56afb1e452dc08c804f8c3143c9e2cc4a31bb738bf8c1917b55830c6e657972117"
	    "01dc0b98daa1faeaa6ee9e56ab606ce03a1a881e8f14e87a4acf4646272cfd12",
	    "7671dde590c95d5ac9616651ff5aa0a27bee5913a348e053b8aa9108917fe07011"
	    "6c0acff3f0d1fa97ab38d813fd46506089118147d83393019b068a55d646251ecf"
	    "81105f798d76a10ae413f3d925787d6216a7eb444e510fd56916f1d753a5544ecf"
	    "0072134a146b2615b42f50c179f56b8fae0788008e3e27c67482349e249cb86a",
	},
	{
	    5,
	    "b40b44dfd97e7a84a996a91af8b85188c66c126940ba7aad2e7ae6b385402aa2eb"
	    "cfdac6c5d32c31209e1f81a454751280db64942ce395104e1e4eaca62607de1c2c"
	    "a748251754ea5bbe8c20150e7f47efd57012c63b3c6a6632dc1c7cd15f3e1c9999"
	    "04037d60fac2eb9397f2adbe458d7f264e64f1e73aa927b30988e2aed2f03620",
	    "73ac69eecf286894d8102018a6fc729f4b1f4247d3703f69bdc6a5fe3e0c84616a"
	    "b199d1f2f3e53bffb17f0a2209fe8b4f7d4c7bae59c2bc7d01f1ff94c67588cc6b"
	    "38fa6024886f2c078bfe09b5d9e6584cd6c521c3bb52f4de7687b37117a2dbbec0"
	    "d59e92fa9a8cc3240d4432f91757aabcae03e87431dac003e7d73574bfdd8218",
	},
	{
	    6,
	    "06c4e8ffb6872fad96f9aaca5eee1553eb62aed0ad7198cef42e87f6a616c84461"
	    "1a30c4e4f37fe2fe23c0883cde5cf7059d88b657c7ed2087e3d210925ede716435"
	    "d6d5d82597a1e52b9553919e804f5656278bd739880692c94bff2824d8e0b48cac"
	    "1d24682699e4883389dc4f2faa2eb3b4db6e39debd5061ff3609916f3e07529a",
	    "82d3199d0013035682cc7f2a399d4c212544376a839aa863a0f4c91220ca7a6dc2"
	    "ffb3aa05f2631f0fa9ac19b6e97eb7e6669e5ec254799350c8b8d189e880780084"
	    "2a5383c4d907c932f34490aaf00064de8cdb157357bde37c1504d2960034930887"
	    "603abc5ccb9f5247f79224baff6120a3c622a46d7b1bcaee02c5025460941256",
	},
	{
	    7,
	    "3f8770f387faad08faa9d8414e9f449ac68e6ff0417f673f602a646a891419fe66"
	    "036ef6e6d1a8f54baa9fed1fc11c77cfb9cff65bae915045027046ebe0c01bf5a9"
	    "41f3bb0f73791d3fc0b84370f9f30af0cd5b0fc334dd61f70feb60dad785f070fe"
	    "f1f343ed933b49a5ca0d16a503f599a365a4296739248b28d1a20b0e2cc8975c",
	    "af0a7ec382aedc0cfd626e49e7628bc7a353a4cb108855541a5651bf64fbb28a7c"
	    "5035ba0f48a9c73dabb2be0533d02e8fd5d0d5639a18b2803ba6bf527e1d145d5f"
	    "d6406c437b79bcaad6c7bdf1cf4bd56a893c3eb9510335a7a798548c6753f74617"
	    "bede88bef924ba4b334f8852476d90b26c5dc4c3668a2519266a562c6c8034a6",
	},
	{
	    8,
	    "2351207d04fc16ade43ccab08600939c7c1fa70a5c0aaca76063d04c3228eaeb72"
	    "5d6d46ceed8f785ab9f2f9b06acfe398c6699c6129da084cb531177445a682894f"
	    "9685eaf836999221d17c9a64a3a057000524cd2823986db378b074290a1a9b93a2"
	    "2e135ed2c14c7e20c6d045cd00b903400374126676ea78874d79f2dd7883cf5c",
	    "be2f5495c61cba1bb348a34948c004045e3bd4dae8f0fe82bf44d0da245a060048"
	    "eb5e68ce6dea1eb0229e144f578b3aa7e9f4f85febd135df8525e6fe40c6f0340d"
	    "13dd09b255ccd5112a94238f2be3c0b5b7ecde06580426a93e0708555a265305ab"
	    "f86d874e34b4995b788e37a823491f25127a502fe0704baa6bfdf04e76c13276",
	},
	{
	    63,
	    "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b11"
	    "97012b1e7d9af4d7cb7bdd1f3bb49a90a9b5dec3ea2bbc6eaebce77f4e470cbf46"
	    "87093b5352f04e4a4570fba233164e6acc36900e35d185886a827f7ea9bdc1e5c3"
	    "ce88b095a200e62c10c043b3e9bc6cb9b6ac4dfa51794b02ace9f98779040755",
	    "bb1eb5d4afa793c1ebdd9fb08def6c36d10096986ae0cfe148cd101170ce37aea0"
	    "5a63d74a840aecd514f654f080e51ac50fd617d22610d91780fe6b07a26b0847ab"
	    "b38291058c97474ef6ddd190d30fc318185c09ca1589d2024f0a6f16d45f116783"
	    "77483fa5c005b2a107cb9943e5da634e7046855eaa888663de55d6471371d55d",
	},
	{
	    64,
	    "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98fc"
	    "9cc56cb831ffe33ea8e7e1d1df09b26efd2767670066aa82d023b1dfe8ab1b2b7f"
	    "bb5b97592d46ffe3e05a6a9b592e2949c74160e4674301bc3f97e04903f8c6cf95"
	    "b863174c33228924cdef7ae47559b10b294acd660666c4538833582b43f82d74",
	    "ba8ced36f327700d213f120b1a207a3b8c04330528586f414d09f2f7d9ccb7e682"
	    "44c26010afc3f762615bbac552a1ca909e67c83e2fd5478cf46b9e811efccc93f7"
	    "7a21b17a152ebaca1695733fdb086e23cd0eb48c41c034d52523fc21236e5d8c92"
	    "55306e48d52ba40b4dac24256460d56573d1312319afcf3ed39d72d0bfc69acb",
	},
	{
	    65,
	    "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee0e"
	    "16e0a4749d6811dd1d6d1265c29729b1b75a9ac346cf93f0e1d7296dfcfd4313b3"
	    "a227faaaaf7757cc95b4e87a49be3b8a270a12020233509b1c3632b3485eef309d"
	    "0abc4a4a696c9decc6e90454b53b000f456a3f10079072baaf7a981653221f2c",
	    "c0a4edefa2d2accb9277c371ac12fcdbb52988a86edc54f0716e1591b4326e72d5"
	    "e795f46a596b02d3d4bfb43abad1e5d19211152722ec1f20fef2cd413e3c22f2fc"
	    "5da3d73041275be6ede3517b3b9f0fc67ade5956a672b8b75d96cb43294b904149"
	    "7de92637ed3f2439225e683910cb3ae923374449ca788fb0f9bea92731bc26ad",
	},
	{
	    127,
	    "d81293fda863f008c09e92fc382a81f5a0b4a1251cba1634016a0f86a6bd640de3"
	    "137d477156d1fde56b0cf36f8ef18b44b2d79897bece12227539ac9ae0a5119da4"
	    "7644d934d26e74dc316145dcb8bb69ac3f2e05c242dd6ee06484fcb0e956dc4435"
	    "5b452c5e2bbb5e2b66e99f5dd443d0cbcaaafd4beebaed24ae2f8bb672bcef78",
	    "c64200ae7dfaf35577ac5a9521c47863fb71514a3bcad18819218b818de85818ee"
	    "7a317aaccc1458f78d6f65f3427ec97d9c0adb0d6dacd4471374b621b7b5f35cd5"
	    "4663c64dbe0b9e2d95632f84c611313ea5bd90b71ce97b3cf645776f3adc11e27d"
	    "135cbadb9875c2bf8d3ae6b02f8a0206aba0c35bfe42574011931c9a255ce6dc",
	},
	{
	    128,
	    "f17e570564b26578c33bb7f44643f539624b05df1a76c81f30acd548c44b45efa6"
	    "9faba091427f9c5c4caa873aa07828651f19c55bad85c47d1368b11c6fd99e47ec"
	    "ba5820a0325984d74fe3e4058494ca12e3f1d3293d0010a9722f7dee64f71246f7"
	    "5e9361f44cc8e214a100650db1313ff76a9f93ec6e84edb7add1cb4a95019b0c",
	    "b04fe15577457267ff3b6f3c947d93be581e7e3a4b018679125eaf86f6a628ecd8"
	    "6bbe0001f10bda47e6077b735016fca8119da11348d93ca302bbd125bde0db2b50"
	    "edbe728a620bb9d3e6f706286aedea973425c0b9eedf8a38873544cf91badf49ad"
	    "92a635a93f71ddfcee1eae536c25d1b270956be16588ef1cfef2f1d15f650bd5",
	},
	{
	    129,
	    "683aaae9f3c5ba37eaaf072aed0f9e30bac0865137bae68b1fde4ca2aebdcb12f9"
	    "6ffa7b36dd78ba321be7e842d364a62a42e3746681c8bace18a4a8a79649285c71"
	    "27bf8febf125be9de39586d251f0d41da20980b70d35e3dac0eee59e468a894fa7"
	    "e6a07129aaad09855f6ad4801512a116ba2b7841e6cfc99ad77594a8f2d181a7",
	    "d4a64dae6cdccbac1e5287f54f17c5f985105457c1a2ec1878ebd4b57e20d38f1c"
	    "9db018541eec241b748f87725665b7b1ace3e0065b29c3bcb232c90e37897fa5aa"
	    "ee7e1e8a2ecfcd9b51463e42238cfdd7fee1aecb3267fa7f2128079176132a412c"
	    "d8aaf0791276f6b98ff67359bd8652ef3a203976d5ff1cd41885573487bcd683",
	},
	{
	    1023,
	    "10108970eeda3eb932baac1428c7a2163b0e924c9a9e25b35bba72b28f70bd11a1"
	    "82d27a591b05592b15607500e1e8dd56bc6c7fc063715b7a1d737df5bad3339c56"
	    "778957d870eb9717b57ea3d9fb68d1b55127bba6a906a4a24bbd5acb2d123a37b2"
	    "8f9e9a81bbaae360d58f85e5fc9d75f7c370a0cc09b6522d9c8d822f2f28f485",
	    "c951ecdf03288d0fcc96ee3413563d8a6d3589547f2c2fb36d9786470f1b9d6e89"
	    "0316d2e6d8b8c25b0a5b2180f94fb1a158ef508c3cde45e2966bd796a696d3e13e"
	    "fd86259d756387d9becf5c8bf1ce2192b87025152907b6d8cc33d17826d8b7b9bc"
	    "97e38c3c85108ef09f013e01c229c20a83d9e8efac5b37470da28575fd755a10",
	},
	{
	    1024,
	    "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af71c"
	    "f8107265ecdaf8505b95d8fcec83a98a6a96ea5109d2c179c47a387ffbb404756f"
	    "6eeae7883b446b70ebb144527c2075ab8ab204c0086bb22b7c93d465efc57f8d91"
	    "7f0b385c6df265e77003b85102967486ed57db5c5ca170ba441427ed9afa684e",
	    "75c46f6f3d9eb4f55ecaaee480db732e6c2105546f1e675003687c31719c7ba4a7"
	    "8bc838c72852d4f49c864acb7adafe2478e824afe51c8919d06168414c265f298a"
	    "8094b1ad813a9b8614acabac321f24ce61c5a5346eb519520d38ecc43e89b50002"
	    "36df0597243e4d2493fd626730e2ba17ac4d8824d09d1a4a8f57b8227778e2de",
	},
	{
	    1025,
	    "d00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444f4"
	    "c4a22b4b399155358a994e52bf255de60035742ec71bd08ac275a1b51cc6bfe332"
	    "b0ef84b409108cda080e6269ed4b3e2c3f7d722aa4cdc98d16deb554e5627be8f9"
	    "55c98e1d5f9565a9194cad0c4285f93700062d9595adb992ae68ff12800ab67a",
	    "357dc55de0c7e382c900fd6e320acc04146be01db6a8ce7210b7189bd664ea6936"
	    "2396b77fdc0d2634a552970843722066c3c15902ae5097e00ff53f1e116f1cd535"
	    "2720113a837ab2452cafbde4d54085d9cf5d21ca613071551b25d52e69d6c81123"
	    "872b6f19cd3bc1333edf0c52b94de23ba772cf82636cff4542540a7738d5b930",
	},
	{
	    2048,
	    "e776b6028c7cd22a4d0ba182a8bf62205d2ef576467e838ed6f2529b85fba24a9a"
	    "60bf80001410ec9eea6698cd537939fad4749edd484cb541aced55cd9bf54764d0"
	    "63f23f6f1e32e12958ba5cfeb1bf618ad094266d4fc3c968c2088f677454c288c6"
	    "7ba0dba337b9d91c7e1ba586dc9a5bc2d5e90c14f53a8863ac75655461cea8f9",
	    "879cf1fa2ea0e79126cb1063617a05b6ad9d0b696d0d757cf053439f60a99dd101"
	    "73b961cd574288194b23ece278c330fbb8585485e74967f31352a8183aa782b2b2"
	    "2f26cdcadb61eed1a5bc144b8198fbb0c13abbf8e3192c145d0a5c21633b0ef860"
	    "54f42809df823389ee40811a5910dcbd1018af31c3b43aa55201ed4edaac74fe",
	},
	{
	    2049,
	    "5f4d72f40d7a5f82b15ca2b2e44b1de3c2ef86c426c95c1af0b687952256303096"
	    "de31d71d74103403822a2e0bc1eb193e7aecc9643a76b7bbc0c9f9c52e8783aae9"
	    "8764ca468962b5c2ec92f0c74eb5448d519713e09413719431c802f948dd5d9042"
	    "5a4ecdadece9eb178d80f26efccae630734dff63340285adec2aed3b51073ad3",
	    "9f29700902f7c86e514ddc4df1e3049f258b2472b6dd5267f61bf13983b78dd5f9"
	    "a88abfefdfa1e00b418971f2b39c64ca621e8eb37fceac57fd0c8fc8e117d43b81"
	    "447be22d5d8186f8f5919ba6bcc6846bd7d50726c06d245672c2ad4f61702c6464"
	    "99ee1173daa061ffe15bf45a631e2946d616a4c345822f1151284712f76b2b0e",
	},
	{
	    3072,
	    "b98cb0ff3623be03326b373de6b9095218513e64f1ee2edd2525c7ad1e5cffd29a"
	    "3f6b0b978d6608335c09dc94ccf682f9951cdfc501bfe47b9c9189a6fc7b404d12"
	    "0258506341a6d802857322fbd20d3e5dae05b95c88793fa83db1cb08e7d8008d15"
	    "99b6209d78336e24839724c191b2a52a80448306e0daa84a3fdb566661a37e11",
	    "044a0e7b172a312dc02a4c9a818c036ffa2776368d7f528268d2e6b5df19177022"
	    "f302d0529e4174cc507c463671217975e81dab02b8fdeb0d7ccc7568dd22574c78"
	    "3a76be215441b32e91b9a904be8ea81f7a0afd14bad8ee7c8efc305ace5d3dd61b"
	    "996febe8da4f56ca0919359a7533216e2999fc87ff7d8f176fbecb3d6f34278b",
	},
	{
	    3073,
	    "7124b49501012f81cc7f11ca069ec9226cecb8a2c850cfe644e327d22d3e1cd39a"
	    "27ae3b79d68d89da9bf25bc27139ae65a324918a5f9b7828181e52cf373c84f35b"
	    "639b7fccbb985b6f2fa56aea0c18f531203497b8bbd3a07ceb5926f1cab74d14bd"
	    "66486d9a91eba99059a98bd1cd25876b2af5a76c3e9eed554ed72ea952b603bf",
	    "68dede9bef00ba89e43f31a6825f4cf433389fedae75c04ee9f0cf16a427c95a96"
	    "d6da3fe985054d3478865be9a092250839a697bbda74e279e8a9e69f0025e4cfdd"
	    "d6cfb434b1cd9543aaf97c635d1b451a4386041e4bb100f5e45407cbbc24fa53ea"
	    "2de3536ccb329e4eb9466ec37093a42cf62b82903c696a93a50b702c80f3c3c5",
	},
	{
	    4096,
	    "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e96902"
	    "89e9409ddb1b99768eafe1623da896faf7e1114bebeadc1be30829b6f8af707d85"
	    "c298f4f0ff4d9438aef948335612ae921e76d411c3a9111df62d27eaf871959ae0"
	    "062b5492a0feb98ef3ed4af277f5395172dbe5c311918ea0074ce0036454f620",
	    "befc660aea2f1718884cd8deb9902811d332f4fc4a38cf7c7300d597a081bfc0bb"
	    "b64a36edb564e01e4b4aaf3b060092a6b838bea44afebd2deb8298fa562b7b597c"
	    "757b9df4c911c3ca462e2ac89e9a787357aaf74c3b56d5c07bc93ce899568a3eb1"
	    "7d9250c20f6c5f6c1e792ec9a2dcb715398d5a6ec6d5c54f586a00403a1af1de",
	},
	{
	    4097,
	    "9b4052b38f1c5fc8b1f9ff7ac7b27cd242487b3d890d15c96a1c25b8aa0fb99505"
	    "f91b0b5600a11251652eacfa9497b31cd3c409ce2e45cfe6c0a016967316c426bd"
	    "26f619eab5d70af9a418b845c608840390f361630bd497b1ab44019316357c61db"
	    "e091ce72fc16dc340ac3d6e009e050b3adac4b5b2c92e722cffdc46501531956",
	    "00df940cd36bb9fa7cbbc3556744e0dbc8191401afe70520ba292ee3ca80abbc60"
	    "6db4976cfdd266ae0abf667d9481831ff12e0caa268e7d3e57260c0824115a54ce"
	    "595ccc897786d9dcbf495599cfd90157186a46ec800a6763f1c59e36197e9939e9"
	    "00809f7077c102f888caaf864b253bc41eea812656d46742e4ea42769f89b83f",
	},
	{
	    5120,
	    "9cadc15fed8b5d854562b26a9536d9707cadeda9b143978f319ab34230535833ac"
	    "c61c8fdc114a2010ce8038c853e121e1544985133fccdd0a2d507e8e615e611e9a"
	    "0ba4f47915f49e53d721816a9198e8b30f12d20ec3689989175f1bf7a300eee0d9"
	    "321fad8da232ece6efb8e9fd81b42ad161f6b9550a069e66b11b40487a5f5059",
	    "2c493e48e9b9bf31e0553a22b23503c0a3388f035cece68eb438d22fa1943e209b"
	    "4dc9209cd80ce7c1f7c9a744658e7e288465717ae6e56d5463d4f80cdb2ef56495"
	    "f6a4f5487f69749af0c34c2cdfa857f3056bf8d807336a14d7b89bf62bef2fb54f"
	    "9af6a546f818dc1e98b9e07f8a5834da50fa28fb5874af91bf06020d1bf0120e",
	},
	{
	    5121,
	    "628bd2cb2004694adaab7bbd778a25df25c47b9d4155a55f8fbd79f2fe154cff96"
	    "adaab0613a6146cdaabe498c3a94e529d3fc1da2bd08edf54ed64d40dcd6777647"
	    "eac51d8277d70219a9694334a68bc8f0f23e20b0ff70ada6f844542dfa32cd4204"
	    "ca1846ef76d811cdb296f65e260227f477aa7aa008bac878f72257484f2b6c95",
	    "6ccf1c34753e7a044db80798ecd0782a8f76f33563accaddbfbb2e0ea4b2d0240d"
	    "07e63f13667a8d1490e5e04f13eb617aea16a8c8a5aaed1ef6fbde1b0515e3c810"
	    "50b361af6ead126032998290b563e3caddeaebfab592e155f2e161fb7cba939092"
	    "133f23f9e65245e58ec23457b78a2e8a125588aad6e07d7f11a85b88d375b72d",
	},
	{
	    6144,
	    "3e2e5b74e048f3add6d21faab3f83aa44d3b2278afb83b80b3c35164ebeca2054d"
	    "742022da6fdda444ebc384b04a54c3ac5839b49da7d39f6d8a9db03deab32aade1"
	    "56c1c0311e9b3435cde0ddba0dce7b26a376cad121294b689193508dd63151603c"
	    "6ddb866ad16c2ee41585d1633a2cea093bea714f4c5d6b903522045b20395c83",
	    "3d6b6d21281d0ade5b2b016ae4034c5dec10ca7e475f90f76eac7138e9bc8f1dc3"
	    "5754060091dc5caf3efabe0603c60f45e415bb3407db67e6beb3d11cf8e4f79075"
	    "61f05dace0c15807f4b5f389c841eb114d81a82c02a00b57206b1d11fa6e803486"
	    "b048a5ce87105a686dee041207e095323dfe172df73deb8c9532066d88f9da7e",
	},
	{
	    6145,
	    "f1323a8631446cc50536a9f705ee5cb619424d46887f3c376c695b70e0f0507f18"
	    "a2cfdd73c6e39dd75ce7c1c6e3ef238fd54465f053b25d21044ccb2093beb01501"
	    "5532b108313b5829c3621ce324b8e14229091b7c93f32db2e4e63126a377d2a63a"
	    "3597997d4f1cba59309cb4af240ba70cebff9a23d5e3ff0cdae2cfd54e070022",
	    "9ac301e9e39e45e3250a7e3b3df701aa0fb6889fbd80eeecf28dbc6300fbc539f3"
	    "c184ca2f59780e27a576c1d1fb9772e99fd17881d02ac7dfd39675aca918453283"
	    "ed8c3169085ef4a466b91c1649cc341dfdee60e32231fc34c9c4e0b9a2ba87ca8f"
	    "372589c744c15fd6f985eec15e98136f25beeb4b13c4e43dc84abcc79cd4646c",
	},
	{
	    7168,
	    "61da957ec2499a95d6b8023e2b0e604ec7f6b50e80a9678b89d2628e99ada77a57"
	    "07c321c83361793b9af62a40f43b523df1c8633cecb4cd14d00bdc79c78fca5165"
	    "b863893f6d38b02ff7236c5a9a8ad2dba87d24c547cab046c29fc5bc1ed142e1de"
	    "4763613bb162a5a538e6ef05ed05199d751f9eb58d332791b8d73fb74e4fce95",
	    "b42835e40e9d4a7f42ad8cc04f85a963a76e18198377ed84adddeaecacc6f3fca2"
	    "f01d5277d69bb681c70fa8d36094f73ec06e452c80d2ff2257ed82e7ba34840098"
	    "9a65ee8daa7094ae0933e3d2210ac6395c4af24f91c2b590ef87d7788d7066ea3e"
	    "aebca4c08a4f14b9a27644f99084c3543711b64a070b94f2c9d1d8a90d035d52",
	},
	{
	    7169,
	    "a003fc7a51754a9b3c7fae0367ab3d782dccf28855a03d435f8cfe74605e781798"
	    "a8b20534be1ca9eb2ae2df3fae2ea60e48c6fb0b850b1385b5de0fe460dbe9d9f9"
	    "b0d8db4435da75c601156df9d047f4ede008732eb17adc05d96180f8a735485228"
	    "40779e6062d643b79478a6e8dbce68927f36ebf676ffa7d72d5f68f050b119c8",
	    "ed9b1a922c046fdb3d423ae34e143b05ca1bf28b710432857bf738bcedbfa5113c"
	    "9e28d72fcbfc020814ce3f5d4fc867f01c8f5b6caf305b3ea8a8ba2da3ab69fabc"
	    "b438f19ff11f5378ad4484d75c478de425fb8e6ee809b54eec9bdb184315dc8566"
	    "17c09f5340451bf42fd3270a7b0b6566169f242e533777604c118a6358250f54",
	},
	{
	    8192,
	    "aae792484c8efe4f19e2ca7d371d8c467ffb10748d8a5a1ae579948f718a2a635f"
	    "e51a27db045a567c1ad51be5aa34c01c6651c4d9b5b5ac5d0fd58cf18dd61a4777"
	    "8566b797a8c67df7b1d60b97b19288d2d877bb2df417ace009dcb0241ca1257d62"
	    "712b6a4043b4ff33f690d849da91ea3bf711ed583cb7b7a7da2839ba71309bbf",
	    "dc9637c8845a770b4cbf76b8daec0eebf7dc2eac11498517f08d44c8fc00d58a48"
	    "34464159dcbc12a0ba0c6d6eb41bac0ed6585cabfe0aca36a375e6c5480c22afdc"
	    "40785c170f5a6b8a1107dbee282318d00d915ac9ed1143ad40765ec120042ee121"
	    "cd2baa36250c618adaf9e27260fda2f94dea8fb6f08c04f8f10c78292aa46102",
	},
	{
	    8193,
	    "bab6c09cb8ce8cf459261398d2e7aef35700bf488116ceb94a36d0f5f1b7bc3bb2"
	    "282aa69be089359ea1154b9a9286c4a56af4de975a9aa4a5c497654914d279bea6"
	    "0bb6d2cf7225a2fa0ff5ef56bbe4b149f3ed15860f78b4e2ad04e158e375c1e0c0"
	    "b551cd7dfc82f1b155c11b6b3ed51ec9edb30d133653bb5709d1dbd55f4e1ff6",
	    "954a2a75420c8d6547e3ba5b98d963e6fa6491addc8c023189cc519821b4a1f5f0"
	    "3228648fd983aef045c2fa8290934b0866b615f585149587dda229903996532883"
	    "5a2b18f1d63b7e300fc76ff260b571839fe44876a4eae66cbac8c67694411ed7e0"
	    "9df51068a22c6e67d6d3dd2cca8ff12e3275384006c80f4db68023f24eebba57",
	},
	{
	    16384,
	    "f875d6646de28985646f34ee13be9a576fd515f76b5b0a26bb324735041ddde49d"
	    "764c270176e53e97bdffa58d549073f2c660be0e81293767ed4e4929f9ad34bbb3"
	    "9a529334c57c4a381ffd2a6d4bfdbf1482651b172aa883cc13408fa67758a3e475"
	    "03f93f87720a3177325f7823251b85275f64636a8f1d599c2e49722f42e93893",
	    "9e9fc4eb7cf081ea7c47d1807790ed211bfec56aa25bb7037784c13c4b707b0df9"
	    "e601b101e4cf63a404dfe50f2e1865bb12edc8fca166579ce0c70dba5a5c0fc960"
	    "ad6f3772183416a00bd29d4c6e651ea7620bb100c9449858bf14e1ddc9ecd35725"
	    "581ca5b9160de04060045993d972571c3e8f71e9d0496bfa744656861b169d65",
	},
	{
	    31744,
	    "62b6960e1a44bcc1eb1a611a8d6235b6b4b78f32e7abc4fb4c6cdcce94895c4786"
	    "0cc51f2b0c28a7b77304bd55fe73af663c02d3f52ea053ba43431ca5bab7bfea2f"
	    "5e9d7121770d88f70ae9649ea713087d1914f7f312147e247f87eb2d4ffef0ac97"
	    "8bf7b6579d57d533355aa20b8b77b13fd09748728a5cc327a8ec470f4013226f",
	    "efa53b389ab67c593dba624d898d0f7353ab99e4ac9d42302ee64cbf9939a4193a"
	    "7258db2d9cd32a7a3ecfce46144114b15c2fcb68a618a976bd74515d47be08b628"
	    "be420b5e830fade7c080e351a076fbc38641ad80c736c8a18fe3c66ce12f95c61c"
	    "2462a9770d60d0f77115bbcd3782b593016a4e728d4c06cee4505cb0c08a42ec",
	},
	{
	    102400,
	    "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085e0"
	    "1c59dab908c04c3342b816941a26d69c2605ebee5ec5291cc55e15b76146e6745f"
	    "0601156c3596cb75065a9c57f35585a52e1ac70f69131c23d611ce11ee4ab1ec2c"
	    "009012d236648e77be9295dd0426f29b764d65de58eb7d01dd42248204f45f8e",
	    "1c35d1a5811083fd7119f5d5d1ba027b4d01c0c6c49fb6ff2cf75393ea5db4a7f9"
	    "dbdd3e1d81dcbca3ba241bb18760f207710b751846faaeb9dff8262710999a59b2"
	    "aa1aca298a032d94eacfadf1aa192418eb54808db23b56e34213266aa08499a16b"
	    "354f018fc4967d05f8b9d2ad87a7278337be9693fc638a3bfdbe314574ee6fc4",
	},
	{
	    0, 0, 0
	}
};

#ifdef BLAKE3_DEBUG
#define	dprintf printf
#else
#define	dprintf(...)
#endif

static char fmt_tohex(char c);
static size_t fmt_hexdump(char *dest, const char *src, size_t len);

static char fmt_tohex(char c) {
	return ((char)(c >= 10 ? c-10+'a' : c+'0'));
}

static size_t fmt_hexdump(char *dest, const char *src, size_t len) {
	register const unsigned char *s = (const unsigned char *) src;
	size_t written = 0, i;

	if (!dest)
		return ((len > ((size_t)-1)/2) ? (size_t)-1 : len*2);
	for (i = 0; i < len; ++i) {
		dest[written] = fmt_tohex(s[i]>>4);
		dest[written+1] = fmt_tohex(s[i]&15);
		written += 2;
	}

	return (written);
}

int
main(int argc, char *argv[])
{
	boolean_t failed = B_FALSE;
	uint8_t buffer[102400];
	uint64_t cpu_mhz = 0;
	int id, i, j;
	const zfs_impl_t *blake3 = zfs_impl_get_ops("blake3");

	if (argc == 2)
		cpu_mhz = atoi(argv[1]);

	if (!blake3)
		return (1);

	/* fill test message */
	for (i = 0, j = 0; i < sizeof (buffer); i++, j++) {
		if (j == 251)
			j = 0;
		buffer[i] = (uint8_t)j;
	}

	(void) printf("Running algorithm correctness tests:\n");
	for (id = 0; id < blake3->getcnt(); id++) {
		blake3->setid(id);
		const char *name = blake3->getname();
		dprintf("Result for BLAKE3-%s:\n", name);
		for (i = 0; TestArray[i].hash; i++) {
			blake3_test_t *cur = &TestArray[i];

			BLAKE3_CTX ctx;
			uint8_t digest[TEST_DIGEST_LEN];
			char result[TEST_DIGEST_LEN];

			/* default hashing */
			Blake3_Init(&ctx);
			Blake3_Update(&ctx, buffer, cur->input_len);
			Blake3_FinalSeek(&ctx, 0, digest, TEST_DIGEST_LEN);
			fmt_hexdump(result, (char *)digest, 131);
			if (memcmp(result, cur->hash, 131) != 0)
				failed = B_TRUE;

			dprintf("HASH-res:  %s\n", result);
			dprintf("HASH-ref:  %s\n", cur->hash);

			/* salted hashing */
			Blake3_InitKeyed(&ctx, (const uint8_t *)salt);
			Blake3_Update(&ctx, buffer, cur->input_len);
			Blake3_FinalSeek(&ctx, 0, digest, TEST_DIGEST_LEN);
			fmt_hexdump(result, (char *)digest, 131);
			if (memcmp(result, cur->shash, 131) != 0)
				failed = B_TRUE;

			dprintf("SHASH-res: %s\n", result);
			dprintf("SHASH-ref: %s\n", cur->shash);

			printf("BLAKE3-%s Message (inlen=%d)\tResult: %s\n",
			    name, cur->input_len, failed?"FAILED!":"OK");
		}
	}

	if (failed)
		return (1);

#define	BLAKE3_PERF_TEST(impl, diglen)					\
	do {								\
		BLAKE3_CTX	ctx;					\
		uint8_t		digest[diglen / 8];			\
		uint8_t		block[131072];				\
		uint64_t	delta;					\
		double		cpb = 0;				\
		int		i;					\
		struct timeval	start, end;				\
		memset(block, 0, sizeof (block));			\
		(void) gettimeofday(&start, NULL);			\
		Blake3_Init(&ctx);					\
		for (i = 0; i < 8192; i++)				\
			Blake3_Update(&ctx, block, sizeof (block));	\
		Blake3_Final(&ctx, digest);				\
		(void) gettimeofday(&end, NULL);			\
		delta = (end.tv_sec * 1000000llu + end.tv_usec) -	\
		    (start.tv_sec * 1000000llu + start.tv_usec);	\
		if (cpu_mhz != 0) {					\
			cpb = (cpu_mhz * 1e6 * ((double)delta /		\
			    1000000)) / (8192 * 128 * 1024);		\
		}							\
		(void) printf("BLAKE3-%s %llu us (%.02f CPB)\n", impl,	\
		    (u_longlong_t)delta, cpb);				\
	} while (0)

	printf("Running performance tests (hashing 1024 MiB of data):\n");
	for (id = 0; id < blake3->getcnt(); id++) {
		blake3->setid(id);
		const char *name = blake3->getname();
		BLAKE3_PERF_TEST(name, 256);
	}

	return (0);
}
