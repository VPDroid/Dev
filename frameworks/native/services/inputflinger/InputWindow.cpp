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

#define LOG_TAG "InputWindow"
#define LOG_NDEBUG 0

#include "InputWindow.h"

#include <cutils/log.h>

#include <ui/Rect.h>
#include <ui/Region.h>

namespace android {

// --- InputWindowInfo ---
void InputWindowInfo::addTouchableRegion(const Rect& region) {
    touchableRegion.orSelf(region);
}

bool InputWindowInfo::touchableRegionContainsPoint(int32_t x, int32_t y) const {
    return touchableRegion.contains(x,y);
}

bool InputWindowInfo::frameContainsPoint(int32_t x, int32_t y) const {
    return x >= frameLeft && x < frameRight
            && y >= frameTop && y < frameBottom;
}

bool InputWindowInfo::isTrustedOverlay() const {
    return layoutParamsType == TYPE_INPUT_METHOD
            || layoutParamsType == TYPE_INPUT_METHOD_DIALOG
            || layoutParamsType == TYPE_MAGNIFICATION_OVERLAY
            || layoutParamsType == TYPE_STATUS_BAR
            || layoutParamsType == TYPE_NAVIGATION_BAR
            || layoutParamsType == TYPE_SECURE_SYSTEM_OVERLAY;
}

bool InputWindowInfo::supportsSplitTouch() const {
    return layoutParamsFlags & FLAG_SPLIT_TOUCH;
}

bool InputWindowInfo::overlaps(const InputWindowInfo* other) const {
    return frameLeft < other->frameRight && frameRight > other->frameLeft
            && frameTop < other->frameBottom && frameBottom > other->frameTop;
}


// --- InputWindowHandle ---

InputWindowHandle::InputWindowHandle(const sp<InputApplicationHandle>& inputApplicationHandle) :
    inputApplicationHandle(inputApplicationHandle), mInfo(NULL) {
}

InputWindowHandle::~InputWindowHandle() {
    delete mInfo;
}

void InputWindowHandle::releaseInfo() {
    if (mInfo) {
        delete mInfo;
        mInfo = NULL;
    }
}

} // namespace android
