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

#include <ui/UiConfig.h>

namespace android {

#ifdef FRAMEBUFFER_FORCE_FORMAT
// We need the two-level macro to stringify the contents of a macro argument
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#endif

void appendUiConfigString(String8& configStr)
{
    static const char* config =
            " [libui"
#ifdef FRAMEBUFFER_FORCE_FORMAT
            " FRAMEBUFFER_FORCE_FORMAT=" TOSTRING(FRAMEBUFFER_FORCE_FORMAT)
#endif
            "]";
    configStr.append(config);
}


}; // namespace android
