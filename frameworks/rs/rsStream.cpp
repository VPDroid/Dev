
/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include "rsContext.h"
#include "rsStream.h"

using namespace android;
using namespace android::renderscript;

IStream::IStream(const uint8_t *buf, bool use64) {
    mData = buf;
    mPos = 0;
    mUse64 = use64;
}

void IStream::loadByteArray(void *dest, size_t numBytes) {
    memcpy(dest, mData + mPos, numBytes);
    mPos += numBytes;
}

uint64_t IStream::loadOffset() {
    uint64_t tmp;
    if (mUse64) {
        mPos = (mPos + 7) & (~7);
        tmp = reinterpret_cast<const uint64_t *>(&mData[mPos])[0];
        mPos += sizeof(uint64_t);
        return tmp;
    }
    return loadU32();
}

const char * IStream::loadString() {
    uint32_t len = loadU32();
    const char *s = rsuCopyString((const char *)&mData[mPos], len);
    mPos += len;
    return s;
}

// Output stream implementation
OStream::OStream(uint64_t len, bool use64) {
    mData = (uint8_t*)malloc(len);
    mLength = len;
    mPos = 0;
    mUse64 = use64;
}

OStream::~OStream() {
    free(mData);
}

void OStream::addByteArray(const void *src, size_t numBytes) {
    // We need to potentially grow more than once if the number of byes we write is substantial
    while (mPos + numBytes >= mLength) {
        growSize();
    }
    memcpy(mData + mPos, src, numBytes);
    mPos += numBytes;
}

void OStream::addOffset(uint64_t v) {
    if (mUse64) {
        mPos = (mPos + 7) & (~7);
        if (mPos + sizeof(v) >= mLength) {
            growSize();
        }
        mData[mPos++] = (uint8_t)(v & 0xff);
        mData[mPos++] = (uint8_t)((v >> 8) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 16) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 24) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 32) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 40) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 48) & 0xff);
        mData[mPos++] = (uint8_t)((v >> 56) & 0xff);
    } else {
        addU32(v);
    }
}

void OStream::addString(const char *s, size_t len) {
    addU32(len);
    if (mPos + len*sizeof(char) >= mLength) {
        growSize();
    }
    char *stringData = reinterpret_cast<char *>(&mData[mPos]);
    memcpy(stringData, s, len);
    mPos += len*sizeof(char);
}

void OStream::addString(const char *s) {
    addString(s, strlen(s));
}

void OStream::growSize() {
    uint8_t *newData = (uint8_t*)malloc(mLength*2);
    memcpy(newData, mData, mLength*sizeof(uint8_t));
    mLength = mLength * 2;
    free(mData);
    mData = newData;
}


