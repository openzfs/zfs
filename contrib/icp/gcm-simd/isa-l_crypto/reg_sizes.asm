;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2019 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%ifndef _REG_SIZES_ASM_
%define _REG_SIZES_ASM_

%ifndef AS_FEATURE_LEVEL
%define AS_FEATURE_LEVEL 4
%endif

%define EFLAGS_HAS_CPUID        (1<<21)
%define FLAG_CPUID1_ECX_CLMUL   (1<<1)
%define FLAG_CPUID1_EDX_SSE2    (1<<26)
%define FLAG_CPUID1_ECX_SSE3	(1)
%define FLAG_CPUID1_ECX_SSE4_1  (1<<19)
%define FLAG_CPUID1_ECX_SSE4_2  (1<<20)
%define FLAG_CPUID1_ECX_POPCNT  (1<<23)
%define FLAG_CPUID1_ECX_AESNI   (1<<25)
%define FLAG_CPUID1_ECX_OSXSAVE (1<<27)
%define FLAG_CPUID1_ECX_AVX     (1<<28)
%define FLAG_CPUID1_EBX_AVX2    (1<<5)

%define FLAG_CPUID7_EBX_AVX2           (1<<5)
%define FLAG_CPUID7_EBX_AVX512F        (1<<16)
%define FLAG_CPUID7_EBX_AVX512DQ       (1<<17)
%define FLAG_CPUID7_EBX_AVX512IFMA     (1<<21)
%define FLAG_CPUID7_EBX_AVX512PF       (1<<26)
%define FLAG_CPUID7_EBX_AVX512ER       (1<<27)
%define FLAG_CPUID7_EBX_AVX512CD       (1<<28)
%define FLAG_CPUID7_EBX_SHA            (1<<29)
%define FLAG_CPUID7_EBX_AVX512BW       (1<<30)
%define FLAG_CPUID7_EBX_AVX512VL       (1<<31)

%define FLAG_CPUID7_ECX_AVX512VBMI     (1<<1)
%define FLAG_CPUID7_ECX_AVX512VBMI2    (1 << 6)
%define FLAG_CPUID7_ECX_GFNI           (1 << 8)
%define FLAG_CPUID7_ECX_VAES           (1 << 9)
%define FLAG_CPUID7_ECX_VPCLMULQDQ     (1 << 10)
%define FLAG_CPUID7_ECX_VNNI           (1 << 11)
%define FLAG_CPUID7_ECX_BITALG         (1 << 12)
%define FLAG_CPUID7_ECX_VPOPCNTDQ      (1 << 14)

%define FLAGS_CPUID7_EBX_AVX512_G1 (FLAG_CPUID7_EBX_AVX512F | FLAG_CPUID7_EBX_AVX512VL | FLAG_CPUID7_EBX_AVX512BW | FLAG_CPUID7_EBX_AVX512CD | FLAG_CPUID7_EBX_AVX512DQ)
%define FLAGS_CPUID7_ECX_AVX512_G2 (FLAG_CPUID7_ECX_AVX512VBMI2 | FLAG_CPUID7_ECX_GFNI | FLAG_CPUID7_ECX_VAES | FLAG_CPUID7_ECX_VPCLMULQDQ | FLAG_CPUID7_ECX_VNNI | FLAG_CPUID7_ECX_BITALG | FLAG_CPUID7_ECX_VPOPCNTDQ)

%define FLAG_XGETBV_EAX_XMM            (1<<1)
%define FLAG_XGETBV_EAX_YMM            (1<<2)
%define FLAG_XGETBV_EAX_XMM_YMM        0x6
%define FLAG_XGETBV_EAX_ZMM_OPM        0xe0

%define FLAG_CPUID1_EAX_AVOTON     0x000406d0
%define FLAG_CPUID1_EAX_STEP_MASK  0xfffffff0

; define d and w variants for registers

%define	raxd	eax
%define raxw	ax
%define raxb	al

%define	rbxd	ebx
%define rbxw	bx
%define rbxb	bl

%define	rcxd	ecx
%define rcxw	cx
%define rcxb	cl

%define	rdxd	edx
%define rdxw	dx
%define rdxb	dl

%define	rsid	esi
%define rsiw	si
%define rsib	sil

%define	rdid	edi
%define rdiw	di
%define rdib	dil

%define	rbpd	ebp
%define rbpw	bp
%define rbpb	bpl

