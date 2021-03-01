/* Copyright (C) 2014 The Android Open Source Project
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

package android.media.audiopolicy;

import android.media.AudioFocusInfo;

/**
 * @hide
 */
oneway interface IAudioPolicyCallback {

    // callbacks for audio focus
    void notifyAudioFocusGrant(in AudioFocusInfo afi, int requestResult);
    void notifyAudioFocusLoss(in AudioFocusInfo afi, boolean wasNotified);

    // callback for mix activity status update
    void notifyMixStateUpdate(in String regId, int state);
}
