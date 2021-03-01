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

import com.android.server.wifi.xdja.RingBufferStatus;

public class ParcelableRingBufferStatus implements Parcelable {

    public RingBufferStatus mResults[];

    public ParcelableRingBufferStatus(RingBufferStatus[] results) {
        mResults = results;
    }

    public RingBufferStatus[] getResults() {
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
                RingBufferStatus result = mResults[i];
                dest.writeParcelable(result, flags);
            }
        } else {
            dest.writeInt(0);
        }
    }

    /** Implement the Parcelable interface {@hide} */
    public static final Creator<ParcelableRingBufferStatus> CREATOR =
            new Creator<ParcelableRingBufferStatus>() {
                public ParcelableRingBufferStatus createFromParcel(Parcel in) {
                    int n = in.readInt();

                    if(n == 0)
                        return new ParcelableRingBufferStatus(null);
                        
                    RingBufferStatus results[] = new RingBufferStatus[n];
                    for (int i = 0; i < n; i++) {
                        results[i] = RingBufferStatus.CREATOR.createFromParcel(in);
                    }
                    return new ParcelableRingBufferStatus(results);
                }

                public ParcelableRingBufferStatus[] newArray(int size) {
                    return new ParcelableRingBufferStatus[size];
                }
            };
}
