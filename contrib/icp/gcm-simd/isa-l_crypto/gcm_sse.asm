;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; Authors:
;       Erdinc Ozturk
;       Vinodh Gopal
;       James Guilford
;
;
; References:
;       This code was derived and highly optimized from the code described in paper:
;               Vinodh Gopal et. al. Optimized Galois-Counter-Mode Implementation on Intel Architecture Processors. August, 2010
;
;       For the shift-based reductions used in this code, we used the method described in paper:
;               Shay Gueron, Michael E. Kounavis. Intel Carry-Less Multiplication Instruction and its Usage for Computing the GCM Mode. January, 2010.
;
;
;
;
; Assumptions:
;
;
;
; iv:
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                             Salt  (From the SA)               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                     Initialization Vector                     |
;       |         (This is the sequence number from IPSec header)       |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x1                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;
;
; AAD:
;       AAD will be padded with 0 to the next 16byte multiple
;       for example, assume AAD is a u32 vector
;
;       if AAD is 8 bytes:
;       AAD[3] = {A0, A1};
;       padded AAD in xmm register = {A1 A0 0 0}
;
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                               SPI (A1)                        |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                     32-bit Sequence Number (A0)               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x0                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;                                       AAD Format with 32-bit Sequence Number
;
;       if AAD is 12 bytes:
;       AAD[3] = {A0, A1, A2};
;       padded AAD in xmm register = {A2 A1 A0 0}
;
;       0                   1                   2                   3
;       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                               SPI (A2)                        |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                 64-bit Extended Sequence Number {A1,A0}       |
;       |                                                               |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;       |                              0x0                              |
;       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
;
;        AAD Format with 64-bit Extended Sequence Number
;
;
; aadLen:
;       Must be a multiple of 4 bytes and from the definition of the spec.
;       The code additionally supports any aadLen length.
;
; TLen:
;       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
;
; poly = x^128 + x^127 + x^126 + x^121 + 1
; throughout the code, one tab and two tab indentations are used. one tab is for GHASH part, two tabs is for AES part.
;

%include "reg_sizes.asm"
%include "gcm_defines.asm"

%ifndef GCM128_MODE
%ifndef GCM192_MODE
%ifndef GCM256_MODE
%error "No GCM mode selected for gcm_sse.asm!"
%endif
%endif
%endif

%ifndef FUNCT_EXTENSION
%define FUNCT_EXTENSION
%endif

%ifdef GCM128_MODE
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _128 %+ y %+ sse %+ FUNCT_EXTENSION
%define NROUNDS 9
%endif

%ifdef GCM192_MODE
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _192 %+ y %+ sse %+ FUNCT_EXTENSION
%define NROUNDS 11
%endif

%ifdef GCM256_MODE
%define FN_NAME(x,y) aes_gcm_ %+ x %+ _256 %+ y %+ sse %+ FUNCT_EXTENSION
%define NROUNDS 13
%endif


default rel
; need to push 5 registers into stack to maintain
%define STACK_OFFSET 8*5

%define	TMP2	16*0    ; Temporary storage for AES State 2 (State 1 is stored in an XMM register)
%define	TMP3	16*1    ; Temporary storage for AES State 3
%define	TMP4	16*2    ; Temporary storage for AES State 4
%define	TMP5	16*3    ; Temporary storage for AES State 5
%define	TMP6	16*4    ; Temporary storage for AES State 6
%define	TMP7	16*5    ; Temporary storage for AES State 7
%define	TMP8	16*6    ; Temporary storage for AES State 8

%define	LOCAL_STORAGE	16*7

%ifidn __OUTPUT_FORMAT__, win64
	%define	XMM_STORAGE	16*10
%else
	%define	XMM_STORAGE	0
%endif

%define	VARIABLE_OFFSET	LOCAL_STORAGE + XMM_STORAGE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Utility Macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
; Input: A and B (128-bits each, bit-reflected)
; Output: C = A*B*x mod poly, (i.e. >>1 )
; To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
; GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GHASH_MUL  7
%define %%GH %1         ; 16 Bytes
%define %%HK %2         ; 16 Bytes
%define %%T1 %3
%define %%T2 %4
%define %%T3 %5
%define %%T4 %6
%define %%T5 %7
        ; %%GH, %%HK hold the values for the two operands which are carry-less multiplied
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ; Karatsuba Method
        movdqa  %%T1, %%GH
        pshufd  %%T2, %%GH, 01001110b
        pshufd  %%T3, %%HK, 01001110b
        pxor    %%T2, %%GH                              ; %%T2 = (a1+a0)
        pxor    %%T3, %%HK                              ; %%T3 = (b1+b0)

        pclmulqdq       %%T1, %%HK, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%GH, %%HK, 0x00                ; %%GH = a0*b0
        pclmulqdq       %%T2, %%T3, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T2, %%GH
        pxor    %%T2, %%T1                              ; %%T2 = a0*b1+a1*b0

        movdqa  %%T3, %%T2
        pslldq  %%T3, 8                                 ; shift-L %%T3 2 DWs
        psrldq  %%T2, 8                                 ; shift-R %%T2 2 DWs
        pxor    %%GH, %%T3
        pxor    %%T1, %%T2                              ; <%%T1:%%GH> holds the result of the carry-less multiplication of %%GH by %%HK


        ;first phase of the reduction
        movdqa  %%T2, %%GH
        movdqa  %%T3, %%GH
        movdqa  %%T4, %%GH                              ; move %%GH into %%T2, %%T3, %%T4 in order to perform the three shifts independently

        pslld   %%T2, 31                                ; packed right shifting << 31
        pslld   %%T3, 30                                ; packed right shifting shift << 30
        pslld   %%T4, 25                                ; packed right shifting shift << 25
        pxor    %%T2, %%T3                              ; xor the shifted versions
        pxor    %%T2, %%T4

        movdqa  %%T5, %%T2
        psrldq  %%T5, 4                                 ; shift-R %%T5 1 DW

        pslldq  %%T2, 12                                ; shift-L %%T2 3 DWs
        pxor    %%GH, %%T2                              ; first phase of the reduction complete
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;second phase of the reduction
        movdqa  %%T2,%%GH                               ; make 3 copies of %%GH (in in %%T2, %%T3, %%T4) for doing three shift operations
        movdqa  %%T3,%%GH
        movdqa  %%T4,%%GH

        psrld   %%T2,1                                  ; packed left shifting >> 1
        psrld   %%T3,2                                  ; packed left shifting >> 2
        psrld   %%T4,7                                  ; packed left shifting >> 7
        pxor    %%T2,%%T3                               ; xor the shifted versions
        pxor    %%T2,%%T4

        pxor    %%T2, %%T5
        pxor    %%GH, %%T2
        pxor    %%GH, %%T1                              ; the result is in %%T1


%endmacro


%macro PRECOMPUTE 8
%define	%%GDATA	%1
%define	%%HK	%2
%define	%%T1	%3
%define	%%T2	%4
%define	%%T3	%5
%define	%%T4	%6
%define	%%T5	%7
%define	%%T6	%8


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
        movdqa  %%T4, %%HK
        pshufd  %%T1, %%HK, 01001110b
        pxor    %%T1, %%HK
        movdqu  [%%GDATA + HashKey_k], %%T1


        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6      ;  %%T4 = HashKey^2<<1 mod poly
        movdqu  [%%GDATA + HashKey_2], %%T4                         ;  [HashKey_2] = HashKey^2<<1 mod poly
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_2_k], %%T1

        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^3<<1 mod poly
        movdqu  [%%GDATA + HashKey_3], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_3_k], %%T1


        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^4<<1 mod poly
        movdqu  [%%GDATA + HashKey_4], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_4_k], %%T1

        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^5<<1 mod poly
        movdqu  [%%GDATA + HashKey_5], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_5_k], %%T1


        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^6<<1 mod poly
        movdqu  [%%GDATA + HashKey_6], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_6_k], %%T1

        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^7<<1 mod poly
        movdqu  [%%GDATA + HashKey_7], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_7_k], %%T1

        GHASH_MUL %%T4, %%HK, %%T1, %%T2, %%T3, %%T5, %%T6              ;  %%T4 = HashKey^8<<1 mod poly
        movdqu  [%%GDATA + HashKey_8], %%T4
        pshufd  %%T1, %%T4, 01001110b
        pxor    %%T1, %%T4
        movdqu  [%%GDATA + HashKey_8_k], %%T1


