/*
 * Copyright (C) 2011 The Android Open Source Project
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

#pragma version(1)
#pragma rs java_package_name(com.example.android.rs.computeperf)

const int dim = 2048;
rs_allocation gBuf;

void __attribute__((kernel)) k_x(uchar in, uint32_t x) {
    for (int i=0; i<dim; i++) {
        rsSetElementAt_uchar(gBuf, 1, i, x);
    }
}

uchar __attribute__((kernel)) k_xy(uint32_t x, uint32_t y) {
    return 0;
}

