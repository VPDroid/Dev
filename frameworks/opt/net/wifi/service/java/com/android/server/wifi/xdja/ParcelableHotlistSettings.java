/*
 * Copyright (C) 2010 The Android Open Source Project
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

package com.android.server.wifi.xdja;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.Arrays;
import java.nio.ByteBuffer;

import android.net.wifi.WifiScanner.HotlistSettings;

public class ParcelableHotlistSettings implements Parcelable {

    public HotlistSettings mResult;

    public ParcelableHotlistSettings(HotlistSettings result) {
        mResult = result;
    }

    public HotlistSettings getResult() {
        return mResult;
    }

    /** Implement the Parcelable interface {@hide} */
    public int describeContents() {
        return 0;
    }

    /** Implement the Parcelable interface {@hide} */
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeParcelable(mResult, flags);
    }

    /** Implement the Parcelable interface {@hide} */
    public static final Creator<ParcelableHotlistSettings> CREATOR =
            new Creator<ParcelableHotlistSettings>() {
                public ParcelableHotlistSettings createFromParcel(Parcel in) {
                    HotlistSettings result =HotlistSettings.CREATOR.createFromParcel(in);;
                    return new ParcelableHotlistSettings(result);
                }

                public ParcelableHotlistSettings[] newArray(int size) {
                    return new ParcelableHotlistSettings[size];
                }
            };
}
