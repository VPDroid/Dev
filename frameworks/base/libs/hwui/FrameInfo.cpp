/*
 * Copyright (C) 2015 The Android Open Source Project
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
#include "FrameInfo.h"

#include <cstring>

namespace android {
namespace uirenderer {

const std::string FrameInfoNames[] = {
    "Flags",
    "IntendedVsync",
    "Vsync",
    "OldestInputEvent",
    "NewestInputEvent",
    "HandleInputStart",
    "AnimationStart",
    "PerformTraversalsStart",
    "DrawStart",
    "SyncQueued",
    "SyncStart",
    "IssueDrawCommandsStart",
    "SwapBuffers",
    "FrameCompleted",
};

void FrameInfo::importUiThreadInfo(int64_t* info) {
    memcpy(mFrameInfo, info, UI_THREAD_FRAME_INFO_SIZE * sizeof(int64_t));
}

} /* namespace uirenderer */
} /* namespace android */
