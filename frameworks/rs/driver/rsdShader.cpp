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

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <rs_hal.h>
#include <rsContext.h>
#include <rsProgram.h>

#include "rsdCore.h"
#include "rsdAllocation.h"
#include "rsdShader.h"
#include "rsdShaderCache.h"

using namespace android;
using namespace android::renderscript;

RsdShader::RsdShader(const Program *p, uint32_t type,
                     const char * shaderText, size_t shaderLength,
                     const char** textureNames, size_t textureNamesCount,
                     const size_t *textureNamesLength) {
    mUserShader.replace(0, shaderLength, shaderText);
    mRSProgram = p;
    mType = type;
    initMemberVars();
    initAttribAndUniformArray();
    init(textureNames, textureNamesCount, textureNamesLength);

    for(size_t i=0; i < textureNamesCount; i++) {
        mTextureNames.push(String8(textureNames[i], textureNamesLength[i]));
    }
}

RsdShader::~RsdShader() {
    for (uint32_t i = 0; i < mStateBasedShaders.size(); i ++) {
        StateBasedKey *state = mStateBasedShaders.itemAt(i);
        if (state->mShaderID) {
            glDeleteShader(state->mShaderID);
        }
        delete state;
    }

    delete[] mAttribNames;
    delete[] mUniformNames;
    delete[] mUniformArraySizes;
}

void RsdShader::initMemberVars() {
    mDirty = true;
    mAttribCount = 0;
    mUniformCount = 0;

    mAttribNames = nullptr;
    mUniformNames = nullptr;
    mUniformArraySizes = nullptr;
    mCurrentState = nullptr;

    mIsValid = false;
}

RsdShader::StateBasedKey *RsdShader::getExistingState() {
    RsdShader::StateBasedKey *returnKey = nullptr;

    for (uint32_t i = 0; i < mStateBasedShaders.size(); i ++) {
        returnKey = mStateBasedShaders.itemAt(i);

        for (uint32_t ct = 0; ct < mRSProgram->mHal.state.texturesCount; ct ++) {
            uint32_t texType = 0;
            if (mRSProgram->mHal.state.textureTargets[ct] == RS_TEXTURE_2D) {
                Allocation *a = mRSProgram->mHal.state.textures[ct];
                if (a && a->mHal.state.surfaceTextureID) {
                    texType = GL_TEXTURE_EXTERNAL_OES;
                } else {
                    texType = GL_TEXTURE_2D;
                }
            } else {
                texType = GL_TEXTURE_CUBE_MAP;
            }
            if (texType != returnKey->mTextureTargets[ct]) {
                returnKey = nullptr;
                break;
            }
        }
    }
    return returnKey;
}

uint32_t RsdShader::getStateBasedShaderID(const Context *rsc) {
    StateBasedKey *state = getExistingState();
    if (state != nullptr) {
        mCurrentState = state;
        return mCurrentState->mShaderID;
    }
    // We have not created a shader for this particular state yet
    state = new StateBasedKey(mTextureCount);
    mCurrentState = state;
    mStateBasedShaders.add(state);
    createShader();
    loadShader(rsc);
    return mCurrentState->mShaderID;
}

void RsdShader::init(const char** textureNames, size_t textureNamesCount,
                     const size_t *textureNamesLength) {
    uint32_t attribCount = 0;
    uint32_t uniformCount = 0;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.inputElementsCount; ct++) {
        initAddUserElement(mRSProgram->mHal.state.inputElements[ct], mAttribNames,
                           nullptr, &attribCount, RS_SHADER_ATTR);
    }
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.constantsCount; ct++) {
        initAddUserElement(mRSProgram->mHal.state.constantTypes[ct]->getElement(),
                           mUniformNames, mUniformArraySizes, &uniformCount, RS_SHADER_UNI);
    }

    mTextureUniformIndexStart = uniformCount;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.texturesCount; ct++) {
        mUniformNames[uniformCount] = "UNI_";
        mUniformNames[uniformCount].append(textureNames[ct], textureNamesLength[ct]);
        mUniformArraySizes[uniformCount] = 1;
        uniformCount++;
    }
}

