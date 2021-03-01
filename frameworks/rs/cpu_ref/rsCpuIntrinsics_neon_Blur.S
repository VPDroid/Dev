/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define ENTRY(f) .text; .align 4; .globl f; .type f,#function; f: .fnstart
#define PRIVATE(f) .text; .align 4; .type f,#function; f: .fnstart
#define END(f) .fnend; .size f, .-f;

.eabi_attribute 25,1 @Tag_ABI_align8_preserved
.arm

/* Number of fractional bits to preserve in intermediate results.  The
 * intermediate storage is 16-bit, and we started with 8 bit data (the integer
 * part), so this should be between 0 and 8.
 */
.set FRACTION_BITS, 7

.set MAX_R, 25


/* A quick way of making a line of code conditional on some other condition.
 * Use `.set cc, 1` or `.set cc, 0` to enable or disable lines prefixed with
 * `ifcc`:
 */
.macro ifcc zzz:vararg
.if cc
            \zzz
.endif
.endm

/* Fetch 16 columns of bytes (regardless of image format), convolve these
 * vertically, and leave them in the register file.  If working near the top or
 * bottom of an image then clamp the addressing while loading the data in.
 *
 * The convolution is fully unrolled for windows up to max_r, with the
 * outermost edges calculated first.  This way it's possible to branch directly
 * into the relevant part of the code for an arbitrary convolution radius.  Two
 * variants of the loop are produced; one eliminates the clamping code for a
 * slight speed advantage.
 *
 * Where the macro is called with reg=x, the specified register is taken to
 * contain a pre-calculated pointer into one of the two loops.
 *
 * Input:
 *      r1 -- src
 *      r2 -- pitch
 *      r5 -- r
 *      r6 -- rup
 *      r7 -- rdn
 *      r12 -- switch index
 *      q0-q3 -- coefficient table
 * Output:
 *      r1 += 16
 *      q10,q11 -- 16 convolved columns
 * Modifies:
 *      r10 = upper row pointer
 *      r11 = lower row pointer
 *      q12-q15 = temporary sums
 */
