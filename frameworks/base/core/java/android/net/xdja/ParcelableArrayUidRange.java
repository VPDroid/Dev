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

package android.net.xdja;

import android.os.Parcel;
import android.os.Parcelable;
import android.net.UidRange;

/**
 * Native methods for UidRange.
 *
 * {@hide}
 */
public class ParcelableArrayUidRange implements Parcelable {

    public UidRange mResults[];

    public ParcelableArrayUidRange(UidRange[] results) {
        mResults = results;
    }

    /*public UidRange[] getResults() {
        return mResults;
    }*/

    /** Implement the Parcelable interface {@hide} */
    public int describeContents() {
        return 0;
    }

    /** Implement the Parcelable interface {@hide} */
    public void writeToParcel(Parcel dest, int flags) {
        if (mResults != null) {
            dest.writeInt(mResults.length);
            for (int i = 0; i < mResults.length; i++) {
                UidRange result = mResults[i];
                dest.writeParcelable(result, flags);
            }
        } else {
            dest.writeInt(0);
        }
    }

    /** Implement the Parcelable interface {@hide} */
    public static final Creator<ParcelableArrayUidRange> CREATOR =
            new Creator<ParcelableArrayUidRange>() {
                public ParcelableArrayUidRange createFromParcel(Parcel in) {
                    int n = in.readInt();

                    if(n == 0)
                        return new ParcelableArrayUidRange(null);
                        
                    UidRange results[] = new UidRange[n];
                    for (int i = 0; i < n; i++) {
                        results[i] = UidRange.CREATOR.createFromParcel(in);
                    }
                    return new ParcelableArrayUidRange(results);
                }

                public ParcelableArrayUidRange[] newArray(int size) {
                    return new ParcelableArrayUidRange[size];
                }
            };
}
