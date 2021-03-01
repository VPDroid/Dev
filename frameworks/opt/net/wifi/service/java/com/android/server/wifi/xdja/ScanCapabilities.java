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
public class ScanCapabilities implements Parcelable {
    public int  max_scan_cache_size;
    public int  max_scan_buckets;
    public int  max_ap_cache_per_scan;
    public int  max_rssi_sample_size;
    public int  max_scan_reporting_threshold;    
    public int  max_hotlist_bssids;
    public int  max_significant_wifi_change_aps;

    public ScanCapabilities() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(max_scan_cache_size);
        dest.writeInt(max_scan_buckets);
        dest.writeInt(max_ap_cache_per_scan);
        dest.writeInt(max_rssi_sample_size);
        dest.writeInt(max_scan_reporting_threshold);
        dest.writeInt(max_hotlist_bssids);
        dest.writeInt(max_significant_wifi_change_aps);
    }

    public void readFromParcel(Parcel in) {
        max_scan_cache_size = in.readInt();
        max_scan_buckets = in.readInt();
        max_ap_cache_per_scan = in.readInt();
        max_rssi_sample_size = in.readInt();
        max_scan_reporting_threshold = in.readInt();
        max_hotlist_bssids = in.readInt();
        max_significant_wifi_change_aps = in.readInt();
    }

    public static final Parcelable.Creator<ScanCapabilities> CREATOR =
            new Parcelable.Creator<ScanCapabilities>() {
        @Override
        public ScanCapabilities createFromParcel(Parcel in) {
            ScanCapabilities params = new ScanCapabilities();
            params.max_scan_cache_size = in.readInt();
            params.max_scan_buckets = in.readInt();
            params.max_ap_cache_per_scan = in.readInt();
            params.max_rssi_sample_size = in.readInt();
            params.max_scan_reporting_threshold = in.readInt();
            params.max_hotlist_bssids = in.readInt();
            params.max_significant_wifi_change_aps = in.readInt();
            return params;
        }
        @Override
        public ScanCapabilities[] newArray(int size) {
            return new ScanCapabilities[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = max_scan_cache_size;
        result = prime * result + max_scan_buckets;
        result = prime * result + max_ap_cache_per_scan;
        result = prime * result + max_rssi_sample_size;
        result = prime * result + max_scan_reporting_threshold;
        result = prime * result + max_hotlist_bssids;
        result = prime * result + max_significant_wifi_change_aps;
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
        ScanCapabilities other = (ScanCapabilities) obj;
        return (max_scan_cache_size == other.max_scan_cache_size) && 
               (max_scan_buckets == other.max_scan_buckets) &&
               (max_ap_cache_per_scan == other.max_ap_cache_per_scan) &&
               (max_rssi_sample_size == other.max_rssi_sample_size) && 
               (max_scan_reporting_threshold == other.max_scan_reporting_threshold) && 
               (max_hotlist_bssids == other.max_hotlist_bssids) && 
               (max_significant_wifi_change_aps == other.max_significant_wifi_change_aps);
    }

    @Override
    public String toString() {
        return "max_scan_cache_size: " + max_scan_cache_size + 
               " max_scan_buckets: " + max_scan_buckets + 
               " max_ap_cache_per_scan: " + max_ap_cache_per_scan +
               " max_rssi_sample_size: " + max_rssi_sample_size + 
               " max_scan_reporting_threshold: " + max_scan_reporting_threshold + 
               " max_hotlist_bssids: " + max_hotlist_bssids + 
               " max_significant_wifi_change_aps: " + max_significant_wifi_change_aps;
    }

}
