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

import com.android.server.wifi.xdja.ChannelSettings;

public class BucketSettings implements Parcelable {
    public int bucket;
    public int band;
    public int period_ms;
    public int report_events;
    public int num_channels;
    public ChannelSettings channels[];

    public BucketSettings() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(bucket);
        dest.writeInt(band);
        dest.writeInt(period_ms);
        dest.writeInt(report_events);
        if(channels != null && channels.length > 0 && num_channels == channels.length){
            dest.writeInt(num_channels);
            dest.writeParcelableArray(channels,flags);
        }else{
            dest.writeInt(0);
        }
    }

    public static final Parcelable.Creator<BucketSettings> CREATOR =
            new Parcelable.Creator<BucketSettings>() {
        @Override
        public BucketSettings createFromParcel(Parcel in) {
            BucketSettings params = new BucketSettings();
            params.bucket = in.readInt();
            params.band = in.readInt();
            params.period_ms = in.readInt();
            params.report_events = in.readInt();
            params.num_channels = in.readInt();

            if(params.num_channels > 0){
                Parcelable[] tmp = in.readParcelableArray(ChannelSettings.class.getClassLoader());
                if(tmp != null){
                    params.channels = new ChannelSettings[tmp.length];
                    for (int i = 0; i < tmp.length; i++) {
                        params.channels[i] = (ChannelSettings) tmp[i];
                    } 
                }
            }         
            return params;
        }
        @Override
        public BucketSettings[] newArray(int size) {
            return new BucketSettings[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = bucket;
        result = prime * result + band;
        result = prime * result + period_ms;
        result = prime * result + report_events;
        result = prime * result + num_channels;

        if(channels != null){
            for (int i = 0; i < channels.length; i++) {
                result = prime * result + channels[i].hashCode();
            } 
        }
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
        BucketSettings other = (BucketSettings) obj;
        if(!((bucket == other.bucket) && 
               (band == other.band) &&
               (period_ms == other.period_ms) &&
               (report_events == other.report_events) &&
               (num_channels == other.num_channels))) return false;

        if(channels != null){
            for (int i = 0; i < channels.length; i++) {
                if(!channels[i].equals(other.channels[i]))
                        return false;
            }
        }
        return true;
    }

    @Override
    public String toString() {
        return " bucket: " + bucket + 
               " band: " + band + 
               " period_ms: " + period_ms +
               " report_events: " + report_events +
               " num_channels: " + num_channels ;
    }

}