.macro fetch, max_r=MAX_R, labelc=1, labelnc=2, reg=r12 /*{{{*/
  .ifc \reg,r12 ; .set cc, 1 ; .else ; .set cc, 0 ; .endif

            vld1.8      {d30,d31}, [r1]
            mls         r10, r2, r6, r1

            vmovl.u8    q14, d30
            pld         [r1, #32]
            vmovl.u8    q15, d31
  .if \max_r < 16 // approximate
    ifcc    adr         \reg, 1f
  .else
    ifcc    ldr         \reg, 2f
1:  ifcc    add         \reg, \reg, pc
  .endif

            vmull.u16   q12, d28, d0[0]
    ifcc    sub         \reg, r5, LSL #6
            vmull.u16   q13, d29, d0[0]
            mla         r11, r2, r7, r1
            vmull.u16   q14, d30, d0[0]
            add         r1, r1, #16
            vmull.u16   q15, d31, d0[0]
            bx          \reg

     ifcc   .align 2
  2: ifcc   .word       1f-1b-8

  .irp rowclamp, 1, 0
    .set cc, \rowclamp
    .align 4
    .irp dreg, 6, 5, 4, 3, 2, 1, 0 ; .irp lane, 3, 2, 1, 0
      .set i, \dreg * 4 + \lane
      .if 0 < i && i <= \max_r
        .if \rowclamp
            vld1.8      {d20,d21}, [r10]
            vld1.8      {d22,d23}, [r11]
            cmp         r6, #i
        .else
            vld1.8      {d20,d21}, [r10], r2
            vld1.8      {d22,d23}, [r11]
            sub         r11, r11, r2
        .endif
            vswp        d21, d22
            pld         [r10, #32]
            vaddl.u8    q10, d20, d21
    ifcc    addhs       r10, r10, r2
            vaddl.u8    q11, d22, d23
    ifcc    cmp         r7, #i
            vmlal.u16   q12, d20, d\dreg[\lane]
            pld         [r11, #32]
            vmlal.u16   q13, d21, d\dreg[\lane]
    ifcc    subhs       r11, r11, r2
            vmlal.u16   q14, d22, d\dreg[\lane]
    ifcc    nop
            vmlal.u16   q15, d23, d\dreg[\lane]
        .endif
    .endr ; .endr
    .if \rowclamp == 1
        1: \labelc :
            b           2f
    .else
        2: \labelnc :
    .endif
  .endr

            vqrshrn.u32 d20, q12, #16 - FRACTION_BITS
            vqrshrn.u32 d21, q13, #16 - FRACTION_BITS
            vqrshrn.u32 d22, q14, #16 - FRACTION_BITS
            vqrshrn.u32 d23, q15, #16 - FRACTION_BITS
.endm /*}}}*/

/* Some portion of the convolution window (as much as will fit, and all of it
 * for the uchar1 cases) is kept in the register file to avoid unnecessary
 * memory accesses.  This forces the horizontal loops to be unrolled because
 * there's no indexed addressing into the register file.
 *
 * As in the fetch macro, the operations are ordered from outside to inside, so
 * that jumping into the middle of the block bypasses the unwanted window taps.
 *
 * There are several variants of the macro because of the fixed offets of the
 * taps -- the wider the maximum radius the further the centre tap is from the
 * most recently fetched data.  This means that pre-filling the window requires
 * more data that won't be used and it means that rotating the window involves
 * more mov operations.
 *
 * When the buffer gets too big the buffer at [r9] is used.
 *
 * Input:
 *      q4-q11 -- convoltion window
 *      r9 -- pointer to additional convolution window data
 * Output:
 *      r9 -- updated buffer pointer (if used)
 *      d31 -- result to be stored
 * Modifies:
 *      r12 -- temp buffer pointer
 *      q12-q13 -- temporaries for load and vext operations.
 *      q14-q15 -- intermediate sums
 */
#define TUNED_LIST1 8, 16
.macro hconv1_8/*{{{*/
            vmull.u16   q14, d18, d0[0]
            vmull.u16   q15, d19, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
            .word 107f-100b
            .word 108f-100b
    108:    vmlal.u16   q14, d16, d2[0]
            vmlal.u16   q15, d17, d2[0]
            vmlal.u16   q14, d20, d2[0]
            vmlal.u16   q15, d21, d2[0]
    107:    vext.u16    q12, q8, q9, #1
            vext.u16    q13, q9, q10, #7
            vmlal.u16   q14, d24, d1[3]
            vmlal.u16   q15, d25, d1[3]
            vmlal.u16   q14, d26, d1[3]
            vmlal.u16   q15, d27, d1[3]
    106:    vext.u16    q12, q8, q9, #2
            vext.u16    q13, q9, q10, #6
            vmlal.u16   q14, d24, d1[2]
            vmlal.u16   q15, d25, d1[2]
            vmlal.u16   q14, d26, d1[2]
            vmlal.u16   q15, d27, d1[2]
    105:    vext.u16    q12, q8, q9, #3
            vext.u16    q13, q9, q10, #5
            vmlal.u16   q14, d24, d1[1]
            vmlal.u16   q15, d25, d1[1]
            vmlal.u16   q14, d26, d1[1]
            vmlal.u16   q15, d27, d1[1]
    104:    //vext.u16    q12, q8, q9, #4
            //vext.u16    q13, q9, q10, #4
            vmlal.u16   q14, d17, d1[0]
            vmlal.u16   q15, d18, d1[0]
            vmlal.u16   q14, d19, d1[0]
            vmlal.u16   q15, d20, d1[0]
    103:    vext.u16    q12, q8, q9, #5
            vext.u16    q13, q9, q10, #3
            vmlal.u16   q14, d24, d0[3]
            vmlal.u16   q15, d25, d0[3]
            vmlal.u16   q14, d26, d0[3]
            vmlal.u16   q15, d27, d0[3]
    102:    vext.u16    q12, q8, q9, #6
            vext.u16    q13, q9, q10, #2
            vmlal.u16   q14, d24, d0[2]
            vmlal.u16   q15, d25, d0[2]
            vmlal.u16   q14, d26, d0[2]
            vmlal.u16   q15, d27, d0[2]
    101:    vext.u16    q12, q8, q9, #7
            vext.u16    q13, q9, q10, #1
            vmlal.u16   q14, d24, d0[1]
            vmlal.u16   q15, d25, d0[1]
            vmlal.u16   q14, d26, d0[1]
            vmlal.u16   q15, d27, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

.macro hconv1_16/*{{{*/
            vmull.u16   q14, d16, d0[0]
            vmull.u16   q15, d17, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
            .word 107f-100b
            .word 108f-100b
            .word 109f-100b
            .word 110f-100b
            .word 111f-100b
            .word 112f-100b
            .word 113f-100b
            .word 114f-100b
            .word 115f-100b
            .word 116f-100b
    116:    //vext.u16    q12, q6, q7, #0
            //vext.u16    q13, q10, q11, #0
            vmlal.u16   q14, d12, d4[0]
            vmlal.u16   q15, d13, d4[0]
            vmlal.u16   q14, d20, d4[0]
            vmlal.u16   q15, d21, d4[0]
    115:    vext.u16    q12, q6, q7, #1
            vext.u16    q13, q9, q10, #7
            vmlal.u16   q14, d24, d3[3]
            vmlal.u16   q15, d25, d3[3]
            vmlal.u16   q14, d26, d3[3]
            vmlal.u16   q15, d27, d3[3]
    114:    vext.u16    q12, q6, q7, #2
            vext.u16    q13, q9, q10, #6
            vmlal.u16   q14, d24, d3[2]
            vmlal.u16   q15, d25, d3[2]
            vmlal.u16   q14, d26, d3[2]
            vmlal.u16   q15, d27, d3[2]
    113:    vext.u16    q12, q6, q7, #3
            vext.u16    q13, q9, q10, #5
            vmlal.u16   q14, d24, d3[1]
            vmlal.u16   q15, d25, d3[1]
            vmlal.u16   q14, d26, d3[1]
            vmlal.u16   q15, d27, d3[1]
    112:    //vext.u16    q12, q6, q7, #4
            //vext.u16    q13, q9, q10, #4
            vmlal.u16   q14, d13, d3[0]
            vmlal.u16   q15, d14, d3[0]
            vmlal.u16   q14, d19, d3[0]
            vmlal.u16   q15, d20, d3[0]
    111:    vext.u16    q12, q6, q7, #5
            vext.u16    q13, q9, q10, #3
            vmlal.u16   q14, d24, d2[3]
            vmlal.u16   q15, d25, d2[3]
            vmlal.u16   q14, d26, d2[3]
            vmlal.u16   q15, d27, d2[3]
    110:    vext.u16    q12, q6, q7, #6
            vext.u16    q13, q9, q10, #2
            vmlal.u16   q14, d24, d2[2]
            vmlal.u16   q15, d25, d2[2]
            vmlal.u16   q14, d26, d2[2]
            vmlal.u16   q15, d27, d2[2]
    109:    vext.u16    q12, q6, q7, #7
            vext.u16    q13, q9, q10, #1
            vmlal.u16   q14, d24, d2[1]
            vmlal.u16   q15, d25, d2[1]
            vmlal.u16   q14, d26, d2[1]
            vmlal.u16   q15, d27, d2[1]
    108:    //vext.u16    q12, q7, q8, #0
            //vext.u16    q13, q9, q10, #0
            vmlal.u16   q14, d14, d2[0]
            vmlal.u16   q15, d15, d2[0]
            vmlal.u16   q14, d18, d2[0]
            vmlal.u16   q15, d19, d2[0]
    107:    vext.u16    q12, q7, q8, #1
            vext.u16    q13, q8, q9, #7
            vmlal.u16   q14, d24, d1[3]
            vmlal.u16   q15, d25, d1[3]
            vmlal.u16   q14, d26, d1[3]
            vmlal.u16   q15, d27, d1[3]
    106:    vext.u16    q12, q7, q8, #2
            vext.u16    q13, q8, q9, #6
            vmlal.u16   q14, d24, d1[2]
            vmlal.u16   q15, d25, d1[2]
            vmlal.u16   q14, d26, d1[2]
            vmlal.u16   q15, d27, d1[2]
    105:    vext.u16    q12, q7, q8, #3
            vext.u16    q13, q8, q9, #5
            vmlal.u16   q14, d24, d1[1]
            vmlal.u16   q15, d25, d1[1]
            vmlal.u16   q14, d26, d1[1]
            vmlal.u16   q15, d27, d1[1]
    104:    //vext.u16    q12, q7, q8, #4
            //vext.u16    q13, q8, q9, #4
            vmlal.u16   q14, d15, d1[0]
            vmlal.u16   q15, d16, d1[0]
            vmlal.u16   q14, d17, d1[0]
            vmlal.u16   q15, d18, d1[0]
    103:    vext.u16    q12, q7, q8, #5
            vext.u16    q13, q8, q9, #3
            vmlal.u16   q14, d24, d0[3]
            vmlal.u16   q15, d25, d0[3]
            vmlal.u16   q14, d26, d0[3]
            vmlal.u16   q15, d27, d0[3]
    102:    vext.u16    q12, q7, q8, #6
            vext.u16    q13, q8, q9, #2
            vmlal.u16   q14, d24, d0[2]
            vmlal.u16   q15, d25, d0[2]
            vmlal.u16   q14, d26, d0[2]
            vmlal.u16   q15, d27, d0[2]
    101:    vext.u16    q12, q7, q8, #7
            vext.u16    q13, q8, q9, #1
            vmlal.u16   q14, d24, d0[1]
            vmlal.u16   q15, d25, d0[1]
            vmlal.u16   q14, d26, d0[1]
            vmlal.u16   q15, d27, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vmov        q6, q7
            vmov        q7, q8
            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

.macro hconv1_25/*{{{*/
            vext.u16    q12, q6, q7, #7
            vmull.u16   q14, d24, d0[0]
            vmull.u16   q15, d25, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
            .word 107f-100b
            .word 108f-100b
            .word 109f-100b
            .word 110f-100b
            .word 111f-100b
            .word 112f-100b
            .word 113f-100b
            .word 114f-100b
            .word 115f-100b
            .word 116f-100b
            .word 117f-100b
            .word 118f-100b
            .word 119f-100b
            .word 120f-100b
            .word 121f-100b
            .word 122f-100b
            .word 123f-100b
            .word 124f-100b
            .word 125f-100b
    125:    vext.u16    q12, q3, q4, #6
            vext.u16    q13, q10, q11, #0
            vmlal.u16   q14, d24, d6[1]
            vmlal.u16   q15, d25, d6[1]
            vmlal.u16   q14, d26, d6[1]
            vmlal.u16   q15, d27, d6[1]
    124:    vext.u16    q12, q3, q4, #7
            vext.u16    q13, q9, q10, #7
            vmlal.u16   q14, d24, d6[0]
            vmlal.u16   q15, d25, d6[0]
            vmlal.u16   q14, d26, d6[0]
            vmlal.u16   q15, d27, d6[0]
    123:    vext.u16    q12, q4, q5, #0
            vext.u16    q13, q9, q10, #6
            vmlal.u16   q14, d24, d5[3]
            vmlal.u16   q15, d25, d5[3]
            vmlal.u16   q14, d26, d5[3]
            vmlal.u16   q15, d27, d5[3]
    122:    vext.u16    q12, q4, q5, #1
            vext.u16    q13, q9, q10, #5
            vmlal.u16   q14, d24, d5[2]
            vmlal.u16   q15, d25, d5[2]
            vmlal.u16   q14, d26, d5[2]
            vmlal.u16   q15, d27, d5[2]
    121:    vext.u16    q12, q4, q5, #2
            vext.u16    q13, q9, q10, #4
            vmlal.u16   q14, d24, d5[1]
            vmlal.u16   q15, d25, d5[1]
            vmlal.u16   q14, d26, d5[1]
            vmlal.u16   q15, d27, d5[1]
    120:    vext.u16    q12, q4, q5, #3
            vext.u16    q13, q9, q10, #3
            vmlal.u16   q14, d24, d5[0]
            vmlal.u16   q15, d25, d5[0]
            vmlal.u16   q14, d26, d5[0]
            vmlal.u16   q15, d27, d5[0]
    119:    vext.u16    q12, q4, q5, #4
            vext.u16    q13, q9, q10, #2
            vmlal.u16   q14, d24, d4[3]
            vmlal.u16   q15, d25, d4[3]
            vmlal.u16   q14, d26, d4[3]
            vmlal.u16   q15, d27, d4[3]
    118:    vext.u16    q12, q4, q5, #5
            vext.u16    q13, q9, q10, #1
            vmlal.u16   q14, d24, d4[2]
            vmlal.u16   q15, d25, d4[2]
            vmlal.u16   q14, d26, d4[2]
            vmlal.u16   q15, d27, d4[2]
    117:    vext.u16    q12, q4, q5, #6
            vext.u16    q13, q9, q10, #0
            vmlal.u16   q14, d24, d4[1]
            vmlal.u16   q15, d25, d4[1]
            vmlal.u16   q14, d26, d4[1]
            vmlal.u16   q15, d27, d4[1]
    116:    vext.u16    q12, q4, q5, #7
            vext.u16    q13, q8, q9, #7
            vmlal.u16   q14, d24, d4[0]
            vmlal.u16   q15, d25, d4[0]
            vmlal.u16   q14, d26, d4[0]
            vmlal.u16   q15, d27, d4[0]
    115:    vext.u16    q12, q5, q6, #0
            vext.u16    q13, q8, q9, #6
            vmlal.u16   q14, d24, d3[3]
            vmlal.u16   q15, d25, d3[3]
            vmlal.u16   q14, d26, d3[3]
            vmlal.u16   q15, d27, d3[3]
    114:    vext.u16    q12, q5, q6, #1
            vext.u16    q13, q8, q9, #5
            vmlal.u16   q14, d24, d3[2]
            vmlal.u16   q15, d25, d3[2]
            vmlal.u16   q14, d26, d3[2]
            vmlal.u16   q15, d27, d3[2]
    113:    vext.u16    q12, q5, q6, #2
            vext.u16    q13, q8, q9, #4
            vmlal.u16   q14, d24, d3[1]
            vmlal.u16   q15, d25, d3[1]
            vmlal.u16   q14, d26, d3[1]
            vmlal.u16   q15, d27, d3[1]
    112:    vext.u16    q12, q5, q6, #3
            vext.u16    q13, q8, q9, #3
            vmlal.u16   q14, d24, d3[0]
            vmlal.u16   q15, d25, d3[0]
            vmlal.u16   q14, d26, d3[0]
            vmlal.u16   q15, d27, d3[0]
    111:    vext.u16    q12, q5, q6, #4
            vext.u16    q13, q8, q9, #2
            vmlal.u16   q14, d24, d2[3]
            vmlal.u16   q15, d25, d2[3]
            vmlal.u16   q14, d26, d2[3]
            vmlal.u16   q15, d27, d2[3]
    110:    vext.u16    q12, q5, q6, #5
            vext.u16    q13, q8, q9, #1
            vmlal.u16   q14, d24, d2[2]
            vmlal.u16   q15, d25, d2[2]
            vmlal.u16   q14, d26, d2[2]
            vmlal.u16   q15, d27, d2[2]
    109:    vext.u16    q12, q5, q6, #6
            vext.u16    q13, q8, q9, #0
            vmlal.u16   q14, d24, d2[1]
            vmlal.u16   q15, d25, d2[1]
            vmlal.u16   q14, d26, d2[1]
            vmlal.u16   q15, d27, d2[1]
    108:    vext.u16    q12, q5, q6, #7
            vext.u16    q13, q7, q8, #7
            vmlal.u16   q14, d24, d2[0]
            vmlal.u16   q15, d25, d2[0]
            vmlal.u16   q14, d26, d2[0]
            vmlal.u16   q15, d27, d2[0]
    107:    vext.u16    q12, q6, q7, #0
            vext.u16    q13, q7, q8, #6
            vmlal.u16   q14, d24, d1[3]
            vmlal.u16   q15, d25, d1[3]
            vmlal.u16   q14, d26, d1[3]
            vmlal.u16   q15, d27, d1[3]
    106:    vext.u16    q12, q6, q7, #1
            vext.u16    q13, q7, q8, #5
            vmlal.u16   q14, d24, d1[2]
            vmlal.u16   q15, d25, d1[2]
            vmlal.u16   q14, d26, d1[2]
            vmlal.u16   q15, d27, d1[2]
    105:    vext.u16    q12, q6, q7, #2
            vext.u16    q13, q7, q8, #4
            vmlal.u16   q14, d24, d1[1]
            vmlal.u16   q15, d25, d1[1]
            vmlal.u16   q14, d26, d1[1]
            vmlal.u16   q15, d27, d1[1]
    104:    vext.u16    q12, q6, q7, #3
            vext.u16    q13, q7, q8, #3
            vmlal.u16   q14, d24, d1[0]
            vmlal.u16   q15, d25, d1[0]
            vmlal.u16   q14, d26, d1[0]
            vmlal.u16   q15, d27, d1[0]
    103:    vext.u16    q12, q6, q7, #4
            vext.u16    q13, q7, q8, #2
            vmlal.u16   q14, d24, d0[3]
            vmlal.u16   q15, d25, d0[3]
            vmlal.u16   q14, d26, d0[3]
            vmlal.u16   q15, d27, d0[3]
    102:    vext.u16    q12, q6, q7, #5
            vext.u16    q13, q7, q8, #1
            vmlal.u16   q14, d24, d0[2]
            vmlal.u16   q15, d25, d0[2]
            vmlal.u16   q14, d26, d0[2]
            vmlal.u16   q15, d27, d0[2]
    101:    vext.u16    q12, q6, q7, #6
            vext.u16    q13, q7, q8, #0
            vmlal.u16   q14, d24, d0[1]
            vmlal.u16   q15, d25, d0[1]
            vmlal.u16   q14, d26, d0[1]
            vmlal.u16   q15, d27, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vmov        d7, d9
            vmov        q4, q5
            vmov        q5, q6
            vmov        q6, q7
            vmov        q7, q8
            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

#define TUNED_LIST4 6, 12
.macro hconv4_6/*{{{*/
            vmull.u16   q14, d14, d0[0]
            vmull.u16   q15, d15, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
    106:    vmlal.u16   q14, d8,  d1[2]
            vmlal.u16   q15, d9,  d1[2]
            vmlal.u16   q14, d20, d1[2]
            vmlal.u16   q15, d21, d1[2]
    105:    vmlal.u16   q14, d9,  d1[1]
            vmlal.u16   q15, d10, d1[1]
            vmlal.u16   q14, d19, d1[1]
            vmlal.u16   q15, d20, d1[1]
    104:    vmlal.u16   q14, d10, d1[0]
            vmlal.u16   q15, d11, d1[0]
            vmlal.u16   q14, d18, d1[0]
            vmlal.u16   q15, d19, d1[0]
    103:    vmlal.u16   q14, d11, d0[3]
            vmlal.u16   q15, d12, d0[3]
            vmlal.u16   q14, d17, d0[3]
            vmlal.u16   q15, d18, d0[3]
    102:    vmlal.u16   q14, d12, d0[2]
            vmlal.u16   q15, d13, d0[2]
            vmlal.u16   q14, d16, d0[2]
            vmlal.u16   q15, d17, d0[2]
    101:    vmlal.u16   q14, d13, d0[1]
            vmlal.u16   q15, d14, d0[1]
            vmlal.u16   q14, d15, d0[1]
            vmlal.u16   q15, d16, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vmov        q4, q5
            vmov        q5, q6
            vmov        q6, q7
            vmov        q7, q8
            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

.macro hconv4_12/*{{{*/
            vmull.u16   q14, d8, d0[0]
            vmull.u16   q15, d9, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
            .word 107f-100b
            .word 108f-100b
            .word 109f-100b
            .word 110f-100b
            .word 111f-100b
            .word 112f-100b
    112:    add         r12, r9, #0x1a0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d3[0]
            vmlal.u16   q15, d25, d3[0]
            vmlal.u16   q14, d20, d3[0]
            vmlal.u16   q15, d21, d3[0]
    111:    add         r12, r9, #0x1a8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmlal.u16   q14, d24, d2[3]
            vmlal.u16   q15, d25, d2[3]
            vmlal.u16   q14, d19, d2[3]
            vmlal.u16   q15, d20, d2[3]
    110:    add         r12, r9, #0x1b0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d2[2]
            vmlal.u16   q15, d25, d2[2]
            vmlal.u16   q14, d18, d2[2]
            vmlal.u16   q15, d19, d2[2]
    109:    add         r12, r9, #0x1b8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmlal.u16   q14, d24, d2[1]
            vmlal.u16   q15, d25, d2[1]
            vmlal.u16   q14, d17, d2[1]
            vmlal.u16   q15, d18, d2[1]
    108:    add         r12, r9, #0x1c0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d2[0]
            vmlal.u16   q15, d25, d2[0]
            vmlal.u16   q14, d16, d2[0]
            vmlal.u16   q15, d17, d2[0]
    107:    add         r12, r9, #0x1c8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmlal.u16   q14, d24, d1[3]
            vmlal.u16   q15, d25, d1[3]
            vmlal.u16   q14, d15, d1[3]
            vmlal.u16   q15, d16, d1[3]
    106:    add         r12, r9, #0x1d0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d1[2]
            vmlal.u16   q15, d25, d1[2]
            vmlal.u16   q14, d14, d1[2]
            vmlal.u16   q15, d15, d1[2]
    105:    add         r12, r9, #0x1d8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmlal.u16   q14, d24, d1[1]
            vmlal.u16   q15, d25, d1[1]
            vmlal.u16   q14, d13, d1[1]
            vmlal.u16   q15, d14, d1[1]
    104:    add         r12, r9, #0x1e0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d1[0]
            vmlal.u16   q15, d25, d1[0]
            vmlal.u16   q14, d12, d1[0]
            vmlal.u16   q15, d13, d1[0]
    103:    add         r12, r9, #0x1e8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmlal.u16   q14, d24, d0[3]
            vmlal.u16   q15, d25, d0[3]
            vmlal.u16   q14, d11, d0[3]
            vmlal.u16   q15, d12, d0[3]
    102:    add         r12, r9, #0x1f0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d0[2]
            vmlal.u16   q15, d25, d0[2]
            vmlal.u16   q14, d10, d0[2]
            vmlal.u16   q15, d11, d0[2]
    101:    add         r12, r9, #0x1f8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]
            vmlal.u16   q14, d24, d0[1]
            vmlal.u16   q15, d8,  d0[1]
            vmlal.u16   q14, d9,  d0[1]
            vmlal.u16   q15, d10, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vst1.u8     {q4}, [r9:128]!
            bic         r9, r9, #0x200
            vmov        q4, q5
            vmov        q5, q6
            vmov        q6, q7
            vmov        q7, q8
            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

.macro hconv4_25/*{{{*/
            add         r12, r9, #0x198
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12:64]
            vmull.u16   q14, d24, d0[0]
            vmull.u16   q15, d25, d0[0]

            ldr         r12, [pc, r5, LSL #2]
            add         pc, pc, r12
            bkpt
    100:    .word 101f-100b
            .word 102f-100b
            .word 103f-100b
            .word 104f-100b
            .word 105f-100b
            .word 106f-100b
            .word 107f-100b
            .word 108f-100b
            .word 109f-100b
            .word 110f-100b
            .word 111f-100b
            .word 112f-100b
            .word 113f-100b
            .word 114f-100b
            .word 115f-100b
            .word 116f-100b
            .word 117f-100b
            .word 118f-100b
            .word 119f-100b
            .word 120f-100b
            .word 121f-100b
            .word 122f-100b
            .word 123f-100b
            .word 124f-100b
            .word 125f-100b
    125:    add         r12, r9, #0x0d0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d6[1]
            vmlal.u16   q15, d25, d6[1]
            vmlal.u16   q14, d20, d6[1]
            vmlal.u16   q15, d21, d6[1]
    124:    add         r12, r9, #0x0d8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d6[0]
            vmlal.u16   q15, d25, d6[0]
            vmlal.u16   q14, d19, d6[0]
            vmlal.u16   q15, d20, d6[0]
    123:    add         r12, r9, #0x0e0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d5[3]
            vmlal.u16   q15, d25, d5[3]
            vmlal.u16   q14, d18, d5[3]
            vmlal.u16   q15, d19, d5[3]
    122:    add         r12, r9, #0x0e8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d5[2]
            vmlal.u16   q15, d25, d5[2]
            vmlal.u16   q14, d17, d5[2]
            vmlal.u16   q15, d18, d5[2]
    121:    add         r12, r9, #0x0f0
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d5[1]
            vmlal.u16   q15, d25, d5[1]
            vmlal.u16   q14, d16, d5[1]
            vmlal.u16   q15, d17, d5[1]
    120:    add         r12, r9, #0x0f8
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d5[0]
            vmlal.u16   q15, d25, d5[0]
            vmlal.u16   q14, d15, d5[0]
            vmlal.u16   q15, d16, d5[0]
    119:    add         r12, r9, #0x100
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d4[3]
            vmlal.u16   q15, d25, d4[3]
            vmlal.u16   q14, d14, d4[3]
            vmlal.u16   q15, d15, d4[3]
    118:    add         r12, r9, #0x108
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d4[2]
            vmlal.u16   q15, d25, d4[2]
            vmlal.u16   q14, d13, d4[2]
            vmlal.u16   q15, d14, d4[2]
    117:    add         r12, r9, #0x110
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d4[1]
            vmlal.u16   q15, d25, d4[1]
            vmlal.u16   q14, d12, d4[1]
            vmlal.u16   q15, d13, d4[1]
    116:    add         r12, r9, #0x118
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d4[0]
            vmlal.u16   q15, d25, d4[0]
            vmlal.u16   q14, d11, d4[0]
            vmlal.u16   q15, d12, d4[0]
    115:    add         r12, r9, #0x120
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d3[3]
            vmlal.u16   q15, d25, d3[3]
            vmlal.u16   q14, d10, d3[3]
            vmlal.u16   q15, d11, d3[3]
    114:    add         r12, r9, #0x128
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
            vmlal.u16   q14, d24, d3[2]
            vmlal.u16   q15, d25, d3[2]
            vmlal.u16   q14, d9,  d3[2]
            vmlal.u16   q15, d10, d3[2]
    113:    add         r12, r9, #0x130
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
            vmlal.u16   q14, d24, d3[1]
            vmlal.u16   q15, d25, d3[1]
            vmlal.u16   q14, d8,  d3[1]
            vmlal.u16   q15, d9,  d3[1]
    112:    add         r12, r9, #0x138
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1f8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]
            vmlal.u16   q14, d24, d3[0]
            vmlal.u16   q15, d25, d3[0]
            vmlal.u16   q14, d26, d3[0]   @ Could be d7, without the load, right?
            vmlal.u16   q15, d8,  d3[0]
    111:    add         r12, r9, #0x140
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
                                            add         r12, r9, #0x1f0
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d2[3]
            vmlal.u16   q15, d25, d2[3]
            vmlal.u16   q14, d26, d2[3]
            vmlal.u16   q15, d27, d2[3]
    110:    add         r12, r9, #0x148
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1e8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]!
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d27}, [r12:64]
            vmlal.u16   q14, d24, d2[2]
            vmlal.u16   q15, d25, d2[2]
            vmlal.u16   q14, d26, d2[2]
            vmlal.u16   q15, d27, d2[2]
    109:    add         r12, r9, #0x150
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
                                            add         r12, r9, #0x1e0
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d2[1]
            vmlal.u16   q15, d25, d2[1]
            vmlal.u16   q14, d26, d2[1]
            vmlal.u16   q15, d27, d2[1]
    108:    add         r12, r9, #0x158
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1d8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]!
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d27}, [r12:64]
            vmlal.u16   q14, d24, d2[0]
            vmlal.u16   q15, d25, d2[0]
            vmlal.u16   q14, d26, d2[0]
            vmlal.u16   q15, d27, d2[0]
    107:    add         r12, r9, #0x160
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
                                            add         r12, r9, #0x1d0
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d1[3]
            vmlal.u16   q15, d25, d1[3]
            vmlal.u16   q14, d26, d1[3]
            vmlal.u16   q15, d27, d1[3]
    106:    add         r12, r9, #0x168
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1c8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]!
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d27}, [r12:64]
            vmlal.u16   q14, d24, d1[2]
            vmlal.u16   q15, d25, d1[2]
            vmlal.u16   q14, d26, d1[2]
            vmlal.u16   q15, d27, d1[2]
    105:    add         r12, r9, #0x170
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
                                            add         r12, r9, #0x1c0
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d1[1]
            vmlal.u16   q15, d25, d1[1]
            vmlal.u16   q14, d26, d1[1]
            vmlal.u16   q15, d27, d1[1]
    104:    add         r12, r9, #0x178
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1b8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]!
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d27}, [r12:64]
            vmlal.u16   q14, d24, d1[0]
            vmlal.u16   q15, d25, d1[0]
            vmlal.u16   q14, d26, d1[0]
            vmlal.u16   q15, d27, d1[0]
    103:    add         r12, r9, #0x180
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]
                                            add         r12, r9, #0x1b0
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d0[3]
            vmlal.u16   q15, d25, d0[3]
            vmlal.u16   q14, d26, d0[3]
            vmlal.u16   q15, d27, d0[3]
    102:    add         r12, r9, #0x188
            bic         r12, r12, #0x200
            vld1.u16    {d24}, [r12:64]!
            bic         r12, r12, #0x200
            vld1.u16    {d25}, [r12]
                                            add         r12, r9, #0x1a8
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d26}, [r12:64]!
                                            bic         r12, r12, #0x200
                                            vld1.u16    {d27}, [r12:64]
            vmlal.u16   q14, d24, d0[2]
            vmlal.u16   q15, d25, d0[2]
            vmlal.u16   q14, d26, d0[2]
            vmlal.u16   q15, d27, d0[2]
    101:    add         r12, r9, #0x190
            bic         r12, r12, #0x200
            vld1.u16    {d24,d25}, [r12:128]!
            bic         r12, r12, #0x200
            vld1.u16    {d26,d27}, [r12:128]
            vmlal.u16   q14, d24, d0[1]
            vmlal.u16   q15, d25, d0[1]
            vmlal.u16   q14, d26, d0[1]
            vmlal.u16   q15, d27, d0[1]

            vqrshrn.u32 d28, q14, #16
            vqrshrn.u32 d29, q15, #16
            vqrshrn.u16 d31, q14, #FRACTION_BITS

            vst1.u8     {q4}, [r9:128]!
            bic         r9, r9, #0x200
            vmov        q4, q5
            vmov        q5, q6
            vmov        q6, q7
            vmov        q7, q8
            vmov        q8, q9
            vmov        q9, q10
            vmov        q10, q11
