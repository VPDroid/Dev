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

#ifndef RSD_BCC_H
#define RSD_BCC_H

#include <rs_hal.h>
#include <rsRuntime.h>

bool rsdScriptInit(const android::renderscript::Context *, android::renderscript::ScriptC *,
                   char const *resName, char const *cacheDir,
                   uint8_t const *bitcode, size_t bitcodeSize, uint32_t flags);
bool rsdInitIntrinsic(const android::renderscript::Context *rsc,
                      android::renderscript::Script *s,
                      RsScriptIntrinsicID iid,
                      android::renderscript::Element *e);

void rsdScriptInvokeFunction(const android::renderscript::Context *dc,
                             android::renderscript::Script *script,
                             uint32_t slot,
                             const void *params,
                             size_t paramLength);

void rsdScriptInvokeForEach(const android::renderscript::Context *rsc,
                            android::renderscript::Script *s,
                            uint32_t slot,
                            const android::renderscript::Allocation * ain,
                            android::renderscript::Allocation * aout,
                            const void * usr,
                            size_t usrLen,
                            const RsScriptCall *sc);

void rsdScriptInvokeForEachMulti(const android::renderscript::Context *rsc,
                                 android::renderscript::Script *s,
                                 uint32_t slot,
                                 const android::renderscript::Allocation ** ains,
                                 size_t inLen,
                                 android::renderscript::Allocation * aout,
                                 const void * usr,
                                 size_t usrLen,
                                 const RsScriptCall *sc);

int rsdScriptInvokeRoot(const android::renderscript::Context *dc,
                        android::renderscript::Script *script);
void rsdScriptInvokeInit(const android::renderscript::Context *dc,
                         android::renderscript::Script *script);
void rsdScriptInvokeFreeChildren(const android::renderscript::Context *dc,
                                 android::renderscript::Script *script);

void rsdScriptSetGlobalVar(const android::renderscript::Context *,
                           const android::renderscript::Script *,
                           uint32_t slot, void *data, size_t dataLen);
void rsdScriptGetGlobalVar(const android::renderscript::Context *,
                           const android::renderscript::Script *,
                           uint32_t slot, void *data, size_t dataLen);
void rsdScriptSetGlobalVarWithElemDims(const android::renderscript::Context *,
                                       const android::renderscript::Script *,
                                       uint32_t slot, void *data,
                                       size_t dataLength,
                                       const android::renderscript::Element *,
                                       const uint32_t *dims,
                                       size_t dimLength);
void rsdScriptSetGlobalBind(const android::renderscript::Context *,
                            const android::renderscript::Script *,
                            uint32_t slot, android::renderscript::Allocation *data);
void rsdScriptSetGlobalObj(const android::renderscript::Context *,
                           const android::renderscript::Script *,
                           uint32_t slot, android::renderscript::ObjectBase *data);

void rsdScriptSetGlobal(const android::renderscript::Context *dc,
                        const android::renderscript::Script *script,
                        uint32_t slot,
                        void *data,
                        size_t dataLength);
void rsdScriptGetGlobal(const android::renderscript::Context *dc,
                        const android::renderscript::Script *script,
                        uint32_t slot,
                        void *data,
                        size_t dataLength);
void rsdScriptDestroy(const android::renderscript::Context *dc,
                      android::renderscript::Script *script);

android::renderscript::Allocation * rsdScriptGetAllocationForPointer(
                        const android::renderscript::Context *dc,
                        const android::renderscript::Script *script,
                        const void *);

void rsdScriptUpdateCachedObject(const android::renderscript::Context *rsc,
                                 const android::renderscript::Script *script,
                                 android::renderscript::rs_script *obj);



#endif
