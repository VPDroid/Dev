/*
 * Copyright (C) 2011-2013 The Android Open Source Project
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

// exports unavailable mathlib functions to compat lib


typedef unsigned int uint32_t;
typedef int int32_t;

extern uint32_t SC_abs_i32(int32_t v);
uint32_t __attribute__((overloadable)) abs(int32_t v) {return SC_abs_i32(v);}

#define IMPORT_F32_FN_F32(func)                                         \
    extern float SC_##func##f(float v);                                 \
    float __attribute__((overloadable)) func(float v) {return SC_##func##f(v);}

#define IMPORT_F32_FN_F32_F32(func)                                     \
    extern float SC_##func##f(float t, float v);                        \
    float __attribute__((overloadable)) func(float t, float v) {return SC_##func##f(t, v);}

IMPORT_F32_FN_F32(acos)
IMPORT_F32_FN_F32(acosh)
IMPORT_F32_FN_F32(asin)
IMPORT_F32_FN_F32(asinh)
IMPORT_F32_FN_F32(atan)
IMPORT_F32_FN_F32_F32(atan2)
IMPORT_F32_FN_F32(atanh)
IMPORT_F32_FN_F32(cbrt)
IMPORT_F32_FN_F32(ceil)
IMPORT_F32_FN_F32_F32(copysign)
IMPORT_F32_FN_F32(cos)
IMPORT_F32_FN_F32(cosh)
IMPORT_F32_FN_F32(erfc)
IMPORT_F32_FN_F32(erf)
IMPORT_F32_FN_F32(exp)
IMPORT_F32_FN_F32(exp2)
IMPORT_F32_FN_F32(expm1)
IMPORT_F32_FN_F32_F32(fdim)
IMPORT_F32_FN_F32(floor)
extern float SC_fmaf(float u, float t, float v);
float __attribute__((overloadable)) fma(float u, float t, float v) {return SC_fmaf(u, t, v);}
IMPORT_F32_FN_F32_F32(fmax)
IMPORT_F32_FN_F32_F32(fmin)
IMPORT_F32_FN_F32_F32(fmod)
extern float SC_frexpf(float v, int* ptr);
float __attribute__((overloadable)) frexp(float v, int* ptr) {return SC_frexpf(v, ptr);}
IMPORT_F32_FN_F32_F32(hypot)
extern int SC_ilogbf(float v);
int __attribute__((overloadable)) ilogb(float v) {return SC_ilogbf(v); }
extern float SC_ldexpf(float v, int i);
float __attribute__((overloadable)) ldexp(float v, int i) {return SC_ldexpf(v, i);}
IMPORT_F32_FN_F32(lgamma)
extern float SC_lgammaf_r(float v, int* ptr);
float __attribute__((overloadable)) lgamma(float v, int* ptr) {return SC_lgammaf_r(v, ptr);}
IMPORT_F32_FN_F32(log)
IMPORT_F32_FN_F32(log10)
IMPORT_F32_FN_F32(log1p)
IMPORT_F32_FN_F32(logb)
extern float SC_modff(float v, float* ptr);
float modf(float v, float* ptr) {return SC_modff(v, ptr);}
IMPORT_F32_FN_F32_F32(nextafter)
IMPORT_F32_FN_F32_F32(pow)
IMPORT_F32_FN_F32_F32(remainder)
extern float SC_remquof(float t, float v, int* ptr);
float remquo(float t, float v, int* ptr) {return SC_remquof(t, v, ptr);}
IMPORT_F32_FN_F32(rint)
IMPORT_F32_FN_F32(round)
IMPORT_F32_FN_F32(sin)
IMPORT_F32_FN_F32(sinh)
IMPORT_F32_FN_F32(sqrt)
IMPORT_F32_FN_F32(tan)
IMPORT_F32_FN_F32(tanh)
IMPORT_F32_FN_F32(tgamma)
IMPORT_F32_FN_F32(trunc)

extern float SC_randf2(float min, float max);
float __attribute__((overloadable)) rsRand(float min, float max) {
  return SC_randf2(min, max);
}

#ifdef RS_COMPATIBILITY_LIB

// !!! DANGER !!!
// These functions are potentially missing on older Android versions.
// Work around the issue by supplying our own variants.
// !!! DANGER !!!

// The logbl() implementation is taken from the latest bionic/, since
// double == long double on Android.
extern "C" long double logbl(long double x) { return logb(x); }

// __aeabi_idiv0 is a missing function in libcompiler_rt.so, so we just
// pick the simplest implementation based on the ARM EABI doc.
extern "C" int __aeabi_idiv0(int v) { return v; }

#endif // compatibility lib