.endm/*}}}*/

/* Dedicated function wrapper for the fetch macro, for the cases where
 * performance isn't that important, to keep code size down.
 */
PRIVATE(fetch_generic_asm)
            push        {r10,r11}
            fetch
            pop         {r10,r11}
            bx          lr
END(fetch_generic_asm)

/* Given values in q10 and q11, and an index in r11, sweep the (r11&15)th value
 * across to fill the rest of the register pair.  Used for filling the right
 * hand edge of the window when starting too close to the right hand edge of
 * the image.
 * Also returns a dup-ed copy of the last element in q12 for the tail-fill
 * case (this happens incidentally in common path, but must be done
 * deliberately in the fast-out path).
 */
PRIVATE(prefetch_clampright1)
            ands        r12, r11, #15
            beq         1f
            sub         r12, r12, #1
            sub         sp, sp, #64
            vst1.u16    {q10,q11}, [sp]
            add         r12, sp, r12, LSL #1
            vld1.u16    {d24[]}, [r12]
            vld1.u16    {d25[]}, [r12]
            vst1.u16    {q12}, [r12]!
            vst1.u16    {q12}, [r12]
            vld1.u16    {q10,q11}, [sp]
            add         sp, sp, #64
            bx          lr
1:          vdup.u16    q12, d23[3]
            bx          lr