%endmacro


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; READ_SMALL_DATA_INPUT: Packs xmm register with data when data input is less than 16 bytes.
; Returns 0 if data has length 0.
; Input: The input data (INPUT), that data's length (LENGTH).
; Output: The packed xmm register (OUTPUT).
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro READ_SMALL_DATA_INPUT	6
%define	%%OUTPUT		%1 ; %%OUTPUT is an xmm register
%define	%%INPUT			%2
%define	%%LENGTH		%3
%define	%%END_READ_LOCATION	%4 ; All this and the lower inputs are temp registers
%define	%%COUNTER		%5
%define	%%TMP1			%6

	pxor	%%OUTPUT, %%OUTPUT
	mov	%%COUNTER, %%LENGTH
	mov	%%END_READ_LOCATION, %%INPUT
	add	%%END_READ_LOCATION, %%LENGTH
	xor	%%TMP1, %%TMP1


	cmp	%%COUNTER, 8
	jl	%%_byte_loop_2
	pinsrq	%%OUTPUT, [%%INPUT],0		;Read in 8 bytes if they exists
	je	%%_done

	sub	%%COUNTER, 8

%%_byte_loop_1:					;Read in data 1 byte at a time while data is left
	shl	%%TMP1, 8			;This loop handles when 8 bytes were already read in
	dec	%%END_READ_LOCATION
	mov	BYTE(%%TMP1), BYTE [%%END_READ_LOCATION]
	dec	%%COUNTER
	jg	%%_byte_loop_1
	pinsrq	%%OUTPUT, %%TMP1, 1
	jmp	%%_done

%%_byte_loop_2:					;Read in data 1 byte at a time while data is left
	cmp	%%COUNTER, 0
	je	%%_done
	shl	%%TMP1, 8			;This loop handles when no bytes were already read in
	dec	%%END_READ_LOCATION
	mov	BYTE(%%TMP1), BYTE [%%END_READ_LOCATION]
	dec	%%COUNTER
	jg	%%_byte_loop_2
	pinsrq	%%OUTPUT, %%TMP1, 0
%%_done:

%endmacro ; READ_SMALL_DATA_INPUT


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; CALC_AAD_HASH: Calculates the hash of the data which will not be encrypted.
; Input: The input data (A_IN), that data's length (A_LEN), and the hash key (HASH_KEY).
; Output: The hash of the data (AAD_HASH).
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro	CALC_AAD_HASH	14
%define	%%A_IN		%1
%define	%%A_LEN		%2
%define	%%AAD_HASH	%3
%define	%%HASH_KEY	%4
%define	%%XTMP1		%5	; xmm temp reg 5
%define	%%XTMP2		%6
%define	%%XTMP3		%7
%define	%%XTMP4		%8
%define	%%XTMP5		%9	; xmm temp reg 5
%define	%%T1		%10	; temp reg 1
%define	%%T2		%11
%define	%%T3		%12
%define	%%T4		%13
%define	%%T5		%14	; temp reg 5


	mov	%%T1, %%A_IN		; T1 = AAD
	mov	%%T2, %%A_LEN		; T2 = aadLen
	pxor	%%AAD_HASH, %%AAD_HASH

	cmp	%%T2, 16
	jl	%%_get_small_AAD_block

%%_get_AAD_loop16:

	movdqu	%%XTMP1, [%%T1]
	;byte-reflect the AAD data
	pshufb	%%XTMP1, [SHUF_MASK]
	pxor	%%AAD_HASH, %%XTMP1
	GHASH_MUL	%%AAD_HASH, %%HASH_KEY, %%XTMP1, %%XTMP2, %%XTMP3, %%XTMP4, %%XTMP5

	sub	%%T2, 16
	je	%%_CALC_AAD_done

	add	%%T1, 16
	cmp	%%T2, 16
	jge	%%_get_AAD_loop16

%%_get_small_AAD_block:
	READ_SMALL_DATA_INPUT	%%XTMP1, %%T1, %%T2, %%T3, %%T4, %%T5
	;byte-reflect the AAD data
	pshufb	%%XTMP1, [SHUF_MASK]
	pxor	%%AAD_HASH, %%XTMP1
	GHASH_MUL	%%AAD_HASH, %%HASH_KEY, %%XTMP1, %%XTMP2, %%XTMP3, %%XTMP4, %%XTMP5

%%_CALC_AAD_done:

%endmacro ; CALC_AAD_HASH



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; PARTIAL_BLOCK: Handles encryption/decryption and the tag partial blocks between update calls.
; Requires the input data be at least 1 byte long.
; Input: gcm_key_data (GDATA_KEY), gcm_context_data (GDATA_CTX), input text (PLAIN_CYPH_IN),
; input text length (PLAIN_CYPH_LEN), the current data offset (DATA_OFFSET),
; and whether encoding or decoding (ENC_DEC).
; Output: A cypher of the first partial block (CYPH_PLAIN_OUT), and updated GDATA_CTX
; Clobbers rax, r10, r12, r13, r15, xmm0, xmm1, xmm2, xmm3, xmm5, xmm6, xmm9, xmm10, xmm11, xmm13
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PARTIAL_BLOCK	8
%define	%%GDATA_KEY		%1
%define	%%GDATA_CTX		%2
%define	%%CYPH_PLAIN_OUT	%3
%define	%%PLAIN_CYPH_IN		%4
%define	%%PLAIN_CYPH_LEN	%5
%define	%%DATA_OFFSET		%6
%define	%%AAD_HASH		%7
%define	%%ENC_DEC		%8
	mov	r13, [%%GDATA_CTX + PBlockLen]
	cmp	r13, 0
	je	%%_partial_block_done		;Leave Macro if no partial blocks

	cmp	%%PLAIN_CYPH_LEN, 16		;Read in input data without over reading
	jl	%%_fewer_than_16_bytes
	XLDR	xmm1, [%%PLAIN_CYPH_IN]		;If more than 16 bytes of data, just fill the xmm register
	jmp	%%_data_read

%%_fewer_than_16_bytes:
	lea	r10, [%%PLAIN_CYPH_IN + %%DATA_OFFSET]
	READ_SMALL_DATA_INPUT	xmm1, r10, %%PLAIN_CYPH_LEN, rax, r12, r15
	mov	r13, [%%GDATA_CTX + PBlockLen]

%%_data_read:				;Finished reading in data


	movdqu	xmm9, [%%GDATA_CTX + PBlockEncKey]	;xmm9 = ctx_data.partial_block_enc_key
	movdqu	xmm13, [%%GDATA_KEY + HashKey]

	lea	r12, [SHIFT_MASK]

	add	r12, r13			; adjust the shuffle mask pointer to be able to shift r13 bytes (16-r13 is the number of bytes in plaintext mod 16)
	movdqu	xmm2, [r12]			; get the appropriate shuffle mask
	pshufb	xmm9, xmm2			;shift right r13 bytes

%ifidn	%%ENC_DEC, DEC
	movdqa	xmm3, xmm1
	pxor	xmm9, xmm1			; Cyphertext XOR E(K, Yn)

	mov	r15, %%PLAIN_CYPH_LEN
	add	r15, r13
	sub	r15, 16				;Set r15 to be the amount of data left in CYPH_PLAIN_IN after filling the block
	jge	%%_no_extra_mask_1		;Determine if if partial block is not being filled and shift mask accordingly
	sub	r12, r15
%%_no_extra_mask_1:

	movdqu	xmm1, [r12 + ALL_F-SHIFT_MASK]	; get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	xmm9, xmm1			; mask out bottom r13 bytes of xmm9

	pand	xmm3, xmm1
	pshufb	xmm3, [SHUF_MASK]
	pshufb	xmm3, xmm2
	pxor	%%AAD_HASH, xmm3


	cmp	r15,0
	jl	%%_partial_incomplete_1

	GHASH_MUL	%%AAD_HASH, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6	;GHASH computation for the last <16 Byte block
	xor	rax,rax
	mov	[%%GDATA_CTX + PBlockLen], rax
	jmp	%%_dec_done
%%_partial_incomplete_1:
	add	[%%GDATA_CTX + PBlockLen], %%PLAIN_CYPH_LEN
%%_dec_done:
	movdqu	[%%GDATA_CTX + AadHash], %%AAD_HASH

%else
	pxor	xmm9, xmm1	; Plaintext XOR E(K, Yn)

	mov	r15, %%PLAIN_CYPH_LEN
	add	r15, r13
	sub	r15, 16				;Set r15 to be the amount of data left in CYPH_PLAIN_IN after filling the block
	jge	%%_no_extra_mask_2		;Determine if if partial block is not being filled and shift mask accordingly
	sub	r12, r15
