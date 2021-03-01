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
public class TdlsCapabilities implements Parcelable {
    public int maxConcurrentTdlsSessionNumber;
    public boolean isGlobalTdlsSupported;
    public boolean isPerMacTdlsSupported;
    public boolean isOffChannelTdlsSupported;

    TdlsCapabilities() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(maxConcurrentTdlsSessionNumber);
        dest.writeInt(isGlobalTdlsSupported?1:0);
        dest.writeInt(isPerMacTdlsSupported?1:0);
        dest.writeInt(isOffChannelTdlsSupported?1:0);
    }

    public static final Parcelable.Creator<TdlsCapabilities> CREATOR =
            new Parcelable.Creator<TdlsCapabilities>() {
        @Override
        public TdlsCapabilities createFromParcel(Parcel in) {
            TdlsCapabilities params = new TdlsCapabilities();
            params.maxConcurrentTdlsSessionNumber = in.readInt();
            params.isGlobalTdlsSupported = in.readInt()==1?true:false;
            params.isPerMacTdlsSupported = in.readInt()==1?true:false;
            params.isOffChannelTdlsSupported = in.readInt()==1?true:false;
            return params;
        }
        @Override
        public TdlsCapabilities[] newArray(int size) {
            return new TdlsCapabilities[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = maxConcurrentTdlsSessionNumber;
        result = prime * result + (isGlobalTdlsSupported?1:0);
        result = prime * result + (isPerMacTdlsSupported?1:0);
        result = prime * result + (isOffChannelTdlsSupported?1:0);
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
        TdlsCapabilities other = (TdlsCapabilities) obj;
        return (maxConcurrentTdlsSessionNumber == other.maxConcurrentTdlsSessionNumber) && 
               (isGlobalTdlsSupported == other.isGlobalTdlsSupported) &&
               (isPerMacTdlsSupported == other.isPerMacTdlsSupported) &&
               (isOffChannelTdlsSupported == other.isOffChannelTdlsSupported);
    }

    @Override
    public String toString() {
        return "maxConcurrentTdlsSessionNumber: " + maxConcurrentTdlsSessionNumber + 
               " isGlobalTdlsSupported: " + isGlobalTdlsSupported + 
               " isPerMacTdlsSupported: " + isPerMacTdlsSupported +
               " isOffChannelTdlsSupported: " + isOffChannelTdlsSupported;
    }

}