END(prefetch_clampright1)

PRIVATE(prefetch_clampright4)
            ands        r12, r11, #15
            beq         1f
            sub         r12, r12, #4
            sub         sp, sp, #64
            vst1.u16    {q10,q11}, [sp]
            add         r12, sp, r12, LSL #1
            vld1.u64    {d24}, [r12]
            vld1.u64    {d25}, [r12]
            vst1.u16    {q12}, [r12]!
            vst1.u16    {q12}, [r12]
            vld1.u16    {q10,q11}, [sp]
            add         sp, sp, #64
            bx          lr
1:          vmov.u16    d24, d23
            vmov.u16    d25, d23
            bx          lr
END(prefetch_clampright4)


/* Helpers for prefetch, below.
 */
.macro prefetch_out qa, qb, store, qsa, qsb, qsb_hi
  .if \store > 0
    .ifc \qsa,\qsb
            vst1.u16    {\qsa}, [r9:128]!
            vst1.u16    {\qsb}, [r9:128]!
    .else
            vst1.u16    {\qsa,\qsb}, [r9:256]!
    .endif
  .elseif \store == 0
            vmov.u16    \qa, \qsa
            vmov.u16    \qb, \qsb
  .else
            vmov.u16    \qb, \qsb_hi
  .endif
.endm