std::string RsdShader::getGLSLInputString() const {
    std::string s;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.inputElementsCount; ct++) {
        const Element *e = mRSProgram->mHal.state.inputElements[ct];
        for (uint32_t field=0; field < e->mHal.state.fieldsCount; field++) {
            const Element *f = e->mHal.state.fields[field];

            // Cannot be complex
            rsAssert(!f->mHal.state.fieldsCount);
            switch (f->mHal.state.vectorSize) {
            case 1: s.append("attribute float ATTRIB_"); break;
            case 2: s.append("attribute vec2 ATTRIB_"); break;
            case 3: s.append("attribute vec3 ATTRIB_"); break;
            case 4: s.append("attribute vec4 ATTRIB_"); break;
            default:
                rsAssert(0);
            }

            s.append(e->mHal.state.fieldNames[field]);
            s.append(";\n");
        }
    }
    return s;
}

void RsdShader::appendAttributes() {
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.inputElementsCount; ct++) {
        const Element *e = mRSProgram->mHal.state.inputElements[ct];
        for (uint32_t field=0; field < e->mHal.state.fieldsCount; field++) {
            const Element *f = e->mHal.state.fields[field];
            const char *fn = e->mHal.state.fieldNames[field];

            // Cannot be complex
            rsAssert(!f->mHal.state.fieldsCount);
            switch (f->mHal.state.vectorSize) {
            case 1: mShader.append("attribute float ATTRIB_"); break;
            case 2: mShader.append("attribute vec2 ATTRIB_"); break;
            case 3: mShader.append("attribute vec3 ATTRIB_"); break;
            case 4: mShader.append("attribute vec4 ATTRIB_"); break;
            default:
                rsAssert(0);
            }

            mShader.append(fn);
            mShader.append(";\n");
        }
    }
}

void RsdShader::appendTextures() {

    // TODO: this does not yet handle cases where the texture changes between IO
    // input and local
    bool appendUsing = true;
    for (uint32_t ct = 0; ct < mRSProgram->mHal.state.texturesCount; ct ++) {
        if (mRSProgram->mHal.state.textureTargets[ct] == RS_TEXTURE_2D) {
            Allocation *a = mRSProgram->mHal.state.textures[ct];
            if (a && a->mHal.state.surfaceTextureID) {
                if(appendUsing) {
                    mShader.append("#extension GL_OES_EGL_image_external : require\n");
                    appendUsing = false;
                }
                mShader.append("uniform samplerExternalOES UNI_");
                mCurrentState->mTextureTargets[ct] = GL_TEXTURE_EXTERNAL_OES;
            } else {
                mShader.append("uniform sampler2D UNI_");
                mCurrentState->mTextureTargets[ct] = GL_TEXTURE_2D;
            }
        } else {
            mShader.append("uniform samplerCube UNI_");
            mCurrentState->mTextureTargets[ct] = GL_TEXTURE_CUBE_MAP;
        }

        mShader.append(mTextureNames[ct]);
        mShader.append(";\n");
    }
}

bool RsdShader::createShader() {
    mShader.clear();
    if (mType == GL_FRAGMENT_SHADER) {
        mShader.append("precision mediump float;\n");
    }
    appendUserConstants();
    appendAttributes();
    appendTextures();
    mShader.append(mUserShader);

    return true;
}

bool RsdShader::loadShader(const Context *rsc) {
    mCurrentState->mShaderID = glCreateShader(mType);
    rsAssert(mCurrentState->mShaderID);

    if(!mShader.length()) {
        createShader();
    }

    if (rsc->props.mLogShaders) {
        ALOGV("Loading shader type %x, ID %i", mType, mCurrentState->mShaderID);
        ALOGV("%s", mShader.c_str());
    }

    if (mCurrentState->mShaderID) {
        const char * ss = mShader.c_str();
        RSD_CALL_GL(glShaderSource, mCurrentState->mShaderID, 1, &ss, nullptr);
        RSD_CALL_GL(glCompileShader, mCurrentState->mShaderID);

        GLint compiled = 0;
        RSD_CALL_GL(glGetShaderiv, mCurrentState->mShaderID, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            RSD_CALL_GL(glGetShaderiv, mCurrentState->mShaderID, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    RSD_CALL_GL(glGetShaderInfoLog, mCurrentState->mShaderID, infoLen, nullptr, buf);
                    rsc->setError(RS_ERROR_FATAL_PROGRAM_LINK, buf);
                    free(buf);
                }
                RSD_CALL_GL(glDeleteShader, mCurrentState->mShaderID);
                mCurrentState->mShaderID = 0;
                return false;
            }
        }
    }

    if (rsc->props.mLogShaders) {
        ALOGV("--Shader load result %x ", glGetError());
    }
    mIsValid = true;
    return true;
}

