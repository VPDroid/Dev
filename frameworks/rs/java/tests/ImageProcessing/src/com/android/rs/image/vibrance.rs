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

#include "ip.rsh"
#pragma rs_fp_relaxed

float vibrance = 0.f;

static const float Rf = 0.2999f;
static const float Gf = 0.587f;
static const float Bf = 0.114f;

static float Vib = 0.f;

uchar4 RS_KERNEL vibranceKernel(uchar4 in) {
    int r = in.r;
    int g = in.g;
    int b = in.b;
    float red = (r-max(g, b)) * (1.f / 256.f);
    float S = (float)(Vib/(1+native_exp(-red*3)))+1;
    float MS = 1.0f - S;
    float Rt = Rf * MS;
    float Gt = Gf * MS;
    float Bt = Bf * MS;

    float R = r;
    float G = g;
    float B = b;

    float Rc = R * (Rt + S) + G * Gt + B * Bt;
    float Gc = R * Rt + G * (Gt + S) + B * Bt;
    float Bc = R * Rt + G * Gt + B * (Bt + S);

    uchar4 o;
    o.r = clamp((int) Rc, 0, 255);
    o.g = clamp((int) Gc, 0, 255);
    o.b = clamp((int) Bc, 0, 255);
    o.a = 0xff;
    return o;
}

void prepareVibrance() {
    Vib = vibrance/100.f;
}
