/*
 * Copyright 2014, The Android Open Source Project
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

/**
 * Helper class to adapt a simple String to cases where a Parcelable is expected.
 * @hide
 */
public class ParcelableInt implements Parcelable {
    public int v;

    public ParcelableInt(int v){
        this.v = v;
    }

    public int getResult() {
        return v;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeInt(v);
    }

    public static final Parcelable.Creator<ParcelableInt> CREATOR =
            new Parcelable.Creator<ParcelableInt>() {
                @Override
                public ParcelableInt createFromParcel(Parcel in) {
                    return new ParcelableInt(in.readInt());
                }
                @Override
                public ParcelableInt[] newArray(int size) {
                    return new ParcelableInt[size];
                }
            };
}