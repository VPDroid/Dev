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

#include <SLES/OpenSLES.h>
#include "channels.h"

// Return an OpenSL ES channel mask, as used in SLDataFormat_PCM.channelMask
SLuint32 channelCountToMask(unsigned channelCount)
{
    // FIXME channel mask is not yet implemented by Stagefright, so use a reasonable default
    //       that is computed from the channel count
    switch (channelCount) {
    case 1:
        // see explanation in data.c re: default channel mask for mono
        return SL_SPEAKER_FRONT_LEFT;
    case 2:
        return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    // Android-specific
    case 3:
        return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT | SL_SPEAKER_FRONT_CENTER;
    case 4:
        return SL_ANDROID_SPEAKER_QUAD;
    case 5:
        return SL_ANDROID_SPEAKER_QUAD | SL_SPEAKER_FRONT_CENTER;
    case 6:
        return SL_ANDROID_SPEAKER_5DOT1;
    case 7:
        return SL_ANDROID_SPEAKER_5DOT1 | SL_SPEAKER_BACK_CENTER;
    case 8:
        return SL_ANDROID_SPEAKER_7DOT1;
    // FIXME FCC_8
    default:
        return UNKNOWN_CHANNELMASK;
    }
}
