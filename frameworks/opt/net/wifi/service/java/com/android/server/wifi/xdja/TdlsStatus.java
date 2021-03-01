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


/**
 * Represents a LLCP packet received in a LLCP Connectionless communication;
 */
public class TdlsStatus implements Parcelable {
    public int channel;
    public int global_operating_class;
    public int state;
    public int reason;

    TdlsStatus() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(channel);
        dest.writeInt(global_operating_class);
        dest.writeInt(state);
        dest.writeInt(reason);
    }

    public static final Parcelable.Creator<TdlsStatus> CREATOR =
            new Parcelable.Creator<TdlsStatus>() {
        @Override
        public TdlsStatus createFromParcel(Parcel in) {
            TdlsStatus params = new TdlsStatus();
            params.channel = in.readInt();
            params.global_operating_class = in.readInt();
            params.state = in.readInt();
            params.reason = in.readInt();
            return params;
        }
        @Override
        public TdlsStatus[] newArray(int size) {
            return new TdlsStatus[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = channel;
        result = prime * result + global_operating_class;
        result = prime * result + state;
        result = prime * result + reason;
        return result;
    }

    /**
     * Returns true if the specified NDEF Message contains
     * identical NDEF Records.
     */
    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj == null) return false;
        if (getClass() != obj.getClass()) return false;
        TdlsStatus other = (TdlsStatus) obj;
        return (channel == other.channel) && 
               (global_operating_class == other.global_operating_class) &&
               (state == other.state) &&
               (reason == other.reason);
    }

    @Override
    public String toString() {
        return " channel: " + channel + 
               " global_operating_class: " + global_operating_class + 
               " state: " + state +
               " reason: " + reason;
    }

}