%define zmm0x xmm0
%define zmm1x xmm1
%define zmm2x xmm2
%define zmm3x xmm3
%define zmm4x xmm4
%define zmm5x xmm5
%define zmm6x xmm6
%define zmm7x xmm7
%define zmm8x xmm8
%define zmm9x xmm9
%define zmm10x xmm10
%define zmm11x xmm11
%define zmm12x xmm12
%define zmm13x xmm13
%define zmm14x xmm14
%define zmm15x xmm15
%define zmm16x xmm16
%define zmm17x xmm17
%define zmm18x xmm18
%define zmm19x xmm19
%define zmm20x xmm20
%define zmm21x xmm21
%define zmm22x xmm22
%define zmm23x xmm23
%define zmm24x xmm24
%define zmm25x xmm25
%define zmm26x xmm26
%define zmm27x xmm27
%define zmm28x xmm28
%define zmm29x xmm29
%define zmm30x xmm30
%define zmm31x xmm31

%define ymm0x xmm0
%define ymm1x xmm1
%define ymm2x xmm2
%define ymm3x xmm3
%define ymm4x xmm4
%define ymm5x xmm5
%define ymm6x xmm6
%define ymm7x xmm7
%define ymm8x xmm8
%define ymm9x xmm9
%define ymm10x xmm10
%define ymm11x xmm11
%define ymm12x xmm12
%define ymm13x xmm13
%define ymm14x xmm14
%define ymm15x xmm15
%define ymm16x xmm16
%define ymm17x xmm17
%define ymm18x xmm18
%define ymm19x xmm19
%define ymm20x xmm20
%define ymm21x xmm21
%define ymm22x xmm22
%define ymm23x xmm23
%define ymm24x xmm24
%define ymm25x xmm25
%define ymm26x xmm26
%define ymm27x xmm27
%define ymm28x xmm28
%define ymm29x xmm29
%define ymm30x xmm30
%define ymm31x xmm31

%define xmm0x xmm0
%define xmm1x xmm1
%define xmm2x xmm2
%define xmm3x xmm3
%define xmm4x xmm4
%define xmm5x xmm5
%define xmm6x xmm6
%define xmm7x xmm7
%define xmm8x xmm8
%define xmm9x xmm9
%define xmm10x xmm10
%define xmm11x xmm11
%define xmm12x xmm12
%define xmm13x xmm13
%define xmm14x xmm14
%define xmm15x xmm15
%define xmm16x xmm16
%define xmm17x xmm17
%define xmm18x xmm18
%define xmm19x xmm19
%define xmm20x xmm20
%define xmm21x xmm21
%define xmm22x xmm22
%define xmm23x xmm23
%define xmm24x xmm24
%define xmm25x xmm25
%define xmm26x xmm26
%define xmm27x xmm27
%define xmm28x xmm28
%define xmm29x xmm29
%define xmm30x xmm30
%define xmm31x xmm31

%define zmm0y ymm0
%define zmm1y ymm1
%define zmm2y ymm2
%define zmm3y ymm3
%define zmm4y ymm4
%define zmm5y ymm5
%define zmm6y ymm6
%define zmm7y ymm7
%define zmm8y ymm8
%define zmm9y ymm9
%define zmm10y ymm10
%define zmm11y ymm11
%define zmm12y ymm12
%define zmm13y ymm13
%define zmm14y ymm14
%define zmm15y ymm15
%define zmm16y ymm16
%define zmm17y ymm17
%define zmm18y ymm18
%define zmm19y ymm19
%define zmm20y ymm20
%define zmm21y ymm21
%define zmm22y ymm22
%define zmm23y ymm23
%define zmm24y ymm24
%define zmm25y ymm25
%define zmm26y ymm26
%define zmm27y ymm27
%define zmm28y ymm28
%define zmm29y ymm29
%define zmm30y ymm30
%define zmm31y ymm31

%define xmm0y ymm0
%define xmm1y ymm1
%define xmm2y ymm2
%define xmm3y ymm3
%define xmm4y ymm4
%define xmm5y ymm5
%define xmm6y ymm6
%define xmm7y ymm7
%define xmm8y ymm8
%define xmm9y ymm9
%define xmm10y ymm10
%define xmm11y ymm11
%define xmm12y ymm12
%define xmm13y ymm13
%define xmm14y ymm14
%define xmm15y ymm15
%define xmm16y ymm16
%define xmm17y ymm17
%define xmm18y ymm18
%define xmm19y ymm19
%define xmm20y ymm20
%define xmm21y ymm21
%define xmm22y ymm22
%define xmm23y ymm23
%define xmm24y ymm24
%define xmm25y ymm25
%define xmm26y ymm26
%define xmm27y ymm27
%define xmm28y ymm28
%define xmm29y ymm29
%define xmm30y ymm30
%define xmm31y ymm31