%%_no_extra_mask_2:

	movdqu	xmm1, [r12 + ALL_F-SHIFT_MASK]	; get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	xmm9, xmm1			; mask out bottom r13  bytes of xmm9

	pshufb	xmm9, [SHUF_MASK]
	pshufb	xmm9, xmm2
	pxor	%%AAD_HASH, xmm9

	cmp	r15,0
	jl	%%_partial_incomplete_2

	GHASH_MUL	%%AAD_HASH, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6	;GHASH computation for the last <16 Byte block
	xor	rax,rax
	mov	[%%GDATA_CTX + PBlockLen], rax
	jmp	%%_encode_done
%%_partial_incomplete_2:
	add     [%%GDATA_CTX + PBlockLen], %%PLAIN_CYPH_LEN
%%_encode_done:
	movdqu	[%%GDATA_CTX + AadHash], %%AAD_HASH

	pshufb	xmm9, [SHUF_MASK]	; shuffle xmm9 back to output as ciphertext
	pshufb	xmm9, xmm2
%endif


	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	; output encrypted Bytes
	cmp	r15,0
	jl	%%_partial_fill
	mov	r12, r13
	mov	r13, 16
	sub	r13, r12			; Set r13 to be the number of bytes to write out
	jmp	%%_count_set
%%_partial_fill:
	mov	r13, %%PLAIN_CYPH_LEN
%%_count_set:
	movq	rax, xmm9
	cmp	r13, 8
	jle	%%_less_than_8_bytes_left

	mov	[%%CYPH_PLAIN_OUT+ %%DATA_OFFSET], rax
	add	%%DATA_OFFSET, 8
	psrldq	xmm9, 8
	movq	rax, xmm9
	sub	r13, 8
%%_less_than_8_bytes_left:
	mov	BYTE [%%CYPH_PLAIN_OUT + %%DATA_OFFSET], al
	add	%%DATA_OFFSET, 1
	shr	rax, 8
	sub	r13, 1
	jne	%%_less_than_8_bytes_left
         ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%%_partial_block_done:
%endmacro ; PARTIAL_BLOCK


; if a = number of total plaintext bytes
; b = floor(a/16)
; %%num_initial_blocks = b mod 8;
; encrypt the initial %%num_initial_blocks blocks and apply ghash on the ciphertext
; %%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r14 are used as a pointer only, not modified
; Updated AAD_HASH is returned in %%T3

%macro INITIAL_BLOCKS 24
%define	%%GDATA_KEY		%1
%define	%%GDATA_CTX		%2
%define	%%CYPH_PLAIN_OUT	%3
%define	%%PLAIN_CYPH_IN		%4
%define	%%LENGTH		%5
%define	%%DATA_OFFSET		%6
%define	%%num_initial_blocks	%7	; can be 0, 1, 2, 3, 4, 5, 6 or 7
%define	%%T1		%8
%define	%%HASH_KEY	%9
%define	%%T3		%10
%define	%%T4		%11
%define	%%T5		%12
%define	%%CTR		%13
%define	%%XMM1		%14
%define	%%XMM2		%15
%define	%%XMM3		%16
%define	%%XMM4		%17
%define	%%XMM5		%18
%define	%%XMM6		%19
%define	%%XMM7		%20
%define	%%XMM8		%21
%define	%%T6		%22
%define	%%T_key		%23
%define	%%ENC_DEC	%24

%assign i       (8-%%num_initial_blocks)
		movdqu	reg(i), %%XMM8	; move AAD_HASH to temp reg

	        ; start AES for %%num_initial_blocks blocks
	        movdqu  %%CTR, [%%GDATA_CTX + CurCount]	; %%CTR = Y0


%assign i (9-%%num_initial_blocks)
%rep %%num_initial_blocks
                paddd   %%CTR, [ONE]           ; INCR Y0
                movdqa  reg(i), %%CTR
                pshufb  reg(i), [SHUF_MASK]     ; perform a 16Byte swap
%assign i (i+1)
%endrep

movdqu  %%T_key, [%%GDATA_KEY+16*0]
%assign i (9-%%num_initial_blocks)
%rep %%num_initial_blocks
                pxor    reg(i),%%T_key
%assign i (i+1)
%endrep

%assign j 1
%rep NROUNDS							; encrypt N blocks with 13 key rounds (11 for GCM192)
movdqu  %%T_key, [%%GDATA_KEY+16*j]
%assign i (9-%%num_initial_blocks)
%rep %%num_initial_blocks
                aesenc  reg(i),%%T_key
%assign i (i+1)
%endrep

%assign j (j+1)
%endrep


movdqu  %%T_key, [%%GDATA_KEY+16*j]				; encrypt with last (14th) key round (12 for GCM192)
%assign i (9-%%num_initial_blocks)
%rep %%num_initial_blocks
                aesenclast      reg(i),%%T_key
%assign i (i+1)
%endrep

%assign i (9-%%num_initial_blocks)
%rep %%num_initial_blocks
                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET]
                pxor    reg(i), %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET], reg(i)            ; write back ciphertext for %%num_initial_blocks blocks
                add     %%DATA_OFFSET, 16
                %ifidn  %%ENC_DEC, DEC
                movdqa  reg(i), %%T1
                %endif
                pshufb  reg(i), [SHUF_MASK]     ; prepare ciphertext for GHASH computations
%assign i (i+1)
%endrep


%assign i (8-%%num_initial_blocks)
%assign j (9-%%num_initial_blocks)

%rep %%num_initial_blocks
        pxor    reg(j), reg(i)
        GHASH_MUL       reg(j), %%HASH_KEY, %%T1, %%T3, %%T4, %%T5, %%T6      ; apply GHASH on %%num_initial_blocks blocks
%assign i (i+1)
%assign j (j+1)
%endrep
        ; %%XMM8 has the current Hash Value
        movdqa  %%T3, %%XMM8

        cmp     %%LENGTH, 128
        jl      %%_initial_blocks_done                  ; no need for precomputed constants

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM1, %%CTR
                pshufb  %%XMM1, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM2, %%CTR
                pshufb  %%XMM2, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM3, %%CTR
                pshufb  %%XMM3, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM4, %%CTR
                pshufb  %%XMM4, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM5, %%CTR
                pshufb  %%XMM5, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM6, %%CTR
                pshufb  %%XMM6, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM7, %%CTR
                pshufb  %%XMM7, [SHUF_MASK]             ; perform a 16Byte swap

                paddd   %%CTR, [ONE]                   ; INCR Y0
                movdqa  %%XMM8, %%CTR
                pshufb  %%XMM8, [SHUF_MASK]             ; perform a 16Byte swap

                movdqu  %%T_key, [%%GDATA_KEY+16*0]
                pxor    %%XMM1, %%T_key
                pxor    %%XMM2, %%T_key
                pxor    %%XMM3, %%T_key
                pxor    %%XMM4, %%T_key
                pxor    %%XMM5, %%T_key
                pxor    %%XMM6, %%T_key
                pxor    %%XMM7, %%T_key
                pxor    %%XMM8, %%T_key


%assign i 1
%rep    NROUNDS       						; do early (13) rounds (11 for GCM192)
                movdqu  %%T_key, [%%GDATA_KEY+16*i]
                aesenc  %%XMM1, %%T_key
                aesenc  %%XMM2, %%T_key
                aesenc  %%XMM3, %%T_key
                aesenc  %%XMM4, %%T_key
                aesenc  %%XMM5, %%T_key
                aesenc  %%XMM6, %%T_key
                aesenc  %%XMM7, %%T_key
                aesenc  %%XMM8, %%T_key
%assign i (i+1)
%endrep


                movdqu          %%T_key, [%%GDATA_KEY+16*i]		; do final key round
                aesenclast      %%XMM1, %%T_key
                aesenclast      %%XMM2, %%T_key
                aesenclast      %%XMM3, %%T_key
                aesenclast      %%XMM4, %%T_key
                aesenclast      %%XMM5, %%T_key
                aesenclast      %%XMM6, %%T_key
                aesenclast      %%XMM7, %%T_key
                aesenclast      %%XMM8, %%T_key

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*0]
                pxor    %%XMM1, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*0], %%XMM1
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM1, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*1]
                pxor    %%XMM2, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*1], %%XMM2
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM2, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*2]
                pxor    %%XMM3, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*2], %%XMM3
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM3, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*3]
                pxor    %%XMM4, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*3], %%XMM4
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM4, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*4]
                pxor    %%XMM5, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*4], %%XMM5
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM5, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*5]
                pxor    %%XMM6, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*5], %%XMM6
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM6, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*6]
                pxor    %%XMM7, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*6], %%XMM7
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM7, %%T1
                %endif

                XLDR  %%T1, [%%PLAIN_CYPH_IN + %%DATA_OFFSET + 16*7]
                pxor    %%XMM8, %%T1
                XSTR  [%%CYPH_PLAIN_OUT + %%DATA_OFFSET + 16*7], %%XMM8
                %ifidn  %%ENC_DEC, DEC
                movdqa  %%XMM8, %%T1
                %endif

                add     %%DATA_OFFSET, 128

                pshufb  %%XMM1, [SHUF_MASK]             ; perform a 16Byte swap
                pxor    %%XMM1, %%T3                    ; combine GHASHed value with the corresponding ciphertext
                pshufb  %%XMM2, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM3, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM4, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM5, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM6, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM7, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM8, [SHUF_MASK]             ; perform a 16Byte swap

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%%_initial_blocks_done:


%endmacro



; encrypt 8 blocks at a time
; ghash the 8 previously encrypted ciphertext blocks
; %%GDATA (KEY), %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN are used as pointers only, not modified
; %%DATA_OFFSET is the data offset value
%macro GHASH_8_ENCRYPT_8_PARALLEL 22
%define	%%GDATA			%1
%define	%%CYPH_PLAIN_OUT	%2
%define	%%PLAIN_CYPH_IN		%3
%define	%%DATA_OFFSET		%4
%define	%%T1	%5
%define	%%T2	%6
%define	%%T3	%7
%define	%%T4	%8
%define	%%T5	%9
%define	%%T6	%10
%define	%%CTR	%11
%define	%%XMM1	%12
%define	%%XMM2	%13
%define	%%XMM3	%14
%define	%%XMM4	%15
%define	%%XMM5	%16
%define	%%XMM6	%17
%define	%%XMM7	%18
%define	%%XMM8	%19
%define	%%T7	%20
%define	%%loop_idx	%21
%define	%%ENC_DEC	%22

        movdqa  %%T7, %%XMM1
        movdqu  [rsp + TMP2], %%XMM2
        movdqu  [rsp + TMP3], %%XMM3
        movdqu  [rsp + TMP4], %%XMM4
        movdqu  [rsp + TMP5], %%XMM5
        movdqu  [rsp + TMP6], %%XMM6
        movdqu  [rsp + TMP7], %%XMM7
        movdqu  [rsp + TMP8], %%XMM8

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Karatsuba Method

        movdqa  %%T4, %%T7
        pshufd  %%T6, %%T7, 01001110b
        pxor    %%T6, %%T7
                %ifidn %%loop_idx, in_order
                paddd  %%CTR, [ONE]                    ; INCR CNT
                %else
                paddd  %%CTR, [ONEf]                   ; INCR CNT
                %endif
        movdqu  %%T5, [%%GDATA + HashKey_8]
        pclmulqdq       %%T4, %%T5, 0x11                        ; %%T1 = a1*b1
        pclmulqdq       %%T7, %%T5, 0x00                        ; %%T7 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_8_k]
        pclmulqdq       %%T6, %%T5, 0x00                        ; %%T2 = (a1+a0)*(b1+b0)
                movdqa %%XMM1, %%CTR

                %ifidn %%loop_idx, in_order
                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM2, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM3, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM4, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM5, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM6, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM7, %%CTR

                paddd  %%CTR, [ONE]                    ; INCR CNT
                movdqa %%XMM8, %%CTR

                pshufb  %%XMM1, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM2, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM3, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM4, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM5, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM6, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM7, [SHUF_MASK]             ; perform a 16Byte swap
                pshufb  %%XMM8, [SHUF_MASK]             ; perform a 16Byte swap
                %else
                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM2, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM3, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM4, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM5, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM6, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM7, %%CTR

                paddd  %%CTR, [ONEf]                   ; INCR CNT
                movdqa %%XMM8, %%CTR
                %endif
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

                movdqu  %%T1, [%%GDATA + 16*0]
                pxor    %%XMM1, %%T1
                pxor    %%XMM2, %%T1
                pxor    %%XMM3, %%T1
                pxor    %%XMM4, %%T1
                pxor    %%XMM5, %%T1
                pxor    %%XMM6, %%T1
                pxor    %%XMM7, %%T1
                pxor    %%XMM8, %%T1

        ;; %%XMM6, %%T5 hold the values for the two operands which are carry-less multiplied
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Karatsuba Method
        movdqu  %%T1, [rsp + TMP2]
        movdqa  %%T3, %%T1

        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_7]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_7_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2

                movdqu  %%T1, [%%GDATA + 16*1]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1


                movdqu  %%T1, [%%GDATA + 16*2]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ; Karatsuba Method
        movdqu  %%T1, [rsp + TMP3]
        movdqa  %%T3, %%T1
        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_6]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_6_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2

                movdqu  %%T1, [%%GDATA + 16*3]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1

        movdqu  %%T1, [rsp + TMP4]
        movdqa  %%T3, %%T1
        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_5]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_5_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2

                movdqu  %%T1, [%%GDATA + 16*4]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1

                movdqu  %%T1, [%%GDATA + 16*5]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1

        movdqu  %%T1, [rsp + TMP5]
        movdqa  %%T3, %%T1
        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_4]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_4_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2


                movdqu  %%T1, [%%GDATA + 16*6]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1
        movdqu  %%T1, [rsp + TMP6]
        movdqa  %%T3, %%T1
        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_3]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_3_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2

                movdqu  %%T1, [%%GDATA + 16*7]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1

        movdqu  %%T1, [rsp + TMP7]
        movdqa  %%T3, %%T1
        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey_2]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_2_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T4, %%T1                              ; accumulate the results in %%T4:%%T7, %%T6 holds the middle part
        pxor    %%T7, %%T3
        pxor    %%T6, %%T2

                movdqu  %%T1, [%%GDATA + 16*8]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1


        ;; %%XMM8, %%T5 hold the values for the two operands which are carry-less multiplied
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Karatsuba Method
        movdqu  %%T1, [rsp + TMP8]
        movdqa  %%T3, %%T1

        pshufd  %%T2, %%T3, 01001110b
        pxor    %%T2, %%T3
        movdqu  %%T5, [%%GDATA + HashKey]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1
        pclmulqdq       %%T3, %%T5, 0x00                ; %%T3 = a0*b0
        movdqu  %%T5, [%%GDATA + HashKey_k]
        pclmulqdq       %%T2, %%T5, 0x00                ; %%T2 = (a1+a0)*(b1+b0)
        pxor    %%T7, %%T3
        pxor    %%T4, %%T1

                movdqu  %%T1, [%%GDATA + 16*9]
                aesenc  %%XMM1, %%T1
                aesenc  %%XMM2, %%T1
                aesenc  %%XMM3, %%T1
                aesenc  %%XMM4, %%T1
                aesenc  %%XMM5, %%T1
                aesenc  %%XMM6, %%T1
                aesenc  %%XMM7, %%T1
                aesenc  %%XMM8, %%T1


%ifdef GCM128_MODE
		movdqu	%%T5, [%%GDATA + 16*10]
%endif
%ifdef GCM192_MODE
		movdqu	%%T1, [%%GDATA + 16*10]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

		movdqu	%%T1, [%%GDATA + 16*11]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

		movdqu	%%T5, [%%GDATA + 16*12]        ; finish last key round
%endif
%ifdef GCM256_MODE
		movdqu	%%T1, [%%GDATA + 16*10]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

		movdqu	%%T1, [%%GDATA + 16*11]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

		movdqu	%%T1, [%%GDATA + 16*12]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

		movdqu	%%T1, [%%GDATA + 16*13]
		aesenc	%%XMM1, %%T1
		aesenc	%%XMM2, %%T1
		aesenc	%%XMM3, %%T1
		aesenc	%%XMM4, %%T1
		aesenc	%%XMM5, %%T1
		aesenc	%%XMM6, %%T1
		aesenc	%%XMM7, %%T1
		aesenc	%%XMM8, %%T1

	        movdqu	%%T5, [%%GDATA + 16*14]        ; finish last key round
%endif

%assign i 0
%assign j 1
%rep 8
                XLDR  %%T1, [%%PLAIN_CYPH_IN+%%DATA_OFFSET+16*i]

%ifidn %%ENC_DEC, DEC
                movdqa  %%T3, %%T1
%endif

                pxor    %%T1, %%T5
                aesenclast      reg(j), %%T1          ; XMM1:XMM8
                XSTR  [%%CYPH_PLAIN_OUT+%%DATA_OFFSET+16*i], reg(j)       ; Write to the Output buffer

%ifidn %%ENC_DEC, DEC
                movdqa  reg(j), %%T3
