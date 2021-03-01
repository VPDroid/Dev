/*
 * Copyright (C) 2012 The Android Open Source Project
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


#include "rsCpuIntrinsic.h"
#include "rsCpuIntrinsicInlines.h"

using namespace android;
using namespace android::renderscript;

namespace android {
namespace renderscript {


class RsdCpuScriptIntrinsicBlend : public RsdCpuScriptIntrinsic {
public:
    void populateScript(Script *) override;

    ~RsdCpuScriptIntrinsicBlend() override;
    RsdCpuScriptIntrinsicBlend(RsdCpuReferenceImpl *ctx, const Script *s, const Element *e);

protected:
    static void kernel(const RsExpandKernelDriverInfo *info, uint32_t xstart,
                       uint32_t xend, uint32_t outstep);
};

}
}


enum {
    BLEND_CLEAR = 0,
    BLEND_SRC = 1,
    BLEND_DST = 2,
    BLEND_SRC_OVER = 3,
    BLEND_DST_OVER = 4,
    BLEND_SRC_IN = 5,
    BLEND_DST_IN = 6,
    BLEND_SRC_OUT = 7,
    BLEND_DST_OUT = 8,
    BLEND_SRC_ATOP = 9,
    BLEND_DST_ATOP = 10,
    BLEND_XOR = 11,

    BLEND_NORMAL = 12,
    BLEND_AVERAGE = 13,
    BLEND_MULTIPLY = 14,
    BLEND_SCREEN = 15,
    BLEND_DARKEN = 16,
    BLEND_LIGHTEN = 17,
    BLEND_OVERLAY = 18,
    BLEND_HARDLIGHT = 19,
    BLEND_SOFTLIGHT = 20,
    BLEND_DIFFERENCE = 21,
    BLEND_NEGATION = 22,
    BLEND_EXCLUSION = 23,
    BLEND_COLOR_DODGE = 24,
    BLEND_INVERSE_COLOR_DODGE = 25,
    BLEND_SOFT_DODGE = 26,
    BLEND_COLOR_BURN = 27,
    BLEND_INVERSE_COLOR_BURN = 28,
    BLEND_SOFT_BURN = 29,
    BLEND_REFLECT = 30,
    BLEND_GLOW = 31,
    BLEND_FREEZE = 32,
    BLEND_HEAT = 33,
    BLEND_ADD = 34,
    BLEND_SUBTRACT = 35,
    BLEND_STAMP = 36,
    BLEND_RED = 37,
    BLEND_GREEN = 38,
    BLEND_BLUE = 39,
    BLEND_HUE = 40,
    BLEND_SATURATION = 41,
    BLEND_COLOR = 42,
    BLEND_LUMINOSITY = 43
};

#if defined(ARCH_ARM_USE_INTRINSICS)
extern "C" int rsdIntrinsicBlend_K(uchar4 *out, uchar4 const *in, int slot,
                    uint32_t xstart, uint32_t xend);
#endif

#if defined(ARCH_X86_HAVE_SSSE3)
extern void rsdIntrinsicBlendSrcOver_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendDstOver_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendSrcIn_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendDstIn_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendSrcOut_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendDstOut_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendSrcAtop_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendDstAtop_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendXor_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendMultiply_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendAdd_K(void *dst, const void *src, uint32_t count8);
extern void rsdIntrinsicBlendSub_K(void *dst, const void *src, uint32_t count8);
#endif

void RsdCpuScriptIntrinsicBlend::kernel(const RsExpandKernelDriverInfo *info,
                                        uint32_t xstart, uint32_t xend,
                                        uint32_t outstep) {
    RsdCpuScriptIntrinsicBlend *cp = (RsdCpuScriptIntrinsicBlend *)info->usr;

    // instep/outstep can be ignored--sizeof(uchar4) known at compile time
    uchar4 *out = (uchar4 *)info->outPtr[0];
    uchar4 *in = (uchar4 *)info->inPtr[0];
    uint32_t x1 = xstart;
    uint32_t x2 = xend;

#if defined(ARCH_ARM_USE_INTRINSICS) && !defined(ARCH_ARM64_USE_INTRINSICS)
    // Bug: 22047392 - Skip optimized version for BLEND_DST_ATOP until this
    // been fixed.
    if (gArchUseSIMD && info->slot != BLEND_DST_ATOP) {
        if (rsdIntrinsicBlend_K(out, in, info->slot, x1, x2) >= 0)
            return;
    }
#endif
    switch (info->slot) {
    case BLEND_CLEAR:
        for (;x1 < x2; x1++, out++) {
            *out = 0;
        }
        break;
    case BLEND_SRC:
        for (;x1 < x2; x1++, out++, in++) {
          *out = *in;
        }
        break;
    //BLEND_DST is a NOP
    case BLEND_DST:
        break;
    case BLEND_SRC_OVER:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendSrcOver_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            short4 out_s = convert_short4(*out);
            in_s = in_s + ((out_s * (short4)(255 - in_s.w)) >> (short4)8);
            *out = convert_uchar4(in_s);
        }
        break;
    case BLEND_DST_OVER:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendDstOver_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
     #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            short4 out_s = convert_short4(*out);
            in_s = out_s + ((in_s * (short4)(255 - out_s.w)) >> (short4)8);
            *out = convert_uchar4(in_s);
        }
        break;
    case BLEND_SRC_IN:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendSrcIn_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            in_s = (in_s * out->w) >> (short4)8;
            *out = convert_uchar4(in_s);
        }
        break;
    case BLEND_DST_IN:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendDstIn_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
     #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 out_s = convert_short4(*out);
            out_s = (out_s * in->w) >> (short4)8;
            *out = convert_uchar4(out_s);
        }
        break;
    case BLEND_SRC_OUT:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendSrcOut_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            in_s = (in_s * (short4)(255 - out->w)) >> (short4)8;
            *out = convert_uchar4(in_s);
        }
        break;
    case BLEND_DST_OUT:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendDstOut_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 out_s = convert_short4(*out);
            out_s = (out_s * (short4)(255 - in->w)) >> (short4)8;
            *out = convert_uchar4(out_s);
        }
        break;
    case BLEND_SRC_ATOP:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendSrcAtop_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            short4 out_s = convert_short4(*out);
            out_s.xyz = ((in_s.xyz * out_s.w) +
              (out_s.xyz * ((short3)255 - (short3)in_s.w))) >> (short3)8;
            *out = convert_uchar4(out_s);
        }
        break;
    case BLEND_DST_ATOP:
    // Bug: 22047392 - We need to make sure that "out->w = in->w;" in all
    // accelerated versions before re-enabling optimizations.
    #if false  // Bug: 22047392
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendDstAtop_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
     #endif
     #endif  // false for Bug: 22047392
        for (;x1 < x2; x1++, out++, in++) {
            short4 in_s = convert_short4(*in);
            short4 out_s = convert_short4(*out);
            out_s.xyz = ((out_s.xyz * in_s.w) +
              (in_s.xyz * ((short3)255 - (short3)out_s.w))) >> (short3)8;
            out_s.w = in_s.w;
            *out = convert_uchar4(out_s);
        }
        break;
    case BLEND_XOR:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendXor_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            *out = *in ^ *out;
        }
        break;
    case BLEND_NORMAL:
        ALOGE("Called unimplemented blend intrinsic BLEND_NORMAL");
        rsAssert(false);
        break;
    case BLEND_AVERAGE:
        ALOGE("Called unimplemented blend intrinsic BLEND_AVERAGE");
        rsAssert(false);
        break;
    case BLEND_MULTIPLY:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if ((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendMultiply_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
          *out = convert_uchar4((convert_short4(*in) * convert_short4(*out))
                                >> (short4)8);
        }
        break;
    case BLEND_SCREEN:
        ALOGE("Called unimplemented blend intrinsic BLEND_SCREEN");
        rsAssert(false);
        break;
    case BLEND_DARKEN:
        ALOGE("Called unimplemented blend intrinsic BLEND_DARKEN");
        rsAssert(false);
        break;
    case BLEND_LIGHTEN:
        ALOGE("Called unimplemented blend intrinsic BLEND_LIGHTEN");
        rsAssert(false);
        break;
    case BLEND_OVERLAY:
        ALOGE("Called unimplemented blend intrinsic BLEND_OVERLAY");
        rsAssert(false);
        break;
    case BLEND_HARDLIGHT:
        ALOGE("Called unimplemented blend intrinsic BLEND_HARDLIGHT");
        rsAssert(false);
        break;
    case BLEND_SOFTLIGHT:
        ALOGE("Called unimplemented blend intrinsic BLEND_SOFTLIGHT");
        rsAssert(false);
        break;
    case BLEND_DIFFERENCE:
        ALOGE("Called unimplemented blend intrinsic BLEND_DIFFERENCE");
        rsAssert(false);
        break;
    case BLEND_NEGATION:
        ALOGE("Called unimplemented blend intrinsic BLEND_NEGATION");
        rsAssert(false);
        break;
    case BLEND_EXCLUSION:
        ALOGE("Called unimplemented blend intrinsic BLEND_EXCLUSION");
        rsAssert(false);
        break;
    case BLEND_COLOR_DODGE:
        ALOGE("Called unimplemented blend intrinsic BLEND_COLOR_DODGE");
        rsAssert(false);
        break;
    case BLEND_INVERSE_COLOR_DODGE:
        ALOGE("Called unimplemented blend intrinsic BLEND_INVERSE_COLOR_DODGE");
        rsAssert(false);
        break;
    case BLEND_SOFT_DODGE:
        ALOGE("Called unimplemented blend intrinsic BLEND_SOFT_DODGE");
        rsAssert(false);
        break;
    case BLEND_COLOR_BURN:
        ALOGE("Called unimplemented blend intrinsic BLEND_COLOR_BURN");
        rsAssert(false);
        break;
    case BLEND_INVERSE_COLOR_BURN:
        ALOGE("Called unimplemented blend intrinsic BLEND_INVERSE_COLOR_BURN");
        rsAssert(false);
        break;
    case BLEND_SOFT_BURN:
        ALOGE("Called unimplemented blend intrinsic BLEND_SOFT_BURN");
        rsAssert(false);
        break;
    case BLEND_REFLECT:
        ALOGE("Called unimplemented blend intrinsic BLEND_REFLECT");
        rsAssert(false);
        break;
    case BLEND_GLOW:
        ALOGE("Called unimplemented blend intrinsic BLEND_GLOW");
        rsAssert(false);
        break;
    case BLEND_FREEZE:
        ALOGE("Called unimplemented blend intrinsic BLEND_FREEZE");
        rsAssert(false);
        break;
    case BLEND_HEAT:
        ALOGE("Called unimplemented blend intrinsic BLEND_HEAT");
        rsAssert(false);
        break;
    case BLEND_ADD:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendAdd_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            uint32_t iR = in->x, iG = in->y, iB = in->z, iA = in->w,
                oR = out->x, oG = out->y, oB = out->z, oA = out->w;
            out->x = (oR + iR) > 255 ? 255 : oR + iR;
            out->y = (oG + iG) > 255 ? 255 : oG + iG;
            out->z = (oB + iB) > 255 ? 255 : oB + iB;
            out->w = (oA + iA) > 255 ? 255 : oA + iA;
        }
        break;
    case BLEND_SUBTRACT:
    #if defined(ARCH_X86_HAVE_SSSE3)
        if (gArchUseSIMD) {
            if((x1 + 8) < x2) {
                uint32_t len = (x2 - x1) >> 3;
                rsdIntrinsicBlendSub_K(out, in, len);
                x1 += len << 3;
                out += len << 3;
                in += len << 3;
            }
        }
    #endif
        for (;x1 < x2; x1++, out++, in++) {
            int32_t iR = in->x, iG = in->y, iB = in->z, iA = in->w,
                oR = out->x, oG = out->y, oB = out->z, oA = out->w;
            out->x = (oR - iR) < 0 ? 0 : oR - iR;
            out->y = (oG - iG) < 0 ? 0 : oG - iG;
            out->z = (oB - iB) < 0 ? 0 : oB - iB;
            out->w = (oA - iA) < 0 ? 0 : oA - iA;
        }
        break;
    case BLEND_STAMP:
        ALOGE("Called unimplemented blend intrinsic BLEND_STAMP");
        rsAssert(false);
        break;
    case BLEND_RED:
        ALOGE("Called unimplemented blend intrinsic BLEND_RED");
        rsAssert(false);
        break;
    case BLEND_GREEN:
        ALOGE("Called unimplemented blend intrinsic BLEND_GREEN");
        rsAssert(false);
        break;
    case BLEND_BLUE:
        ALOGE("Called unimplemented blend intrinsic BLEND_BLUE");
        rsAssert(false);
        break;
    case BLEND_HUE:
        ALOGE("Called unimplemented blend intrinsic BLEND_HUE");
        rsAssert(false);
        break;
    case BLEND_SATURATION:
        ALOGE("Called unimplemented blend intrinsic BLEND_SATURATION");
        rsAssert(false);
        break;
    case BLEND_COLOR:
        ALOGE("Called unimplemented blend intrinsic BLEND_COLOR");
        rsAssert(false);
        break;
    case BLEND_LUMINOSITY:
        ALOGE("Called unimplemented blend intrinsic BLEND_LUMINOSITY");
        rsAssert(false);
        break;

    default:
        ALOGE("Called unimplemented value %d", info->slot);
        rsAssert(false);

    }
}


RsdCpuScriptIntrinsicBlend::RsdCpuScriptIntrinsicBlend(RsdCpuReferenceImpl *ctx,
                                                       const Script *s, const Element *e)
            : RsdCpuScriptIntrinsic(ctx, s, e, RS_SCRIPT_INTRINSIC_ID_BLEND) {

    mRootPtr = &kernel;
}

RsdCpuScriptIntrinsicBlend::~RsdCpuScriptIntrinsicBlend() {
}

void RsdCpuScriptIntrinsicBlend::populateScript(Script *s) {
    s->mHal.info.exportedVariableCount = 0;
}

RsdCpuScriptImpl * rsdIntrinsic_Blend(RsdCpuReferenceImpl *ctx,
                                      const Script *s, const Element *e) {
    return new RsdCpuScriptIntrinsicBlend(ctx, s, e);
}
