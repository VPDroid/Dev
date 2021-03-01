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
public class RingBufferStatus implements Parcelable {
    public String name;
    public int flag;
    public int ringBufferId;
    public int ringBufferByteSize;
    public int verboseLevel;
    public int writtenBytes;
    public int readBytes;
    public int writtenRecords;

    RingBufferStatus() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(name);
        dest.writeInt(flag);
        dest.writeInt(ringBufferId);
        dest.writeInt(ringBufferByteSize);
        dest.writeInt(verboseLevel);
        dest.writeInt(writtenBytes);
        dest.writeInt(readBytes);
        dest.writeInt(writtenRecords);
    }

    public static final Parcelable.Creator<RingBufferStatus> CREATOR =
            new Parcelable.Creator<RingBufferStatus>() {
        @Override
        public RingBufferStatus createFromParcel(Parcel in) {
            RingBufferStatus params = new RingBufferStatus();
            params.name = in.readString();
            params.flag = in.readInt();
            params.ringBufferId = in.readInt();
            params.ringBufferByteSize = in.readInt();
            params.verboseLevel = in.readInt();
            params.writtenBytes = in.readInt();
            params.readBytes = in.readInt();
            params.writtenRecords = in.readInt();
            return params;
        }
        @Override
        public RingBufferStatus[] newArray(int size) {
            return new RingBufferStatus[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = name.hashCode();
        result = prime * result + flag;
        result = prime * result + ringBufferId;
        result = prime * result + ringBufferByteSize;
        result = prime * result + verboseLevel;
        result = prime * result + writtenBytes;
        result = prime * result + readBytes;
        result = prime * result + writtenRecords;
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
        RingBufferStatus other = (RingBufferStatus) obj;
        return (name.equals(other.name)) && 
               (flag == other.flag) &&
               (ringBufferId == other.ringBufferId) &&
               (ringBufferByteSize == other.ringBufferByteSize) &&
               (verboseLevel == other.verboseLevel) &&
               (writtenBytes == other.writtenBytes) &&
               (readBytes == other.readBytes) &&
               (writtenRecords == other.writtenRecords);
    }

    @Override
    public String toString() {
        return "name: " + name + " flag: " + flag + " ringBufferId: " + ringBufferId +
                " ringBufferByteSize: " +ringBufferByteSize + " verboseLevel: " +verboseLevel +
                " writtenBytes: " + writtenBytes + " readBytes: " + readBytes +
                " writtenRecords: " + writtenRecords;
    }

}