%endif
%assign i (i+1)
%assign j (j+1)
%endrep




        pxor    %%T2, %%T6
        pxor    %%T2, %%T4
        pxor    %%T2, %%T7


        movdqa  %%T3, %%T2
        pslldq  %%T3, 8                                 ; shift-L %%T3 2 DWs
        psrldq  %%T2, 8                                 ; shift-R %%T2 2 DWs
        pxor    %%T7, %%T3
        pxor    %%T4, %%T2                              ; accumulate the results in %%T4:%%T7



        ;first phase of the reduction
        movdqa  %%T2, %%T7
        movdqa  %%T3, %%T7
        movdqa  %%T1, %%T7                              ; move %%T7 into %%T2, %%T3, %%T1 in order to perform the three shifts independently

        pslld   %%T2, 31                                ; packed right shifting << 31
        pslld   %%T3, 30                                ; packed right shifting shift << 30
        pslld   %%T1, 25                                ; packed right shifting shift << 25
        pxor    %%T2, %%T3                              ; xor the shifted versions
        pxor    %%T2, %%T1

        movdqa  %%T5, %%T2
        psrldq  %%T5, 4                                 ; shift-R %%T5 1 DW

        pslldq  %%T2, 12                                ; shift-L %%T2 3 DWs
        pxor    %%T7, %%T2                              ; first phase of the reduction complete
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

                pshufb  %%XMM1, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM2, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM3, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM4, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM5, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM6, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM7, [SHUF_MASK]     ; perform a 16Byte swap
                pshufb  %%XMM8, [SHUF_MASK]     ; perform a 16Byte swap

        ;second phase of the reduction
        movdqa  %%T2,%%T7                               ; make 3 copies of %%T7 (in in %%T2, %%T3, %%T1) for doing three shift operations
        movdqa  %%T3,%%T7
        movdqa  %%T1,%%T7

        psrld   %%T2,1                                  ; packed left shifting >> 1
        psrld   %%T3,2                                  ; packed left shifting >> 2
        psrld   %%T1,7                                  ; packed left shifting >> 7
        pxor    %%T2,%%T3                               ; xor the shifted versions
        pxor    %%T2,%%T1

        pxor    %%T2, %%T5
        pxor    %%T7, %%T2
        pxor    %%T7, %%T4                              ; the result is in %%T4


        pxor    %%XMM1, %%T7

%endmacro


; GHASH the last 4 ciphertext blocks.
%macro	GHASH_LAST_8 16
%define	%%GDATA	%1
%define	%%T1	%2
%define	%%T2	%3
%define	%%T3	%4
%define	%%T4	%5
%define	%%T5	%6
%define	%%T6	%7
%define	%%T7	%8
%define	%%XMM1	%9
%define	%%XMM2	%10
%define	%%XMM3	%11
%define	%%XMM4	%12
%define	%%XMM5	%13
%define	%%XMM6	%14
%define	%%XMM7	%15
%define	%%XMM8	%16

        ; Karatsuba Method
        movdqa  %%T6, %%XMM1
        pshufd  %%T2, %%XMM1, 01001110b
        pxor    %%T2, %%XMM1
        movdqu  %%T5, [%%GDATA + HashKey_8]
        pclmulqdq       %%T6, %%T5, 0x11                ; %%T6 = a1*b1

        pclmulqdq       %%XMM1, %%T5, 0x00              ; %%XMM1 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_8_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        movdqa  %%T7, %%XMM1
        movdqa  %%XMM1, %%T2                            ; result in %%T6, %%T7, %%XMM1


        ; Karatsuba Method
        movdqa  %%T1, %%XMM2
        pshufd  %%T2, %%XMM2, 01001110b
        pxor    %%T2, %%XMM2
        movdqu  %%T5, [%%GDATA + HashKey_7]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM2, %%T5, 0x00              ; %%XMM2 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_7_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM2
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1


        ; Karatsuba Method
        movdqa  %%T1, %%XMM3
        pshufd  %%T2, %%XMM3, 01001110b
        pxor    %%T2, %%XMM3
        movdqu  %%T5, [%%GDATA + HashKey_6]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM3, %%T5, 0x00              ; %%XMM3 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_6_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM3
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1

        ; Karatsuba Method
        movdqa  %%T1, %%XMM4
        pshufd  %%T2, %%XMM4, 01001110b
        pxor    %%T2, %%XMM4
        movdqu  %%T5, [%%GDATA + HashKey_5]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM4, %%T5, 0x00              ; %%XMM3 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_5_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM4
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1

        ; Karatsuba Method
        movdqa  %%T1, %%XMM5
        pshufd  %%T2, %%XMM5, 01001110b
        pxor    %%T2, %%XMM5
        movdqu  %%T5, [%%GDATA + HashKey_4]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM5, %%T5, 0x00              ; %%XMM3 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_4_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM5
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1

        ; Karatsuba Method
        movdqa  %%T1, %%XMM6
        pshufd  %%T2, %%XMM6, 01001110b
        pxor    %%T2, %%XMM6
        movdqu  %%T5, [%%GDATA + HashKey_3]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM6, %%T5, 0x00              ; %%XMM3 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_3_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM6
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1

        ; Karatsuba Method
        movdqa  %%T1, %%XMM7
        pshufd  %%T2, %%XMM7, 01001110b
        pxor    %%T2, %%XMM7
        movdqu  %%T5, [%%GDATA + HashKey_2]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM7, %%T5, 0x00              ; %%XMM3 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_2_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM7
        pxor    %%XMM1, %%T2                            ; results accumulated in %%T6, %%T7, %%XMM1


        ; Karatsuba Method
        movdqa  %%T1, %%XMM8
        pshufd  %%T2, %%XMM8, 01001110b
        pxor    %%T2, %%XMM8
        movdqu  %%T5, [%%GDATA + HashKey]
        pclmulqdq       %%T1, %%T5, 0x11                ; %%T1 = a1*b1

        pclmulqdq       %%XMM8, %%T5, 0x00              ; %%XMM4 = a0*b0
        movdqu  %%T4, [%%GDATA + HashKey_k]
        pclmulqdq       %%T2, %%T4, 0x00                ; %%T2 = (a1+a0)*(b1+b0)

        pxor    %%T6, %%T1
        pxor    %%T7, %%XMM8
        pxor    %%T2, %%XMM1
        pxor    %%T2, %%T6
        pxor    %%T2, %%T7                              ; middle section of the temp results combined as in Karatsuba algorithm


        movdqa  %%T4, %%T2
        pslldq  %%T4, 8                                 ; shift-L %%T4 2 DWs
        psrldq  %%T2, 8                                 ; shift-R %%T2 2 DWs
        pxor    %%T7, %%T4
        pxor    %%T6, %%T2                              ; <%%T6:%%T7> holds the result of the accumulated carry-less multiplications


        ;first phase of the reduction
        movdqa %%T2, %%T7
        movdqa %%T3, %%T7
        movdqa %%T4, %%T7                               ; move %%T7 into %%T2, %%T3, %%T4 in order to perform the three shifts independently

        pslld %%T2, 31                                  ; packed right shifting << 31
        pslld %%T3, 30                                  ; packed right shifting shift << 30
        pslld %%T4, 25                                  ; packed right shifting shift << 25
        pxor %%T2, %%T3                                 ; xor the shifted versions
        pxor %%T2, %%T4

        movdqa %%T1, %%T2
        psrldq %%T1, 4                                  ; shift-R %%T1 1 DW

        pslldq %%T2, 12                                 ; shift-L %%T2 3 DWs
        pxor %%T7, %%T2                                 ; first phase of the reduction complete
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;second phase of the reduction
        movdqa %%T2,%%T7                                ; make 3 copies of %%T7 (in in %%T2, %%T3, %%T4) for doing three shift operations
        movdqa %%T3,%%T7
        movdqa %%T4,%%T7

        psrld %%T2,1                                    ; packed left shifting >> 1
        psrld %%T3,2                                    ; packed left shifting >> 2
        psrld %%T4,7                                    ; packed left shifting >> 7
        pxor %%T2,%%T3                                  ; xor the shifted versions
        pxor %%T2,%%T4

        pxor %%T2, %%T1
        pxor %%T7, %%T2
        pxor %%T6, %%T7                                 ; the result is in %%T6

%endmacro

; Encryption of a single block
%macro ENCRYPT_SINGLE_BLOCK 3
%define	%%GDATA	%1
%define	%%ST	%2
%define	%%T1	%3
		movdqu	%%T1, [%%GDATA+16*0]
                pxor    %%ST, %%T1