.macro prefetch_one  qa, qb, rem, c, store=0, step=1
.set i, (need - 16) - \rem
.if i >= 0
1:          cmp         r10, #i+16
            blo         2f
            prefetch_out \qa, \qb, \store, q9, q9, d19
            b           1f
2:          cmp         r11, #i+16
            bls         3f
            prefetch_out \qa, \qb, \store, q10, q11, d23
            bl          fetch_generic_asm
            b           2f
3:          bl          prefetch_clampright\step
            prefetch_out \qa, \qb, \store, q10, q11, d23
4:          b           4f+4
            @q12 contains pad word from prefetch_clampright call
            prefetch_out \qa, \qb, \store, q12, q12, d25
  .if \rem > 0
            b           4f+4
  .else
1:
2:
3:
4:          nop
  .endif
.endif
.endm

/* Fill the convolution window with context data.  The aim here is to load
 * exactly rlf + rrt columns, and in the main loop to read as many columns as
 * will be written.  This is complicated by the need to handle cases when the
 * input starts very close to the left or right (or both) edges of the image,
 * and where these do not fall on 16-byte boundaries.
 *
 * Input:
 *      r1 -- src
 *      r2 -- pitch
 *      r3 -- count
 *      r4 -- inlen
 *      r5 -- r
 *      r6 -- rup
 *      r7 -- rdn
 *      r8 -- rlf
 *      r9 -- buffer (if needed)
 * Output:
 *      r1 += rlf + min(count, rrt)
 * Modifies:
 *      r10 -- fill start index in the window
 *      r11 -- fill stop index in the window
 *      r12 -- scratch
 */