void RsdShader::appendUserConstants() {
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.constantsCount; ct++) {
        const Element *e = mRSProgram->mHal.state.constantTypes[ct]->getElement();
        for (uint32_t field=0; field < e->mHal.state.fieldsCount; field++) {
            const Element *f = e->mHal.state.fields[field];
            const char *fn = e->mHal.state.fieldNames[field];

            // Cannot be complex
            rsAssert(!f->mHal.state.fieldsCount);
            if (f->mHal.state.dataType == RS_TYPE_MATRIX_4X4) {
                mShader.append("uniform mat4 UNI_");
            } else if (f->mHal.state.dataType == RS_TYPE_MATRIX_3X3) {
                mShader.append("uniform mat3 UNI_");
            } else if (f->mHal.state.dataType == RS_TYPE_MATRIX_2X2) {
                mShader.append("uniform mat2 UNI_");
            } else {
                switch (f->mHal.state.vectorSize) {
                case 1: mShader.append("uniform float UNI_"); break;
                case 2: mShader.append("uniform vec2 UNI_"); break;
                case 3: mShader.append("uniform vec3 UNI_"); break;
                case 4: mShader.append("uniform vec4 UNI_"); break;
                default:
                    rsAssert(0);
                }
            }

            mShader.append(fn);
            if (e->mHal.state.fieldArraySizes[field] > 1) {
                mShader += "[";
                mShader += std::to_string(e->mHal.state.fieldArraySizes[field]);
                mShader += "]";
            }
            mShader.append(";\n");
        }
    }
}

void RsdShader::logUniform(const Element *field, const float *fd, uint32_t arraySize ) {
    RsDataType dataType = field->mHal.state.dataType;
    uint32_t elementSize = field->mHal.state.elementSizeBytes / sizeof(float);
    for (uint32_t i = 0; i < arraySize; i ++) {
        if (arraySize > 1) {
            ALOGV("Array Element [%u]", i);
        }
        if (dataType == RS_TYPE_MATRIX_4X4) {
            ALOGV("Matrix4x4");
            ALOGV("{%f, %f, %f, %f",  fd[0], fd[4], fd[8], fd[12]);
            ALOGV(" %f, %f, %f, %f",  fd[1], fd[5], fd[9], fd[13]);
            ALOGV(" %f, %f, %f, %f",  fd[2], fd[6], fd[10], fd[14]);
            ALOGV(" %f, %f, %f, %f}", fd[3], fd[7], fd[11], fd[15]);
        } else if (dataType == RS_TYPE_MATRIX_3X3) {
            ALOGV("Matrix3x3");
            ALOGV("{%f, %f, %f",  fd[0], fd[3], fd[6]);
            ALOGV(" %f, %f, %f",  fd[1], fd[4], fd[7]);
            ALOGV(" %f, %f, %f}", fd[2], fd[5], fd[8]);
        } else if (dataType == RS_TYPE_MATRIX_2X2) {
            ALOGV("Matrix2x2");
            ALOGV("{%f, %f",  fd[0], fd[2]);
            ALOGV(" %f, %f}", fd[1], fd[3]);
        } else {
            switch (field->mHal.state.vectorSize) {
            case 1:
                ALOGV("Uniform 1 = %f", fd[0]);
                break;
            case 2:
                ALOGV("Uniform 2 = %f %f", fd[0], fd[1]);
                break;
            case 3:
                ALOGV("Uniform 3 = %f %f %f", fd[0], fd[1], fd[2]);
                break;
            case 4:
                ALOGV("Uniform 4 = %f %f %f %f", fd[0], fd[1], fd[2], fd[3]);
                break;
            default:
                rsAssert(0);
            }
        }
        ALOGV("Element size %u data=%p", elementSize, fd);
        fd += elementSize;
        ALOGV("New data=%p", fd);
    }
}

