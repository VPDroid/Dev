/**
 * Copyright (c) 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.app;

import android.app.IActivityContainerCallback;
import android.content.Intent;
import android.content.IIntentSender;
import android.os.IBinder;
import android.view.InputEvent;
import android.view.Surface;

/** @hide */
interface IActivityContainer {
    void attachToDisplay(int displayId);
    void setSurface(in Surface surface, int width, int height, int density);
    int startActivity(in Intent intent);
    int startActivityIntentSender(in IIntentSender intentSender);
    int getDisplayId();
    int getStackId();
    boolean injectEvent(in InputEvent event);
    void release();
}
