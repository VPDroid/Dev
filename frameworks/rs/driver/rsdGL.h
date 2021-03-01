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

#ifndef RSD_GL_H
#define RSD_GL_H

#include <rs_hal.h>
#include <EGL/egl.h>

#define RSD_CALL_GL(x, ...) rsc->setWatchdogGL(#x, __LINE__, __FILE__); x(__VA_ARGS__); rsc->setWatchdogGL(nullptr, 0, nullptr)

class RsdShaderCache;
class RsdVertexArrayState;
class RsdFrameBufferObj;

typedef struct RsdGLRec {
    struct {
        EGLint numConfigs;
        EGLint majorVersion;
        EGLint minorVersion;
        EGLConfig config;
        EGLContext context;
        EGLSurface surface;
        EGLSurface surfaceDefault;
        EGLDisplay display;
    } egl;

    struct {
        const uint8_t * vendor;
        const uint8_t * renderer;
        const uint8_t * version;
        const uint8_t * extensions;

        uint32_t majorVersion;
        uint32_t minorVersion;

        int32_t maxVaryingVectors;
        int32_t maxTextureImageUnits;

        int32_t maxFragmentTextureImageUnits;
        int32_t maxFragmentUniformVectors;

        int32_t maxVertexAttribs;
        int32_t maxVertexUniformVectors;
        int32_t maxVertexTextureUnits;

        bool OES_texture_npot;
        bool IMG_texture_npot;
        bool NV_texture_npot_2D_mipmap;
        float EXT_texture_max_aniso;
    } gl;

    ANativeWindow *wndSurface;
    ANativeWindow *currentWndSurface;

    RsdShaderCache *shaderCache;
    RsdVertexArrayState *vertexArrayState;
    RsdFrameBufferObj *currentFrameBuffer;
} RsdGL;

bool rsdGLSetInternalSurface(const android::renderscript::Context *rsc,
                             RsNativeWindow sur);
bool rsdGLInit(const android::renderscript::Context *rsc);
void rsdGLShutdown(const android::renderscript::Context *rsc);
bool rsdGLSetSurface(const android::renderscript::Context *rsc,
                     uint32_t w, uint32_t h, RsNativeWindow sur);
void rsdGLSwap(const android::renderscript::Context *rsc);
void rsdGLCheckError(const android::renderscript::Context *rsc,
                     const char *msg, bool isFatal = false);
void rsdGLSetPriority(const android::renderscript::Context *rsc,
                      int32_t priority);
void rsdGLClearColor(const android::renderscript::Context *rsc,
                     float r, float g, float b, float a);
void rsdGLClearDepth(const android::renderscript::Context *rsc, float v);
void rsdGLFinish(const android::renderscript::Context *rsc);
void rsdGLDrawQuadTexCoords(const android::renderscript::Context *rsc,
                            float x1, float y1, float z1, float u1, float v1,
                            float x2, float y2, float z2, float u2, float v2,
                            float x3, float y3, float z3, float u3, float v3,
                            float x4, float y4, float z4, float u4, float v4);

#endif