%assign i 1
%rep NROUNDS
		movdqu	%%T1, [%%GDATA+16*i]
                aesenc  %%ST, %%T1
%assign i (i+1)
%endrep
		movdqu	%%T1, [%%GDATA+16*i]
                aesenclast      %%ST, %%T1
%endmacro


;; Start of Stack Setup

%macro FUNC_SAVE 0
	;; Required for Update/GMC_ENC
	;the number of pushes must equal STACK_OFFSET
        push    r12
        push    r13
        push    r14
        push    r15
        push    rsi
        mov     r14, rsp

	sub     rsp, VARIABLE_OFFSET
	and     rsp, ~63

%ifidn __OUTPUT_FORMAT__, win64
        ; xmm6:xmm15 need to be maintained for Windows
        movdqu [rsp + LOCAL_STORAGE + 0*16],xmm6
        movdqu [rsp + LOCAL_STORAGE + 1*16],xmm7
        movdqu [rsp + LOCAL_STORAGE + 2*16],xmm8
        movdqu [rsp + LOCAL_STORAGE + 3*16],xmm9
        movdqu [rsp + LOCAL_STORAGE + 4*16],xmm10
        movdqu [rsp + LOCAL_STORAGE + 5*16],xmm11
        movdqu [rsp + LOCAL_STORAGE + 6*16],xmm12
        movdqu [rsp + LOCAL_STORAGE + 7*16],xmm13
        movdqu [rsp + LOCAL_STORAGE + 8*16],xmm14
        movdqu [rsp + LOCAL_STORAGE + 9*16],xmm15

        mov	arg5, arg(5) ;[r14 + STACK_OFFSET + 8*5]
%endif
%endmacro


%macro FUNC_RESTORE 0

%ifidn __OUTPUT_FORMAT__, win64
        movdqu xmm15  , [rsp + LOCAL_STORAGE + 9*16]
        movdqu xmm14  , [rsp + LOCAL_STORAGE + 8*16]
        movdqu xmm13  , [rsp + LOCAL_STORAGE + 7*16]
        movdqu xmm12  , [rsp + LOCAL_STORAGE + 6*16]
        movdqu xmm11  , [rsp + LOCAL_STORAGE + 5*16]
        movdqu xmm10  , [rsp + LOCAL_STORAGE + 4*16]
        movdqu xmm9 , [rsp + LOCAL_STORAGE + 3*16]
        movdqu xmm8 , [rsp + LOCAL_STORAGE + 2*16]
        movdqu xmm7 , [rsp + LOCAL_STORAGE + 1*16]
        movdqu xmm6 , [rsp + LOCAL_STORAGE + 0*16]
%endif

;; Required for Update/GMC_ENC
        mov     rsp, r14
        pop     rsi
        pop     r15
        pop     r14
        pop     r13
        pop     r12
%endmacro


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GCM_INIT initializes a gcm_context_data struct to prepare for encoding/decoding.
; Input: gcm_key_data * (GDATA_KEY), gcm_context_data *(GDATA_CTX), IV,
; Additional Authentication data (A_IN), Additional Data length (A_LEN).
; Output: Updated GDATA_CTX with the hash of A_IN (AadHash) and initialized other parts of GDATA.
; Clobbers rax, r10-r13 and xmm0-xmm6
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro  GCM_INIT 	5
%define %%GDATA_KEY	%1
%define %%GDATA_CTX	%2
%define %%IV		%3
%define %%A_IN		%4
%define %%A_LEN		%5
%define %%AAD_HASH	xmm0
%define %%SUBHASH	xmm1


        movdqu  %%SUBHASH, [%%GDATA_KEY + HashKey]

	CALC_AAD_HASH %%A_IN, %%A_LEN, %%AAD_HASH, %%SUBHASH, xmm2, xmm3, xmm4, xmm5, xmm6, r10, r11, r12, r13, rax
	pxor	xmm2, xmm3
	mov	r10, %%A_LEN

	movdqu	[%%GDATA_CTX + AadHash], %%AAD_HASH	; ctx_data.aad hash = aad_hash
	mov	[%%GDATA_CTX + AadLen], r10		; ctx_data.aad_length = aad_length
	xor	r10, r10
	mov	[%%GDATA_CTX + InLen], r10		; ctx_data.in_length = 0
	mov	[%%GDATA_CTX + PBlockLen], r10		; ctx_data.partial_block_length = 0
	movdqu	[%%GDATA_CTX + PBlockEncKey], xmm2	; ctx_data.partial_block_enc_key = 0
	mov	r10, %%IV
        movdqa  xmm2, [rel ONEf]                        ; read 12 IV bytes and pad with 0x00000001
        pinsrq  xmm2, [r10], 0
        pinsrd  xmm2, [r10+8], 2
	movdqu	[%%GDATA_CTX + OrigIV], xmm2		; ctx_data.orig_IV = iv

	pshufb xmm2, [SHUF_MASK]

	movdqu	[%%GDATA_CTX + CurCount], xmm2		; ctx_data.current_counter = iv
%endmacro


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GCM_ENC_DEC Encodes/Decodes given data. Assumes that the passed gcm_context_data
; struct has been initialized by GCM_INIT.
; Requires the input data be at least 1 byte long because of READ_SMALL_INPUT_DATA.
; Input: gcm_key_data * (GDATA_KEY), gcm_context_data (GDATA_CTX), input text (PLAIN_CYPH_IN),
; input text length (PLAIN_CYPH_LEN) and whether encoding or decoding (ENC_DEC)
; Output: A cypher of the given plain text (CYPH_PLAIN_OUT), and updated GDATA_CTX
; Clobbers rax, r10-r15, and xmm0-xmm15
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro	GCM_ENC_DEC		6
%define	%%GDATA_KEY		%1
%define	%%GDATA_CTX		%2
%define	%%CYPH_PLAIN_OUT	%3
%define	%%PLAIN_CYPH_IN		%4
%define	%%PLAIN_CYPH_LEN	%5
%define	%%ENC_DEC		%6
%define	%%DATA_OFFSET		r11

; Macro flow:
; calculate the number of 16byte blocks in the message
; process (number of 16byte blocks) mod 8 '%%_initial_num_blocks_is_# .. %%_initial_blocks_encrypted'
; process 8 16 byte blocks at a time until all are done '%%_encrypt_by_8_new .. %%_eight_cipher_left'
; if there is a block of less tahn 16 bytes process it '%%_zero_cipher_left .. %%_multiple_of_16_bytes'

	cmp	%%PLAIN_CYPH_LEN, 0
	je	%%_multiple_of_16_bytes

	xor	%%DATA_OFFSET, %%DATA_OFFSET
	add	[%%GDATA_CTX + InLen], %%PLAIN_CYPH_LEN ;Update length of data processed
	movdqu	xmm13, [%%GDATA_KEY + HashKey]                 ; xmm13 = HashKey
	movdqu	xmm8, [%%GDATA_CTX + AadHash]


	PARTIAL_BLOCK %%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, %%PLAIN_CYPH_LEN, %%DATA_OFFSET, xmm8, %%ENC_DEC

        mov     r13, %%PLAIN_CYPH_LEN                               ; save the number of bytes of plaintext/ciphertext
	sub	r13, %%DATA_OFFSET
	mov	r10, r13	;save the amount of data left to process in r10
        and     r13, -16                                ; r13 = r13 - (r13 mod 16)

        mov     r12, r13
        shr     r12, 4
        and     r12, 7
        jz      %%_initial_num_blocks_is_0

        cmp     r12, 7
        je      %%_initial_num_blocks_is_7
        cmp     r12, 6
        je      %%_initial_num_blocks_is_6
        cmp     r12, 5
        je      %%_initial_num_blocks_is_5
        cmp     r12, 4
        je      %%_initial_num_blocks_is_4
        cmp     r12, 3
        je      %%_initial_num_blocks_is_3
        cmp     r12, 2
        je      %%_initial_num_blocks_is_2

        jmp     %%_initial_num_blocks_is_1

%%_initial_num_blocks_is_7:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 7, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*7
        jmp     %%_initial_blocks_encrypted

%%_initial_num_blocks_is_6:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 6, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*6
        jmp     %%_initial_blocks_encrypted

%%_initial_num_blocks_is_5:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 5, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*5
        jmp     %%_initial_blocks_encrypted

%%_initial_num_blocks_is_4:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 4, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*4
        jmp     %%_initial_blocks_encrypted


%%_initial_num_blocks_is_3:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 3, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*3
        jmp     %%_initial_blocks_encrypted
%%_initial_num_blocks_is_2:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 2, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16*2
        jmp     %%_initial_blocks_encrypted

