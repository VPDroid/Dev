/*
 * Copyright (C) 2014 The Android Open Source Project
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

package android.content.pm;

import android.content.pm.IPackageDeleteObserver2;
import android.content.pm.IPackageInstallerCallback;
import android.content.pm.IPackageInstallerSession;
import android.content.pm.PackageInstaller;
import android.content.pm.ParceledListSlice;
import android.content.IntentSender;

import android.graphics.Bitmap;

/** {@hide} */
interface IPackageInstaller {
    int createSession(in PackageInstaller.SessionParams params, String installerPackageName, int userId);

    void updateSessionAppIcon(int sessionId, in Bitmap appIcon);
    void updateSessionAppLabel(int sessionId, String appLabel);

    void abandonSession(int sessionId);

    IPackageInstallerSession openSession(int sessionId);

    PackageInstaller.SessionInfo getSessionInfo(int sessionId);

    ParceledListSlice getAllSessions(int userId);
    ParceledListSlice getMySessions(String installerPackageName, int userId);

    void registerCallback(IPackageInstallerCallback callback, int userId);
    void unregisterCallback(IPackageInstallerCallback callback);

    void uninstall(String packageName, String callerPackageName, int flags,
            in IntentSender statusReceiver, int userId);

    void setPermissionsResult(int sessionId, boolean accepted);
}
