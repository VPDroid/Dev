// Copyright (C) 2011 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ip.rsh"
#pragma rs_fp_relaxed

uchar alpha = 0x0;

uchar4 RS_KERNEL setImageAlpha(uchar4 in, uint32_t x, uint32_t y) {
    uchar4 out;
    out.rgba = convert_uchar4((convert_uint4(in.rgba) * alpha) >> (uint4)8);
    out.a = alpha;
    return out;
}

