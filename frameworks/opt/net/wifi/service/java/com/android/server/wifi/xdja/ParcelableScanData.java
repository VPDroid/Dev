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

import android.net.wifi.WifiScanner.ScanData;

public class ParcelableScanData implements Parcelable {

    public ScanData mResults[];

    public ParcelableScanData(ScanData[] results) {
        mResults = results;
    }

    public ScanData[] getResults() {
        return mResults;
    }

    /** Implement the Parcelable interface {@hide} */
    public int describeContents() {
        return 0;
    }

    /** Implement the Parcelable interface {@hide} */
    public void writeToParcel(Parcel dest, int flags) {
        if (mResults != null) {
            dest.writeInt(mResults.length);
            for (int i = 0; i < mResults.length; i++) {
                ScanData result = mResults[i];
                dest.writeParcelable(result, flags);
            }
        } else {
            dest.writeInt(0);
        }
    }

    /** Implement the Parcelable interface {@hide} */
    public static final Creator<ParcelableScanData> CREATOR =
            new Creator<ParcelableScanData>() {
                public ParcelableScanData createFromParcel(Parcel in) {
                    int n = in.readInt();

                    if(n == 0)
                        return new ParcelableScanData(null);
                        
                    ScanData results[] = new ScanData[n];
                    for (int i = 0; i < n; i++) {
                        results[i] = ScanData.CREATOR.createFromParcel(in);
                    }
                    return new ParcelableScanData(results);
                }

                public ParcelableScanData[] newArray(int size) {
                    return new ParcelableScanData[size];
                }
            };
}