%define xmm0z zmm0
%define xmm1z zmm1
%define xmm2z zmm2
%define xmm3z zmm3
%define xmm4z zmm4
%define xmm5z zmm5
%define xmm6z zmm6
%define xmm7z zmm7
%define xmm8z zmm8
%define xmm9z zmm9
%define xmm10z zmm10
%define xmm11z zmm11
%define xmm12z zmm12
%define xmm13z zmm13
%define xmm14z zmm14
%define xmm15z zmm15
%define xmm16z zmm16
%define xmm17z zmm17
%define xmm18z zmm18
%define xmm19z zmm19
%define xmm20z zmm20
%define xmm21z zmm21
%define xmm22z zmm22
%define xmm23z zmm23
%define xmm24z zmm24
%define xmm25z zmm25
%define xmm26z zmm26
%define xmm27z zmm27
%define xmm28z zmm28
%define xmm29z zmm29
%define xmm30z zmm30
%define xmm31z zmm31

%define ymm0z zmm0
%define ymm1z zmm1
%define ymm2z zmm2
%define ymm3z zmm3
%define ymm4z zmm4
%define ymm5z zmm5
%define ymm6z zmm6
%define ymm7z zmm7
%define ymm8z zmm8
%define ymm9z zmm9
%define ymm10z zmm10
%define ymm11z zmm11
%define ymm12z zmm12
%define ymm13z zmm13
%define ymm14z zmm14
%define ymm15z zmm15
%define ymm16z zmm16
%define ymm17z zmm17
%define ymm18z zmm18
%define ymm19z zmm19
%define ymm20z zmm20
%define ymm21z zmm21
%define ymm22z zmm22
%define ymm23z zmm23
%define ymm24z zmm24
%define ymm25z zmm25
%define ymm26z zmm26
%define ymm27z zmm27
%define ymm28z zmm28
%define ymm29z zmm29
%define ymm30z zmm30
%define ymm31z zmm31

%define DWORD(reg) reg %+ d
%define WORD(reg)  reg %+ w
%define BYTE(reg)  reg %+ b

%define XWORD(reg) reg %+ x
%define YWORD(reg) reg %+ y
%define ZWORD(reg) reg %+ z

%ifdef INTEL_CET_ENABLED
 %ifdef __NASM_VER__
  %if AS_FEATURE_LEVEL >= 10
   %ifidn __OUTPUT_FORMAT__,elf32
section .note.gnu.property  note  alloc noexec align=4
DD 0x00000004,0x0000000c,0x00000005,0x00554e47
DD 0xc0000002,0x00000004,0x00000003
   %endif
   %ifidn __OUTPUT_FORMAT__,elf64
section .note.gnu.property  note  alloc noexec align=8
DD 0x00000004,0x00000010,0x00000005,0x00554e47
DD 0xc0000002,0x00000004,0x00000003,0x00000000
   %endif
  %endif
 %endif
%endif

%ifidn __OUTPUT_FORMAT__,elf32
section .note.GNU-stack noalloc noexec nowrite progbits
section .text
%endif
%ifidn __OUTPUT_FORMAT__,elf64
 %define __x86_64__
section .note.GNU-stack noalloc noexec nowrite progbits
section .text
%endif
%ifidn __OUTPUT_FORMAT__,win64
 %define __x86_64__
%endif
%ifidn __OUTPUT_FORMAT__,macho64
 %define __x86_64__
%endif

%ifdef __x86_64__
 %define endbranch db 0xf3, 0x0f, 0x1e, 0xfa
%else
 %define endbranch db 0xf3, 0x0f, 0x1e, 0xfb
%endif

%ifdef REL_TEXT
 %define WRT_OPT
%elifidn __OUTPUT_FORMAT__, elf64
 %define WRT_OPT        wrt ..plt
%else
 %define WRT_OPT
%endif

%macro mk_global 1-3
  %ifdef __NASM_VER__
    %ifidn __OUTPUT_FORMAT__, macho64
	global %1
    %elifidn __OUTPUT_FORMAT__, win64
	global %1
    %else
	global %1:%2 %3
    %endif
  %else
	global %1:%2 %3
  %endif
%endmacro


; Fixes for nasm lack of MS proc helpers
%ifdef __NASM_VER__
  %ifidn __OUTPUT_FORMAT__, win64
    %macro alloc_stack 1
	sub	rsp, %1
    %endmacro

    %macro proc_frame 1
	%1:
    %endmacro

    %macro save_xmm128 2
	movdqa	[rsp + %2], %1
    %endmacro

    %macro save_reg 2
	mov	[rsp + %2], %1
    %endmacro

    %macro rex_push_reg	1
	push	%1
    %endmacro

    %macro push_reg 1
	push	%1
    %endmacro

    %define end_prolog
  %endif

  %define endproc_frame
%endif

%ifidn __OUTPUT_FORMAT__, macho64
 %define elf64 macho64
 mac_equ equ 1
%endif

%macro slversion 4
	section .text
	global %1_slver_%2%3%4
	global %1_slver
	%1_slver:
	%1_slver_%2%3%4:
		dw 0x%4
		db 0x%3, 0x%2
%endmacro

%endif ; ifndef _REG_SIZES_ASM_