%%_initial_num_blocks_is_1:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 1, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC
        sub     r13, 16
        jmp     %%_initial_blocks_encrypted

%%_initial_num_blocks_is_0:
	INITIAL_BLOCKS	%%GDATA_KEY, %%GDATA_CTX, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, r13, %%DATA_OFFSET, 0, xmm12, xmm13, xmm14, xmm15, xmm11, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm10, xmm0, %%ENC_DEC


%%_initial_blocks_encrypted:
        cmp     r13, 0
        je      %%_zero_cipher_left

        sub     r13, 128
        je      %%_eight_cipher_left




        movd    r15d, xmm9
        and     r15d, 255
        pshufb  xmm9, [SHUF_MASK]


%%_encrypt_by_8_new:
        cmp     r15d, 255-8
        jg      %%_encrypt_by_8



        add     r15b, 8
	GHASH_8_ENCRYPT_8_PARALLEL	%%GDATA_KEY, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, %%DATA_OFFSET, xmm0, xmm10, xmm11, xmm12, xmm13, xmm14, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm15, out_order, %%ENC_DEC
        add     %%DATA_OFFSET, 128
        sub     r13, 128
        jne     %%_encrypt_by_8_new

        pshufb  xmm9, [SHUF_MASK]
        jmp     %%_eight_cipher_left

%%_encrypt_by_8:
        pshufb  xmm9, [SHUF_MASK]
        add     r15b, 8
	GHASH_8_ENCRYPT_8_PARALLEL	%%GDATA_KEY, %%CYPH_PLAIN_OUT, %%PLAIN_CYPH_IN, %%DATA_OFFSET, xmm0, xmm10, xmm11, xmm12, xmm13, xmm14, xmm9, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm15, in_order, %%ENC_DEC
        pshufb  xmm9, [SHUF_MASK]
        add     %%DATA_OFFSET, 128
        sub     r13, 128
        jne     %%_encrypt_by_8_new

        pshufb  xmm9, [SHUF_MASK]




%%_eight_cipher_left:
	GHASH_LAST_8	%%GDATA_KEY, xmm0, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8


%%_zero_cipher_left:
	movdqu	[%%GDATA_CTX + AadHash], xmm14
	movdqu	[%%GDATA_CTX + CurCount], xmm9

        mov     r13, r10
        and     r13, 15                                ; r13 = (%%PLAIN_CYPH_LEN mod 16)

        je      %%_multiple_of_16_bytes

	mov	[%%GDATA_CTX + PBlockLen], r13		; my_ctx.data.partial_blck_length = r13
        ; handle the last <16 Byte block seperately

        paddd   xmm9, [ONE]                     ; INCR CNT to get Yn
	movdqu	[%%GDATA_CTX + CurCount], xmm9		; my_ctx.data.current_counter = xmm9
        pshufb  xmm9, [SHUF_MASK]
	ENCRYPT_SINGLE_BLOCK	%%GDATA_KEY, xmm9, xmm2                    ; E(K, Yn)
	movdqu	[%%GDATA_CTX + PBlockEncKey], xmm9		; my_ctx_data.partial_block_enc_key = xmm9

	cmp	%%PLAIN_CYPH_LEN, 16
	jge	%%_large_enough_update

	lea	r10, [%%PLAIN_CYPH_IN + %%DATA_OFFSET]
	READ_SMALL_DATA_INPUT	xmm1, r10, r13, r12, r15, rax
	lea	r12, [SHIFT_MASK + 16]
	sub	r12, r13
	jmp	%%_data_read

%%_large_enough_update:
        sub     %%DATA_OFFSET, 16
        add     %%DATA_OFFSET, r13

        movdqu  xmm1, [%%PLAIN_CYPH_IN+%%DATA_OFFSET]                        ; receive the last <16 Byte block

	sub     %%DATA_OFFSET, r13
        add     %%DATA_OFFSET, 16

        lea     r12, [SHIFT_MASK + 16]
        sub     r12, r13                                ; adjust the shuffle mask pointer to be able to shift 16-r13 bytes (r13 is the number of bytes in plaintext mod 16)
        movdqu  xmm2, [r12]                             ; get the appropriate shuffle mask
        pshufb  xmm1, xmm2                              ; shift right 16-r13 bytes
%%_data_read:
        %ifidn  %%ENC_DEC, DEC
        movdqa  xmm2, xmm1
        pxor    xmm9, xmm1                              ; Plaintext XOR E(K, Yn)
        movdqu  xmm1, [r12 + ALL_F - SHIFT_MASK]        ; get the appropriate mask to mask out top 16-r13 bytes of xmm9
        pand    xmm9, xmm1                              ; mask out top 16-r13 bytes of xmm9
        pand    xmm2, xmm1
        pshufb  xmm2, [SHUF_MASK]
        pxor    xmm14, xmm2
	movdqu	[%%GDATA_CTX + AadHash], xmm14

        %else
        pxor    xmm9, xmm1                              ; Plaintext XOR E(K, Yn)
        movdqu  xmm1, [r12 + ALL_F - SHIFT_MASK]        ; get the appropriate mask to mask out top 16-r13 bytes of xmm9
        pand    xmm9, xmm1                              ; mask out top 16-r13 bytes of xmm9
        pshufb  xmm9, [SHUF_MASK]
        pxor    xmm14, xmm9
	movdqu	[%%GDATA_CTX + AadHash], xmm14

        pshufb  xmm9, [SHUF_MASK]               ; shuffle xmm9 back to output as ciphertext
        %endif


        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ; output r13 Bytes
        movq    rax, xmm9
        cmp     r13, 8
        jle     %%_less_than_8_bytes_left

        mov     [%%CYPH_PLAIN_OUT + %%DATA_OFFSET], rax
        add     %%DATA_OFFSET, 8
        psrldq  xmm9, 8
        movq    rax, xmm9
        sub     r13, 8

%%_less_than_8_bytes_left:
        mov     BYTE [%%CYPH_PLAIN_OUT + %%DATA_OFFSET], al
        add     %%DATA_OFFSET, 1
        shr     rax, 8
        sub     r13, 1
        jne     %%_less_than_8_bytes_left
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%%_multiple_of_16_bytes:

%endmacro


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; GCM_COMPLETE Finishes Encyrption/Decryption of last partial block after GCM_UPDATE finishes.
; Input: A gcm_key_data * (GDATA_KEY), gcm_context_data * (GDATA_CTX) and
; whether encoding or decoding (ENC_DEC).
; Output: Authorization Tag (AUTH_TAG) and Authorization Tag length (AUTH_TAG_LEN)
; Clobbers rax, r10-r12, and xmm0, xmm1, xmm5, xmm6, xmm9, xmm11, xmm14, xmm15
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro	GCM_COMPLETE		5
%define	%%GDATA_KEY		%1
%define	%%GDATA_CTX		%2
%define	%%AUTH_TAG		%3
%define	%%AUTH_TAG_LEN		%4
%define	%%ENC_DEC		%5
%define	%%PLAIN_CYPH_LEN	rax

        mov     r12, [%%GDATA_CTX + PBlockLen]		; r12 = aadLen (number of bytes)
	movdqu	xmm14, [%%GDATA_CTX + AadHash]
	movdqu	xmm13, [%%GDATA_KEY + HashKey]

	cmp	r12, 0

	je %%_partial_done

	GHASH_MUL xmm14, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6 ;GHASH computation for the last <16 Byte block
	movdqu	[%%GDATA_CTX + AadHash], xmm14

%%_partial_done:

	mov	r12, [%%GDATA_CTX + AadLen]			; r12 = aadLen (number of bytes)
	mov	%%PLAIN_CYPH_LEN, [%%GDATA_CTX + InLen]

        shl     r12, 3                                  ; convert into number of bits
        movd    xmm15, r12d                             ; len(A) in xmm15

        shl     %%PLAIN_CYPH_LEN, 3                     ; len(C) in bits  (*128)
        movq    xmm1, %%PLAIN_CYPH_LEN
        pslldq  xmm15, 8                                ; xmm15 = len(A)|| 0x0000000000000000
        pxor    xmm15, xmm1                             ; xmm15 = len(A)||len(C)

        pxor    xmm14, xmm15
        GHASH_MUL       xmm14, xmm13, xmm0, xmm10, xmm11, xmm5, xmm6    ; final GHASH computation
        pshufb  xmm14, [SHUF_MASK]                      ; perform a 16Byte swap

        movdqu  xmm9, [%%GDATA_CTX + OrigIV]            ; xmm9 = Y0

	ENCRYPT_SINGLE_BLOCK	%%GDATA_KEY, xmm9, xmm2	; E(K, Y0)

        pxor    xmm9, xmm14



