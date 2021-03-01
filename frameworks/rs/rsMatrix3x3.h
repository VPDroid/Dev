/*
 * Copyright (C) 2011-2012 The Android Open Source Project
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

#ifndef ANDROID_RS_MATRIX_3x3_H
#define ANDROID_RS_MATRIX_3x3_H

#include "rsType.h"


// ---------------------------------------------------------------------------
namespace android {
namespace renderscript {

struct Matrix3x3 : public rs_matrix3x3 {
    inline float get(uint32_t col, uint32_t row) const {
        return m[col*3 + row];
    }

    inline void set(uint32_t col, uint32_t row, float v) {
        m[col*3 + row] = v;
    }

    void loadIdentity();
    void load(const float *);
    void load(const rs_matrix3x3 *);

    void loadMultiply(const rs_matrix3x3 *lhs, const rs_matrix3x3 *rhs);

    void transpose();

    void multiply(const rs_matrix3x3 *rhs) {
        loadMultiply(this, rhs);
    }
};

}
}

#endif  // ANDROID_RS_MATRIX_3x3_H
