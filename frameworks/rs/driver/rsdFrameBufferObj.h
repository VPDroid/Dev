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

#ifndef _RSD_FRAMEBUFFER_OBJ_H_
#define _RSD_FRAMEBUFFER_OBJ_H_

#include <rsContext.h>

struct DrvAllocation;

class RsdFrameBufferObj {
public:
    RsdFrameBufferObj();
    ~RsdFrameBufferObj();

    void setActive(const android::renderscript::Context *rsc);
    void setColorTarget(DrvAllocation *color, uint32_t index) {
        mColorTargets[index] = color;
        mDirty = true;
    }
    void setDepthTarget(DrvAllocation *depth) {
        mDepthTarget = depth;
        mDirty = true;
    }
    void setDimensions(uint32_t width, uint32_t height) {
        mWidth = width;
        mHeight = height;
    }
protected:
    uint32_t mFBOId;
    DrvAllocation **mColorTargets;
    uint32_t mColorTargetsCount;
    DrvAllocation *mDepthTarget;

    uint32_t mWidth;
    uint32_t mHeight;

    bool mDirty;

    bool renderToFramebuffer();
    void checkError(const android::renderscript::Context *rsc);
    void setColorAttachment();
    void setDepthAttachment();
};

#endif //_RSD_FRAMEBUFFER_STATE_H_