.macro prefetch step=1, max_r=25
.set need, ((\max_r + \max_r) * \step + 15) & ~15
  .if \step == 1
            rsb         r10, r8, #need - (\max_r * \step)
  .else
            mov         r10, r8, LSL #2
            rsb         r10, r10, #need - (\max_r * \step)
  .endif
            add         r11, r10, r4
            cmp         r11, #need
            movhi       r11, #need

            bl          fetch_generic_asm
  .if \step == 1
            vdup.u16    q9, d20[0]
  .else
            vmov.u16    d18, d20
            vmov.u16    d19, d20
  .endif
            ands        r12, r10, #15
            beq         2f
            sub         sp, sp, #32
            vst1.u16    {q10,q11}, [sp]
            sub         r12, sp, r12, LSL #1
            sub         sp, sp, #16
            vst1.u16    {q9}, [sp]
            sub         sp, sp, #16
            vst1.u16    {q9}, [sp]
            vld1.u16    {q10,q11}, [r12]
            add         sp, sp, #64
            sub         r1, r1, r10
            bic         r10, r10, #15
            add         r1, r1, r10
2:
  .if \step > 1
            /* it's only in the uchar2 and uchar4 cases where the register file
             * is insufficient (given MAX_R <= 25).
             */
            prefetch_one xx, xx, 192, c=\max_r, step=\step, store=1
            prefetch_one xx, xx, 176, c=\max_r, step=\step, store=1
            prefetch_one xx, xx, 160, c=\max_r, step=\step, store=1
            prefetch_one xx, xx, 144, c=\max_r, step=\step, store=1
            prefetch_one xx, xx, 128, c=\max_r, step=\step, store=1
            prefetch_one xx, xx, 112, c=\max_r, step=\step, store=1
            prefetch_one xx, xx,  96, c=\max_r, step=\step, store=1
            prefetch_one xx, xx,  80, c=\max_r, step=\step, store=1
            prefetch_one xx, xx,  64, c=\max_r, step=\step, store=1
            prefetch_one xx, xx,  48, c=\max_r, step=\step, store=1
  .else
            /* q3 normally contains the coefficient table, but it's not fully
             * used.  In the uchar1, r=25 case the other half of q3 is used for
             * the last two window taps to avoid falling out to memory.
             */
            prefetch_one xx, d7,  48, c=\max_r, step=\step, store=-1
  .endif
            prefetch_one q4, q5,  32, c=\max_r, step=\step, store=0
            prefetch_one q6, q7,  16, c=\max_r, step=\step, store=0
            prefetch_one q8, q9,   0, c=\max_r, step=\step, store=0

  .if \step == 1
            add         r10, r8, #\max_r * \step
  .else
            mov         r10, r8, LSL #2
            add         r10, r10, #\max_r * \step
  .endif
            subs        r4, r4, r10
            movlo       r4, #0
