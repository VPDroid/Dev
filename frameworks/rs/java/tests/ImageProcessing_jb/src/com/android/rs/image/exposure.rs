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

static float bright = 0.f;

void setBright(float v) {
    bright = 255.f / (255.f - v);
}

uchar4 RS_KERNEL exposure(uchar4 in)
{
    uchar4 out = {0, 0, 0, 255};
    float3 t = convert_float3(in.rgb);
    out.rgb = convert_uchar3(clamp(convert_int3(t * bright), 0, 255));
    return out;
}