void RsdShader::setUniform(const Context *rsc, const Element *field, const float *fd,
                         int32_t slot, uint32_t arraySize ) {
    RsDataType dataType = field->mHal.state.dataType;
    if (dataType == RS_TYPE_MATRIX_4X4) {
        RSD_CALL_GL(glUniformMatrix4fv, slot, arraySize, GL_FALSE, fd);
    } else if (dataType == RS_TYPE_MATRIX_3X3) {
        RSD_CALL_GL(glUniformMatrix3fv, slot, arraySize, GL_FALSE, fd);
    } else if (dataType == RS_TYPE_MATRIX_2X2) {
        RSD_CALL_GL(glUniformMatrix2fv, slot, arraySize, GL_FALSE, fd);
    } else {
        switch (field->mHal.state.vectorSize) {
        case 1:
            RSD_CALL_GL(glUniform1fv, slot, arraySize, fd);
            break;
        case 2:
            RSD_CALL_GL(glUniform2fv, slot, arraySize, fd);
            break;
        case 3:
            RSD_CALL_GL(glUniform3fv, slot, arraySize, fd);
            break;
        case 4:
            RSD_CALL_GL(glUniform4fv, slot, arraySize, fd);
            break;
        default:
            rsAssert(0);
        }
    }
}

void RsdShader::setupSampler(const Context *rsc, const Sampler *s, const Allocation *tex) {
    RsdHal *dc = (RsdHal *)rsc->mHal.drv;

    GLenum trans[] = {
        GL_NEAREST, //RS_SAMPLER_NEAREST,
        GL_LINEAR, //RS_SAMPLER_LINEAR,
        GL_LINEAR_MIPMAP_LINEAR, //RS_SAMPLER_LINEAR_MIP_LINEAR,
        GL_REPEAT, //RS_SAMPLER_WRAP,
        GL_CLAMP_TO_EDGE, //RS_SAMPLER_CLAMP
        GL_LINEAR_MIPMAP_NEAREST, //RS_SAMPLER_LINEAR_MIP_NEAREST
    };

    GLenum transNP[] = {
        GL_NEAREST, //RS_SAMPLER_NEAREST,
        GL_LINEAR, //RS_SAMPLER_LINEAR,
        GL_LINEAR, //RS_SAMPLER_LINEAR_MIP_LINEAR,
        GL_CLAMP_TO_EDGE, //RS_SAMPLER_WRAP,
        GL_CLAMP_TO_EDGE, //RS_SAMPLER_CLAMP
        GL_LINEAR, //RS_SAMPLER_LINEAR_MIP_NEAREST,
    };

    // This tells us the correct texture type
    DrvAllocation *drvTex = (DrvAllocation *)tex->mHal.drv;
    const GLenum target = drvTex->glTarget;
    if (!target) {
        // this can happen if the user set the wrong allocation flags.
        rsc->setError(RS_ERROR_BAD_VALUE, "Allocation not compatible with sampler");
        return;
    }

    if (!dc->gl.gl.OES_texture_npot && tex->getType()->getIsNp2()) {
        if (tex->getHasGraphicsMipmaps() &&
            (dc->gl.gl.NV_texture_npot_2D_mipmap || dc->gl.gl.IMG_texture_npot)) {
            if (dc->gl.gl.NV_texture_npot_2D_mipmap) {
                RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                            trans[s->mHal.state.minFilter]);
            } else {
                switch (trans[s->mHal.state.minFilter]) {
                case GL_LINEAR_MIPMAP_LINEAR:
                    RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR_MIPMAP_NEAREST);
                    break;
                default:
                    RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                                trans[s->mHal.state.minFilter]);
                    break;
                }
            }
        } else {
            RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                        transNP[s->mHal.state.minFilter]);
        }
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MAG_FILTER,
                    transNP[s->mHal.state.magFilter]);
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_WRAP_S, transNP[s->mHal.state.wrapS]);
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_WRAP_T, transNP[s->mHal.state.wrapT]);
    } else {
        if (tex->getHasGraphicsMipmaps()) {
            RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                        trans[s->mHal.state.minFilter]);
        } else {
            RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MIN_FILTER,
                        transNP[s->mHal.state.minFilter]);
        }
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_MAG_FILTER, trans[s->mHal.state.magFilter]);
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_WRAP_S, trans[s->mHal.state.wrapS]);
        RSD_CALL_GL(glTexParameteri, target, GL_TEXTURE_WRAP_T, trans[s->mHal.state.wrapT]);
    }

    float anisoValue = rsMin(dc->gl.gl.EXT_texture_max_aniso, s->mHal.state.aniso);
    if (dc->gl.gl.EXT_texture_max_aniso > 1.0f) {
        RSD_CALL_GL(glTexParameterf, target, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisoValue);
    }

    rsdGLCheckError(rsc, "Sampler::setup tex env");
}

