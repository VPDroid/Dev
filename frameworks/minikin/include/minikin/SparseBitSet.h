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

#ifndef MINIKIN_SPARSE_BIT_SET_H
#define MINIKIN_SPARSE_BIT_SET_H

#include <stdint.h>
#include <sys/types.h>
#include <UniquePtr.h>

// ---------------------------------------------------------------------------

namespace android {

// This is an implementation of a set of integers. It is optimized for
// values that are somewhat sparse, in the ballpark of a maximum value
// of thousands to millions. It is particularly efficient when there are
// large gaps. The motivating example is Unicode coverage of a font, but
// the abstraction itself is fully general.

class SparseBitSet {
public:
    SparseBitSet(): mMaxVal(0) {
    }

    // Clear the set
    void clear();

    // Initialize the set to a new value, represented by ranges. For
    // simplicity, these ranges are arranged as pairs of values,
    // inclusive of start, exclusive of end, laid out in a uint32 array.
    void initFromRanges(const uint32_t* ranges, size_t nRanges);

    // Determine whether the value is included in the set
    bool get(uint32_t ch) const {
        if (ch >= mMaxVal) return false;
        uint32_t *bitmap = &mBitmaps[mIndices[ch >> kLogValuesPerPage]];
        uint32_t index = ch & kPageMask;
        return (bitmap[index >> kLogBitsPerEl] & (kElFirst >> (index & kElMask))) != 0;
    }

    // One more than the maximum value in the set, or zero if empty
    uint32_t length() const {
        return mMaxVal;
    }

    // The next set bit starting at fromIndex, inclusive, or kNotFound
    // if none exists.
    uint32_t nextSetBit(uint32_t fromIndex) const;

    static const uint32_t kNotFound = ~0u;

private:
    static const int kLogValuesPerPage = 8;
    static const int kPageMask = (1 << kLogValuesPerPage) - 1;
    static const int kLogBytesPerEl = 2;
    static const int kLogBitsPerEl = kLogBytesPerEl + 3;
    static const int kElMask = (1 << kLogBitsPerEl) - 1;
    // invariant: sizeof(element) == (1 << kLogBytesPerEl)
    typedef uint32_t element;
    static const element kElAllOnes = ~((element)0);
    static const element kElFirst = ((element)1) << kElMask;
    static const uint32_t noZeroPage = ~0u;

    static uint32_t calcNumPages(const uint32_t* ranges, size_t nRanges);
    static int CountLeadingZeros(element x);

    uint32_t mMaxVal;
    UniquePtr<uint32_t[]> mIndices;
    UniquePtr<element[]> mBitmaps;
    uint32_t mZeroPageIndex;
};

// Note: this thing cannot be used in vectors yet. If that were important, we'd need to
// make the copy constructor work, and probably set up move traits as well.

}; // namespace android

#endif // MINIKIN_SPARSE_BIT_SET_H
