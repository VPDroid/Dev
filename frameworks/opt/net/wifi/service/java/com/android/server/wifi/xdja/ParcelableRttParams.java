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

import android.net.wifi.RttManager.RttParams;

public class ParcelableRttParams implements Parcelable {

    public RttParams mParams[];

    public ParcelableRttParams(RttParams[] results) {
        mParams = results;
    }

    public RttParams[] getResults() {
        return mParams;
    }

    /** Implement the Parcelable interface {@hide} */
    public int describeContents() {
        return 0;
    }

    /** Implement the Parcelable interface {@hide} */
    public void writeToParcel(Parcel dest, int flags) {
        if (mParams != null) {
            dest.writeInt(mParams.length);

            for (RttParams params : mParams) {
                dest.writeInt(params.deviceType);
                dest.writeInt(params.requestType);
                dest.writeString(params.bssid);
                dest.writeInt(params.channelWidth);
                dest.writeInt(params.frequency);
                dest.writeInt(params.centerFreq0);
                dest.writeInt(params.centerFreq1);
                dest.writeInt(params.numberBurst);
                dest.writeInt(params.interval);
                dest.writeInt(params.numSamplesPerBurst);
                dest.writeInt(params.numRetriesPerMeasurementFrame);
                dest.writeInt(params.numRetriesPerFTMR);
                dest.writeInt(params.LCIRequest ? 1 : 0);
                dest.writeInt(params.LCRRequest ? 1 : 0);
                dest.writeInt(params.burstTimeout);
                dest.writeInt(params.preamble);
                dest.writeInt(params.bandwidth);
            }
        } else {
            dest.writeInt(0);
        }
    }

    /** Implement the Parcelable interface {@hide} */
    public static final Creator<ParcelableRttParams> CREATOR =
            new Creator<ParcelableRttParams>() {
                public ParcelableRttParams createFromParcel(Parcel in) {

                    int num = in.readInt();

                    if (num == 0) {
                        return new ParcelableRttParams(null);
                    }

                    RttParams params[] = new RttParams[num];
                    for (int i = 0; i < num; i++) {
                        params[i] = new RttParams();
                        params[i].deviceType = in.readInt();
                        params[i].requestType = in.readInt();
                        params[i].bssid = in.readString();
                        params[i].channelWidth = in.readInt();
                        params[i].frequency = in.readInt();
                        params[i].centerFreq0 = in.readInt();
                        params[i].centerFreq1 = in.readInt();
                        params[i].numberBurst = in.readInt();
                        params[i].interval = in.readInt();
                        params[i].numSamplesPerBurst = in.readInt();
                        params[i].numRetriesPerMeasurementFrame = in.readInt();
                        params[i].numRetriesPerFTMR = in.readInt();
                        params[i].LCIRequest = in.readInt() == 1 ? true : false;
                        params[i].LCRRequest = in.readInt() == 1 ? true : false;
                        params[i].burstTimeout = in.readInt();
                        params[i].preamble = in.readInt();
                        params[i].bandwidth = in.readInt();
                    }

                    ParcelableRttParams parcelableParams = new ParcelableRttParams(params);
                    return parcelableParams;
                }

                public ParcelableRttParams[] newArray(int size) {
                    return new ParcelableRttParams[size];
                }
            };
}
