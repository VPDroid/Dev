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

#ifndef ANDROID_RSD_SHADER_CACHE_H
#define ANDROID_RSD_SHADER_CACHE_H

namespace android {
namespace renderscript {

class Context;

}
}


#if !defined(RS_SERVER) && !defined(RS_COMPATIBILITY_LIB)
#include <utils/String8.h>
#include <utils/Vector.h>
#else
#include "rsUtils.h"
#endif
class RsdShader;

// ---------------------------------------------------------------------------

// An element is a group of Components that occupies one cell in a structure.
class RsdShaderCache {
public:
    RsdShaderCache();
    virtual ~RsdShaderCache();

    void setActiveVertex(RsdShader *pv) {
        mVertexDirty = true;
        mVertex = pv;
    }

    void setActiveFragment(RsdShader *pf) {
        mFragmentDirty = true;
        mFragment = pf;
    }

    bool setup(const android::renderscript::Context *rsc);

    void cleanupVertex(RsdShader *s);
    void cleanupFragment(RsdShader *s);

    void cleanupAll();

    int32_t vtxAttribSlot(const android::String8 &attrName) const;
    int32_t vtxUniformSlot(uint32_t a) const {return mCurrent->vtxUniforms[a].slot;}
    uint32_t vtxUniformSize(uint32_t a) const {return mCurrent->vtxUniforms[a].arraySize;}
    int32_t fragUniformSlot(uint32_t a) const {return mCurrent->fragUniforms[a].slot;}
    uint32_t fragUniformSize(uint32_t a) const {return mCurrent->fragUniforms[a].arraySize;}

protected:
    bool link(const android::renderscript::Context *rsc);
    bool mFragmentDirty;
    bool mVertexDirty;
    RsdShader *mVertex;
    RsdShader *mFragment;

    struct UniformQueryData {
        char *name;
        uint32_t nameLength;
        int32_t writtenLength;
        int32_t arraySize;
        uint32_t type;
        UniformQueryData(uint32_t maxName) {
            name = nullptr;
            nameLength = maxName;
            if (nameLength > 0 ) {
                name = new char[nameLength];
            }
        }
        ~UniformQueryData() {
            if (name != nullptr) {
                delete[] name;
                name = nullptr;
            }
        }
    };
    struct UniformData {
        int32_t slot;
        uint32_t arraySize;
    };
    struct AttrData {
        int32_t slot;
        const char* name;
    };
    struct ProgramEntry {
        ProgramEntry(uint32_t numVtxAttr, uint32_t numVtxUnis,
                     uint32_t numFragUnis) : vtx(0), frag(0), program(0), vtxAttrCount(0),
                                             vtxAttrs(0), vtxUniforms(0), fragUniforms(0),
                                             fragUniformIsSTO(0) {
            vtxAttrCount = numVtxAttr;
            if (numVtxAttr) {
                vtxAttrs = new AttrData[numVtxAttr];
            }
            if (numVtxUnis) {
                vtxUniforms = new UniformData[numVtxUnis];
            }
            if (numFragUnis) {
                fragUniforms = new UniformData[numFragUnis];
                fragUniformIsSTO = new bool[numFragUnis];
            }
        }
        ~ProgramEntry() {
            if (vtxAttrs) {
                delete[] vtxAttrs;
                vtxAttrs = nullptr;
            }
            if (vtxUniforms) {
                delete[] vtxUniforms;
                vtxUniforms = nullptr;
            }
            if (fragUniforms) {
                delete[] fragUniforms;
                fragUniforms = nullptr;
            }
            if (fragUniformIsSTO) {
                delete[] fragUniformIsSTO;
                fragUniformIsSTO = nullptr;
            }
        }
        uint32_t vtx;
        uint32_t frag;
        uint32_t program;
        uint32_t vtxAttrCount;
        AttrData *vtxAttrs;
        UniformData *vtxUniforms;
        UniformData *fragUniforms;
        bool *fragUniformIsSTO;
    };
    android::Vector<ProgramEntry*> mEntries;
    ProgramEntry *mCurrent;

    bool hasArrayUniforms(RsdShader *vtx, RsdShader *frag);
    void populateUniformData(RsdShader *prog, uint32_t linkedID, UniformData *data);
    void updateUniformArrayData(const android::renderscript::Context *rsc,
                                RsdShader *prog, uint32_t linkedID,
                                UniformData *data, const char* logTag,
                                UniformQueryData **uniformList, uint32_t uniListSize);
};


#endif //ANDROID_RSD_SHADER_CACHE_H