void RsdShader::setupTextures(const Context *rsc, RsdShaderCache *sc) {
    if (mRSProgram->mHal.state.texturesCount == 0) {
        return;
    }

    RsdHal *dc = (RsdHal *)rsc->mHal.drv;

    uint32_t numTexturesToBind = mRSProgram->mHal.state.texturesCount;
    uint32_t numTexturesAvailable = dc->gl.gl.maxFragmentTextureImageUnits;
    if (numTexturesToBind >= numTexturesAvailable) {
        ALOGE("Attempting to bind %u textures on shader id %p, but only %u are available",
             mRSProgram->mHal.state.texturesCount, this, numTexturesAvailable);
        rsc->setError(RS_ERROR_BAD_SHADER, "Cannot bind more textuers than available");
        numTexturesToBind = numTexturesAvailable;
    }

    for (uint32_t ct=0; ct < numTexturesToBind; ct++) {
        RSD_CALL_GL(glActiveTexture, GL_TEXTURE0 + ct);
        RSD_CALL_GL(glUniform1i, sc->fragUniformSlot(mTextureUniformIndexStart + ct), ct);

        if (!mRSProgram->mHal.state.textures[ct]) {
            // if nothing is bound, reset to default GL texture
            RSD_CALL_GL(glBindTexture, mCurrentState->mTextureTargets[ct], 0);
            continue;
        }

        DrvAllocation *drvTex = (DrvAllocation *)mRSProgram->mHal.state.textures[ct]->mHal.drv;

        if (mCurrentState->mTextureTargets[ct] != GL_TEXTURE_2D &&
            mCurrentState->mTextureTargets[ct] != GL_TEXTURE_CUBE_MAP &&
            mCurrentState->mTextureTargets[ct] != GL_TEXTURE_EXTERNAL_OES) {
            ALOGE("Attempting to bind unknown texture to shader id %p, texture unit %u",
                  this, ct);
            rsc->setError(RS_ERROR_BAD_SHADER, "Non-texture allocation bound to a shader");
        }
        RSD_CALL_GL(glBindTexture, mCurrentState->mTextureTargets[ct], drvTex->textureID);
        rsdGLCheckError(rsc, "ProgramFragment::setup tex bind");
        if (mRSProgram->mHal.state.samplers[ct]) {
            setupSampler(rsc, mRSProgram->mHal.state.samplers[ct],
                         mRSProgram->mHal.state.textures[ct]);
        } else {
            RSD_CALL_GL(glTexParameteri, mCurrentState->mTextureTargets[ct],
                GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            RSD_CALL_GL(glTexParameteri, mCurrentState->mTextureTargets[ct],
                GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            RSD_CALL_GL(glTexParameteri, mCurrentState->mTextureTargets[ct],
                GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            RSD_CALL_GL(glTexParameteri, mCurrentState->mTextureTargets[ct],
                GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            rsdGLCheckError(rsc, "ProgramFragment::setup basic tex env");
        }
        rsdGLCheckError(rsc, "ProgramFragment::setup uniforms");
    }

    RSD_CALL_GL(glActiveTexture, GL_TEXTURE0);
    mDirty = false;
    rsdGLCheckError(rsc, "ProgramFragment::setup");
}

void RsdShader::setupUserConstants(const Context *rsc, RsdShaderCache *sc, bool isFragment) {
    uint32_t uidx = 0;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.constantsCount; ct++) {
        Allocation *alloc = mRSProgram->mHal.state.constants[ct];

        if (!alloc) {
            ALOGE("Attempting to set constants on shader id %p, but alloc at slot %u is not set",
                  this, ct);
            rsc->setError(RS_ERROR_BAD_SHADER, "No constant allocation bound");
            continue;
        }

        const uint8_t *data = static_cast<const uint8_t *>(alloc->mHal.drvState.lod[0].mallocPtr);
        const Element *e = mRSProgram->mHal.state.constantTypes[ct]->getElement();
        for (uint32_t field=0; field < e->mHal.state.fieldsCount; field++) {
            const Element *f = e->mHal.state.fields[field];
            const char *fieldName = e->mHal.state.fieldNames[field];

            uint32_t offset = e->mHal.state.fieldOffsetBytes[field];
            const float *fd = reinterpret_cast<const float *>(&data[offset]);

            int32_t slot = -1;
            uint32_t arraySize = 1;
            if (!isFragment) {
                slot = sc->vtxUniformSlot(uidx);
                arraySize = sc->vtxUniformSize(uidx);
            } else {
                slot = sc->fragUniformSlot(uidx);
                arraySize = sc->fragUniformSize(uidx);
            }
            if (rsc->props.mLogShadersUniforms) {
                ALOGV("Uniform  slot=%i, offset=%i, constant=%i, field=%i, uidx=%i, name=%s",
                     slot, offset, ct, field, uidx, fieldName);
            }
            uidx ++;
            if (slot < 0) {
                continue;
            }

            if (rsc->props.mLogShadersUniforms) {
                logUniform(f, fd, arraySize);
            }
            setUniform(rsc, f, fd, slot, arraySize);
        }
    }
}

void RsdShader::setup(const android::renderscript::Context *rsc, RsdShaderCache *sc) {

    setupUserConstants(rsc, sc, mType == GL_FRAGMENT_SHADER);
    setupTextures(rsc, sc);
}

void RsdShader::initAttribAndUniformArray() {
    mAttribCount = 0;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.inputElementsCount; ct++) {
        const Element *elem = mRSProgram->mHal.state.inputElements[ct];
        mAttribCount += elem->mHal.state.fieldsCount;
    }

    mUniformCount = 0;
    for (uint32_t ct=0; ct < mRSProgram->mHal.state.constantsCount; ct++) {
        const Element *elem = mRSProgram->mHal.state.constantTypes[ct]->getElement();
        mUniformCount += elem->mHal.state.fieldsCount;
    }
    mUniformCount += mRSProgram->mHal.state.texturesCount;

    if (mAttribCount) {
        mAttribNames = new std::string[mAttribCount];
    }
    if (mUniformCount) {
        mUniformNames = new std::string[mUniformCount];
        mUniformArraySizes = new uint32_t[mUniformCount];
    }

    mTextureCount = mRSProgram->mHal.state.texturesCount;
}

void RsdShader::initAddUserElement(const Element *e, std::string *names,
                                   uint32_t *arrayLengths, uint32_t *count,
                                   const char *prefix) {
    rsAssert(e->mHal.state.fieldsCount);
    for (uint32_t ct=0; ct < e->mHal.state.fieldsCount; ct++) {
        const Element *ce = e->mHal.state.fields[ct];
        if (ce->mHal.state.fieldsCount) {
            initAddUserElement(ce, names, arrayLengths, count, prefix);
        } else {
            std::string tmp(prefix);
            tmp.append(e->mHal.state.fieldNames[ct]);
            names[*count] = tmp;
            if (arrayLengths) {
                arrayLengths[*count] = e->mHal.state.fieldArraySizes[ct];
            }
            (*count)++;
        }
    }
}