%%_return_T:
	mov	r10, %%AUTH_TAG				; r10 = authTag
	mov	r11, %%AUTH_TAG_LEN			; r11 = auth_tag_len

        cmp     r11, 16
        je      %%_T_16

        cmp     r11, 12
        je      %%_T_12

%%_T_8:
        movq    rax, xmm9
        mov     [r10], rax
        jmp     %%_return_T_done
%%_T_12:
        movq    rax, xmm9
        mov     [r10], rax
        psrldq  xmm9, 8
        movd    eax, xmm9
        mov     [r10 + 8], eax
        jmp     %%_return_T_done

%%_T_16:
        movdqu  [r10], xmm9

%%_return_T_done:
%endmacro ;GCM_COMPLETE


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void	aes_gcm_precomp_128_sse / aes_gcm_precomp_192_sse / aes_gcm_precomp_256_sse
;        (struct gcm_key_data *key_data);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifnidn FUNCT_EXTENSION, _nt
global FN_NAME(precomp,_)
FN_NAME(precomp,_):
	endbranch

        push    r12
        push    r13
        push    r14
        push    r15

        mov     r14, rsp



        sub     rsp, VARIABLE_OFFSET
        and     rsp, ~63                                ; align rsp to 64 bytes

%ifidn __OUTPUT_FORMAT__, win64
        ; only xmm6 needs to be maintained
        movdqu [rsp + LOCAL_STORAGE + 0*16],xmm6
%endif

	pxor	xmm6, xmm6
	ENCRYPT_SINGLE_BLOCK	arg1, xmm6, xmm2	; xmm6 = HashKey

        pshufb  xmm6, [SHUF_MASK]
        ;;;;;;;;;;;;;;;  PRECOMPUTATION of HashKey<<1 mod poly from the HashKey;;;;;;;;;;;;;;;
        movdqa  xmm2, xmm6
        psllq   xmm6, 1
        psrlq   xmm2, 63
        movdqa  xmm1, xmm2
        pslldq  xmm2, 8
        psrldq  xmm1, 8
        por     xmm6, xmm2
        ;reduction
        pshufd  xmm2, xmm1, 00100100b
        pcmpeqd xmm2, [TWOONE]
        pand    xmm2, [POLY]
        pxor    xmm6, xmm2                             ; xmm6 holds the HashKey<<1 mod poly
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        movdqu  [arg1 + HashKey], xmm6                  ; store HashKey<<1 mod poly


        PRECOMPUTE  arg1, xmm6, xmm0, xmm1, xmm2, xmm3, xmm4, xmm5

%ifidn __OUTPUT_FORMAT__, win64
       movdqu xmm6, [rsp + LOCAL_STORAGE + 0*16]
%endif
        mov     rsp, r14

        pop     r15
        pop     r14
        pop     r13
        pop     r12
ret
%endif	; _nt


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_init_128_sse / aes_gcm_init_192_sse / aes_gcm_init_256_sse (
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *iv,
;        const   u8 *aad,
;        u64     aad_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifnidn FUNCT_EXTENSION, _nt
global FN_NAME(init,_)
FN_NAME(init,_):
	endbranch

	push	r12
	push	r13
%ifidn __OUTPUT_FORMAT__, win64
	; xmm6:xmm15 need to be maintained for Windows
        push    arg5
	sub	rsp, 1*16
	movdqu	[rsp + 0*16],xmm6
        mov     arg5, [rsp + 1*16 + 8*3 + 8*5]
%endif

	GCM_INIT arg1, arg2, arg3, arg4, arg5

%ifidn __OUTPUT_FORMAT__, win64
	movdqu	xmm6 , [rsp + 0*16]
	add	rsp, 1*16
        pop     arg5
%endif
	pop	r13
	pop	r12
        ret
%endif	; _nt


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_enc_128_update_sse / aes_gcm_enc_192_update_sse / aes_gcm_enc_256_update_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *out,
;        const   u8 *in,
;        u64     plaintext_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global FN_NAME(enc,_update_)
FN_NAME(enc,_update_):
	endbranch

	FUNC_SAVE

	GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, ENC

	FUNC_RESTORE

	ret


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_dec_256_update_sse / aes_gcm_dec_192_update_sse / aes_gcm_dec_256_update_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *out,
;        const   u8 *in,
;        u64     plaintext_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global FN_NAME(dec,_update_)
FN_NAME(dec,_update_):
	endbranch

	FUNC_SAVE

	GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, DEC

	FUNC_RESTORE

	ret


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_enc_128_finalize_sse / aes_gcm_enc_192_finalize_sse / aes_gcm_enc_256_finalize_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *auth_tag,
;        u64     auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifnidn FUNCT_EXTENSION, _nt
global FN_NAME(enc,_finalize_)
FN_NAME(enc,_finalize_):
	endbranch

	push r12

%ifidn __OUTPUT_FORMAT__, win64
	; xmm6:xmm15 need to be maintained for Windows
	sub	rsp, 5*16
	movdqu	[rsp + 0*16],xmm6
	movdqu	[rsp + 1*16],xmm9
	movdqu	[rsp + 2*16],xmm11
	movdqu	[rsp + 3*16],xmm14
	movdqu	[rsp + 4*16],xmm15
%endif
	GCM_COMPLETE	arg1, arg2, arg3, arg4, ENC

%ifidn __OUTPUT_FORMAT__, win64
	movdqu	xmm15  , [rsp + 4*16]
	movdqu	xmm14  , [rsp+ 3*16]
	movdqu	xmm11  , [rsp + 2*16]
	movdqu	xmm9 , [rsp + 1*16]
	movdqu	xmm6 , [rsp + 0*16]
	add	rsp, 5*16
%endif

	pop r12
        ret
%endif	; _nt


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_dec_128_finalize_sse / aes_gcm_dec_192_finalize_sse / aes_gcm_dec_256_finalize_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *auth_tag,
;        u64     auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%ifnidn FUNCT_EXTENSION, _nt
global FN_NAME(dec,_finalize_)
FN_NAME(dec,_finalize_):
	endbranch

	push r12

%ifidn __OUTPUT_FORMAT__, win64
	; xmm6:xmm15 need to be maintained for Windows
	sub	rsp, 5*16
	movdqu	[rsp + 0*16],xmm6
	movdqu	[rsp + 1*16],xmm9
	movdqu	[rsp + 2*16],xmm11
	movdqu	[rsp + 3*16],xmm14
	movdqu	[rsp + 4*16],xmm15
%endif
	GCM_COMPLETE	arg1, arg2, arg3, arg4, DEC

%ifidn __OUTPUT_FORMAT__, win64
	movdqu	xmm15  , [rsp + 4*16]
	movdqu	xmm14  , [rsp+ 3*16]
	movdqu	xmm11  , [rsp + 2*16]
	movdqu	xmm9 , [rsp + 1*16]
	movdqu	xmm6 , [rsp + 0*16]
	add	rsp, 5*16
%endif

	pop r12
        ret
%endif	; _nt


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_enc_128_sse / aes_gcm_enc_192_sse / aes_gcm_enc_256_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *out,
;        const   u8 *in,
;        u64     plaintext_len,
;        u8      *iv,
;        const   u8 *aad,
;        u64     aad_len,
;        u8      *auth_tag,
;        u64     auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global FN_NAME(enc,_)
FN_NAME(enc,_):
	endbranch

	FUNC_SAVE

	GCM_INIT arg1, arg2, arg6, arg7, arg8

	GCM_ENC_DEC  arg1, arg2, arg3, arg4, arg5, ENC

	GCM_COMPLETE arg1, arg2, arg9, arg10, ENC

	FUNC_RESTORE

	ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;void   aes_gcm_dec_128_sse / aes_gcm_dec_192_sse / aes_gcm_dec_256_sse
;        const struct gcm_key_data *key_data,
;        struct gcm_context_data *context_data,
;        u8      *out,
;        const   u8 *in,
;        u64     plaintext_len,
;        u8      *iv,
;        const   u8 *aad,
;        u64     aad_len,
;        u8      *auth_tag,
;        u64     auth_tag_len);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global FN_NAME(dec,_)
FN_NAME(dec,_):
	endbranch

	FUNC_SAVE

	GCM_INIT arg1, arg2, arg6, arg7, arg8

	GCM_ENC_DEC  arg1, arg2, arg3, arg4, arg5, DEC

	GCM_COMPLETE arg1, arg2, arg9, arg10, DEC

	FUNC_RESTORE

	ret
