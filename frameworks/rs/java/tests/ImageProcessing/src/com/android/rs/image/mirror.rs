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

#include "ip.rsh"
#pragma rs_fp_relaxed

int32_t gWidth;
int32_t gHeight;
rs_allocation gIn;

uchar4 RS_KERNEL mirror(uint32_t x, uint32_t y) {
    uint32_t x0 = gWidth-x-1;
    uchar4 p = rsGetElementAt_uchar4(gIn, x0, y);
    return p;
}
