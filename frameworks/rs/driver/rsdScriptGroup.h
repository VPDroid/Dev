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

#ifndef RSD_SCRIPT_GROUP_H
#define RSD_SCRIPT_GROUP_H

#include <rs_hal.h>

bool rsdScriptGroupInit(const android::renderscript::Context *rsc,
                        android::renderscript::ScriptGroupBase *sg);
void rsdScriptGroupSetInput(const android::renderscript::Context *rsc,
                            const android::renderscript::ScriptGroup *sg,
                            const android::renderscript::ScriptKernelID *kid,
                            android::renderscript::Allocation *);
void rsdScriptGroupSetOutput(const android::renderscript::Context *rsc,
                             const android::renderscript::ScriptGroup *sg,
                             const android::renderscript::ScriptKernelID *kid,
                             android::renderscript::Allocation *);
void rsdScriptGroupExecute(const android::renderscript::Context *rsc,
                           const android::renderscript::ScriptGroupBase *sg);
void rsdScriptGroupDestroy(const android::renderscript::Context *rsc,
                           const android::renderscript::ScriptGroupBase *sg);
void rsdScriptGroupUpdateCachedObject(const android::renderscript::Context *rsc,
                                      const android::renderscript::ScriptGroup *sg,
                                      android::renderscript::rs_script_group *obj);

#endif // RSD_SCRIPT_GROUP_H