.endm

/* The main loop.
 *
 * Input:
 *      r0 = dst
 *      r1 = src
 *      r2 = pitch
 *      r3 = count
 *      r4 = inlen
 *      r5 = r
 *      r6 = rup
 *      r7 = rdn
 *      r9 = buffer
 * Modifies
 *      r8 = fetch code pointer
 */
.macro mainloop core, step=1, max_r=25, labelc="", labelnc=""
            ldr         r8, 3f
1:          add         r8, r8, pc
            sub         r8, r5, LSL #5
            sub         r8, r5, LSL #4
            cmp         r5, r6
            cmpeq       r5, r7
            beq         5f

            /* if (r != rup || r != rdn) then the address-clamping table should
             * be used rather than the short-cut version.
             */
            ldr         r8, 3f+4
2:          add         r8, r8, pc
            sub         r8, r5, LSL #6
            b           5f
            .align 3
3:          .word       \labelnc-1b-8
            .word       \labelc-2b-8
            .align 4
3:          fetch max_r=\max_r, labelc=\labelc, labelnc=\labelnc, reg=r8

            /* For each call to fetch two are made to \core.  It would be
             * preferable to have twice the work done in \core, but the
             * register file is too small for this to be straightforward.
             */
            \core
            vst1.u8     {d31}, [r0]!
            \core
            vst1.u8     {d31}, [r0]!

            sub         r3, r3, #16
5:          subs        r4, r4, #16
            bhs         3b
            adds        r4, r4, #16
            bne         1f
  .if \step==1
            vdup.u16    q10, d19[3]
            vdup.u16    q11, d19[3]
  .else
            vmov.u64    d20, d19
            vmov.u64    d21, d19
            vmov.u64    d22, d19
            vmov.u64    d23, d19
  .endif
            b           4f

