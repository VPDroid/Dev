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

import com.android.server.wifi.xdja.BucketSettings;

/**
 * Represents a LLCP packet received in a LLCP Connectionless communication;
 */
public class ScanSettings implements Parcelable {
    public int base_period_ms;
    public int max_ap_per_scan;
    public int report_threshold_percent;
    public int report_threshold_num_scans;
    public int num_buckets;
    public BucketSettings buckets[];

    public ScanSettings() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(base_period_ms);
        dest.writeInt(max_ap_per_scan);
        dest.writeInt(report_threshold_percent);
        dest.writeInt(report_threshold_num_scans);
        if(buckets!= null && buckets.length > 0 && buckets.length == num_buckets){
            dest.writeInt(num_buckets);
            dest.writeParcelableArray(buckets,flags);
        }else
            dest.writeInt(0);
    }

    public static final Parcelable.Creator<ScanSettings> CREATOR =
            new Parcelable.Creator<ScanSettings>() {
        @Override
        public ScanSettings createFromParcel(Parcel in) {
            ScanSettings params = new ScanSettings();
            params.base_period_ms = in.readInt();
            params.max_ap_per_scan = in.readInt();
            params.report_threshold_percent = in.readInt();
            params.report_threshold_num_scans = in.readInt();
            params.num_buckets = in.readInt();

            if(params.num_buckets > 0){
                Parcelable[] tmp = in.readParcelableArray(BucketSettings.class.getClassLoader());
                if(tmp != null){
                    params.buckets = new BucketSettings[tmp.length];
                    for (int i = 0; i < tmp.length; i++) {
                        params.buckets[i] = (BucketSettings) tmp[i];
                    }
                }
            }
            return params;
        }
        @Override
        public ScanSettings[] newArray(int size) {
            return new ScanSettings[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = base_period_ms;
        result = prime * result + max_ap_per_scan;
        result = prime * result + report_threshold_percent;
        result = prime * result + report_threshold_num_scans;
        result = prime * result + num_buckets;

        if(buckets != null){
            for (int i = 0; i < buckets.length; i++) {
                result = prime * result + buckets[i].hashCode();
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
        ScanSettings other = (ScanSettings) obj;
        if(!((base_period_ms == other.base_period_ms) && 
          (max_ap_per_scan == other.max_ap_per_scan) &&
          (report_threshold_percent == other.report_threshold_percent) &&
          (report_threshold_num_scans == other.report_threshold_num_scans) &&
          (num_buckets == other.num_buckets))) return false;

        if(buckets != null){
            for (int i = 0; i < buckets.length; i++) {
                if(!buckets[i].equals(other.buckets[i]))
                    return false;
            }
        }

        return true;
    }

    @Override
    public String toString() {
        return " base_period_ms: " + base_period_ms + 
               " max_ap_per_scan: " + max_ap_per_scan + 
               " report_threshold_percent: " + report_threshold_percent +
               " report_threshold_num_scans: " + report_threshold_num_scans +
               " num_buckets: " + num_buckets ;
    }

}
