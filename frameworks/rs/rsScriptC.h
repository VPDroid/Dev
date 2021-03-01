/*
 * Copyright (C) 2009-2012 The Android Open Source Project
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

#ifndef ANDROID_RS_SCRIPT_C_H
#define ANDROID_RS_SCRIPT_C_H

#include "rsEnv.h"
#include "rsScript.h"

#if !defined(RS_COMPATIBILITY_LIB) && !defined(ANDROID_RS_SERIALIZE)
#include "bcinfo/BitcodeTranslator.h"
#endif

// ---------------------------------------------------------------------------
namespace android {
namespace renderscript {


/** This class represents a script compiled from an .rs file. */
class ScriptC : public Script {
public:
    typedef int (*RunScript_t)();
    typedef void (*VoidFunc_t)();

    ScriptC(Context *);
    virtual ~ScriptC();

    virtual void Invoke(Context *rsc, uint32_t slot, const void *data, size_t len);

    virtual uint32_t run(Context *);

    virtual void runForEach(Context *rsc,
                            uint32_t slot,
                            const Allocation ** ains,
                            size_t inLen,
                            Allocation * aout,
                            const void * usr,
                            size_t usrBytes,
                            const RsScriptCall *sc = nullptr);

    virtual void serialize(Context *rsc, OStream *stream) const {    }
    virtual RsA3DClassID getClassId() const { return RS_A3D_CLASS_ID_SCRIPT_C; }
    static Type *createFromStream(Context *rsc, IStream *stream) { return nullptr; }

    bool runCompiler(Context *rsc, const char *resName, const char *cacheDir,
                     const uint8_t *bitcode, size_t bitcodeLen);

//protected:
    void setupScript(Context *);
    void setupGLState(Context *);

#if !defined(RS_COMPATIBILITY_LIB)
    static bool createCacheDir(const char *cacheDir);
#endif
private:
#if !defined(RS_COMPATIBILITY_LIB) && !defined(ANDROID_RS_SERIALIZE)
    bcinfo::BitcodeTranslator *BT;
#endif
};

}
}
#endif