1:          sub         r1, r1, #16
            add         r1, r1, r4
            bl          fetch_generic_asm

  .if \step==1
            vdup.u16    q12, d23[3]
  .else
            vmov.u64    d24, d23
            vmov.u64    d25, d23
  .endif
            rsb         r4, r4, #0
            tst         r4, #8
            beq         1f
            vmov        q10, q11
            vmov        q11, q12
1:          tst         r4, #4
            beq         1f
            vext.u16    q10, q10, q11, #4
            vext.u16    q11, q11, q12, #4
1:          tst         r4, #2
            beq         1f
            vext.u16    q10, q10, q11, #2
            vext.u16    q11, q11, q12, #2
1:          tst         r4, #1
            beq         4f
            vext.u16    q10, q10, q11, #1
            vext.u16    q11, q11, q12, #1
4:          cmp         r3, #0
            beq         5f
3:          \core
  .if \step==1
            vdup.u16    q11, d23[3]
  .else
            vmov.u64    d22, d23
  .endif
            subs        r3, r3, #8
            blo         4f
            vst1.u8     {d31}, [r0]!
            beq         5f
            b           3b
4:          tst         r3, #4
            beq         1f
            vst1.u32    {d31[0]}, [r0]!
            vext.u8     d31, d31, d31, #4
1:          tst         r3, #2
            beq         1f
            vst1.u16    {d31[0]}, [r0]!
            vext.u8     d31, d31, d31, #2
1:          tst         r3, #1
            beq         5f
            vst1.u8     {d31[0]}, [r0]!
            vext.u8     d31, d31, d31, #1
5:          nop
.endm

.irep r, TUNED_LIST1, 25
PRIVATE(convolve1_\r)
            push        {r12,lr}

            sub         r1, r1, r8

            prefetch    step=1, max_r=\r

            mainloop    core=hconv1_\r, step=1, max_r=\r, labelc=.Lcnv1_\r, labelnc=.Lcnvnc1_\r

            pop         {r12,pc}
END(convolve1_\r)
.endr

.irep r, TUNED_LIST4, 25
PRIVATE(convolve4_\r)
            sub         r12, sp, #0x200
            bic         r9, r12, #0x3fc
            mov         sp, r9
            push        {r12,lr}

            /* r9 now points to a buffer on the stack whose address has the low
             * 10 bits clear.  This allows easy address calculation in the
             * wrap-around cases.
             */

            sub         r1, r1, r8, LSL #2

            prefetch    step=4, max_r=\r

            mainloop    core=hconv4_\r, step=4, max_r=\r, labelc=.Lcnv4_\r, labelnc=.Lcnvnc4_\r

            pop         {r12,lr}
            add         sp, r12, #0x200
            bx          lr
END(convolve4_\r)
.endr

/* void rsdIntrinsicBlurU1_K(
 *                  void *out,      // r0
 *                  void *in,       // r1
 *                  size_t w,       // r2
 *                  size_t h,       // r3
 *                  size_t p,       // [sp]
 *                  size_t x,       // [sp,#4]
 *                  size_t y,       // [sp,#8]
 *                  size_t count,   // [sp,#12]
 *                  size_t r,       // [sp,#16]
 *                  uint16_t *tab); // [sp,#20]
 */
ENTRY(rsdIntrinsicBlurU1_K)
            push        {r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
            vpush       {d8-d15}
            ldr         r5, [sp,#120]
            ldr         r8, [sp,#108]
            ldr         r6, [sp,#112]
            sub         r9, r2, r8
            sub         r7, r3, r6
            ldr         r2, [sp,#104]
            ldr         r3, [sp,#116]
            sub         r9, r9, r3
            sub         r7, r7, #1

            ldr         r12, [sp,#124]

            add         r1, r1, r8

            cmp         r6, r5
            movhi       r6, r5
            cmp         r7, r5
            movhi       r7, r5
            cmp         r8, r5
            movhi       r8, r5
            cmp         r9, r5
            movhi       r9, r5

            add         r4, r8, r9
            add         r4, r4, r3

            vld1.u16    {d0,d1,d2,d3}, [r12]!
            vld1.u16    {d4,d5,d6}, [r12]!

            adr         lr, 1f
  .irep r, TUNED_LIST1
            cmp         r5, #\r
            bls         convolve1_\r
  .endr
            b           convolve1_25

1:          vpop        {d8-d15}
            pop         {r4,r5,r6,r7,r8,r9,r10,r11,r12,pc}
END(rsdIntrinsicBlurU1_K)

/* void rsdIntrinsicBlurU4_K(
 *                  void *out,      // r0
 *                  void *in,       // r1
 *                  size_t w,       // r2
 *                  size_t h,       // r3
 *                  size_t p,       // [sp]
 *                  size_t x,       // [sp,#4]
 *                  size_t y,       // [sp,#8]
 *                  size_t count,   // [sp,#12]
 *                  size_t r,       // [sp,#16]
 *                  uint16_t *tab); // [sp,#20]
 */
ENTRY(rsdIntrinsicBlurU4_K)
            push        {r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
            vpush       {d8-d15}
            ldr         r5, [sp,#120]
            ldr         r8, [sp,#108]
            ldr         r6, [sp,#112]
            sub         r9, r2, r8
            sub         r7, r3, r6
            ldr         r2, [sp,#104]
            ldr         r3, [sp,#116]
            sub         r9, r9, r3
            sub         r7, r7, #1

            ldr         r12, [sp,#124]

            add         r1, r1, r8, LSL #2

            cmp         r6, r5
            movhi       r6, r5
            cmp         r7, r5
            movhi       r7, r5
            cmp         r8, r5
            movhi       r8, r5
            cmp         r9, r5
            movhi       r9, r5

            mov         r3, r3, LSL #2
            add         r4, r8, r9
            add         r4, r3, r4, LSL #2

            vld1.u16    {d0,d1,d2,d3}, [r12]!
            vld1.u16    {d4,d5,d6}, [r12]!

            adr         lr, 1f
  .irep r, TUNED_LIST4
            cmp         r5, #\r
            bls         convolve4_\r
  .endr
            b           convolve4_25

1:          vpop        {d8-d15}
            pop         {r4,r5,r6,r7,r8,r9,r10,r11,r12,pc}
END(rsdIntrinsicBlurU4_K)
