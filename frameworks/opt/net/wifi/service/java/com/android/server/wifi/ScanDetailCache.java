
package com.android.server.wifi;

import android.net.wifi.ScanResult;
import android.net.wifi.WifiConfiguration;
import android.util.Log;
import android.os.SystemClock;

import com.android.server.wifi.ScanDetail;
import com.android.server.wifi.hotspot2.PasspointMatch;
import com.android.server.wifi.hotspot2.PasspointMatchInfo;
import com.android.server.wifi.hotspot2.pps.HomeSP;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

class ScanDetailCache {

    private static final String TAG = "ScanDetailCache";

    private WifiConfiguration mConfig;
    private HashMap<String, ScanDetail> mMap;
    private HashMap<String, PasspointMatchInfo> mPasspointMatches;
    private static boolean DBG=false;
    ScanDetailCache(WifiConfiguration config) {
        mConfig = config;
        mMap = new HashMap();
        mPasspointMatches = new HashMap();
    }

    void put(ScanDetail scanDetail) {
        put(scanDetail, null, null);
    }

    void put(ScanDetail scanDetail, PasspointMatch match, HomeSP homeSp) {

        mMap.put(scanDetail.getBSSIDString(), scanDetail);

        if (match != null && homeSp != null) {
            mPasspointMatches.put(scanDetail.getBSSIDString(),
                    new PasspointMatchInfo(match, scanDetail, homeSp));
        }
    }

    ScanResult get(String bssid) {
        ScanDetail scanDetail = getScanDetail(bssid);
        return scanDetail == null ? null : scanDetail.getScanResult();
    }

    ScanDetail getScanDetail(String bssid) {
        return mMap.get(bssid);
    }

    void remove(String bssid) {
        mMap.remove(bssid);
    }

    int size() {
        return mMap.size();
    }

    boolean isEmpty() {
        return size() == 0;
    }

    ScanDetail getFirst() {
        Iterator<ScanDetail> it = mMap.values().iterator();
        return it.hasNext() ? it.next() : null;
    }

    Collection<String> keySet() {
        return mMap.keySet();
    }

    Collection<ScanDetail> values() {
        return mMap.values();
    }

    public void trim(int num) {
        int currentSize = mMap.size();
        if (currentSize <= num) {
            return; // Nothing to trim
        }
        ArrayList<ScanDetail> list = new ArrayList<ScanDetail>(mMap.values());
        if (list.size() != 0) {
            // Sort by descending timestamp
            Collections.sort(list, new Comparator() {
                public int compare(Object o1, Object o2) {
                    ScanDetail a = (ScanDetail) o1;
                    ScanDetail b = (ScanDetail) o2;
                    if (a.getSeen() > b.getSeen()) {
                        return 1;
                    }
                    if (a.getSeen() < b.getSeen()) {
                        return -1;
                    }
                    return a.getBSSIDString().compareTo(b.getBSSIDString());
                }
            });
        }
        for (int i = 0; i < currentSize - num ; i++) {
            // Remove oldest results from scan cache
            ScanDetail result = list.get(i);
            mMap.remove(result.getBSSIDString());
            mPasspointMatches.remove(result.getBSSIDString());
        }
    }

    /* @hide */
    private ArrayList<ScanDetail> sort() {
        ArrayList<ScanDetail> list = new ArrayList<ScanDetail>(mMap.values());
        if (list.size() != 0) {
            Collections.sort(list, new Comparator() {
                public int compare(Object o1, Object o2) {
                    ScanResult a = ((ScanDetail)o1).getScanResult();
                    ScanResult b = ((ScanDetail)o2).getScanResult();
                    if (a.numIpConfigFailures > b.numIpConfigFailures) {
                        return 1;
                    }
                    if (a.numIpConfigFailures < b.numIpConfigFailures) {
                        return -1;
                    }
                    if (a.seen > b.seen) {
                        return -1;
                    }
                    if (a.seen < b.seen) {
                        return 1;
                    }
                    if (a.level > b.level) {
                        return -1;
                    }
                    if (a.level < b.level) {
                        return 1;
                    }
                    return a.BSSID.compareTo(b.BSSID);
                }
            });
        }
        return list;
    }

    public WifiConfiguration.Visibility getVisibilityByRssi(long age) {
        WifiConfiguration.Visibility status = new WifiConfiguration.Visibility();

        long now_ms = System.currentTimeMillis();
        long now_elapsed_ms = SystemClock.elapsedRealtime();
        for(ScanDetail scanDetail : values()) {
            ScanResult result = scanDetail.getScanResult();
            if (scanDetail.getSeen() == 0)
                continue;

            if (result.is5GHz()) {
                //strictly speaking: [4915, 5825]
                //number of known BSSID on 5GHz band
                status.num5 = status.num5 + 1;
            } else if (result.is24GHz()) {
                //strictly speaking: [2412, 2482]
                //number of known BSSID on 2.4Ghz band
                status.num24 = status.num24 + 1;
            }

            if (result.timestamp != 0) {
                if (DBG) {
                    Log.e("getVisibilityByRssi", " considering " + result.SSID + " " + result.BSSID
                        + " elapsed=" + now_elapsed_ms + " timestamp=" + result.timestamp
                        + " age = " + age);
                }
                if ((now_elapsed_ms - (result.timestamp/1000)) > age) continue;
            } else {
                // This check the time at which we have received the scan result from supplicant
                if ((now_ms - result.seen) > age) continue;
            }

            if (result.is5GHz()) {
                if (result.level > status.rssi5) {
                    status.rssi5 = result.level;
                    status.age5 = result.seen;
                    status.BSSID5 = result.BSSID;
                }
            } else if (result.is24GHz()) {
                if (result.level > status.rssi24) {
                    status.rssi24 = result.level;
                    status.age24 = result.seen;
                    status.BSSID24 = result.BSSID;
                }
            }
        }

        return status;
    }

    public WifiConfiguration.Visibility getVisibilityByPasspointMatch(long age) {

        long now_ms = System.currentTimeMillis();
        PasspointMatchInfo pmiBest24 = null, pmiBest5 = null;

        for(PasspointMatchInfo pmi : mPasspointMatches.values()) {
            ScanDetail scanDetail = pmi.getScanDetail();
            if (scanDetail == null) continue;
            ScanResult result = scanDetail.getScanResult();
            if (result == null) continue;

            if (scanDetail.getSeen() == 0)
                continue;

            if ((now_ms - result.seen) > age) continue;

            if (result.is5GHz()) {
                if (pmiBest5 == null || pmiBest5.compareTo(pmi) < 0) {
                    pmiBest5 = pmi;
                }
            } else if (result.is24GHz()) {
                if (pmiBest24 == null || pmiBest24.compareTo(pmi) < 0) {
                    pmiBest24 = pmi;
                }
            }
        }

        WifiConfiguration.Visibility status = new WifiConfiguration.Visibility();
        String logMsg = "Visiblity by passpoint match returned ";
        if (pmiBest5 != null) {
            ScanResult result = pmiBest5.getScanDetail().getScanResult();
            status.rssi5 = result.level;
            status.age5 = result.seen;
            status.BSSID5 = result.BSSID;
            logMsg += "5 GHz BSSID of " + result.BSSID;
        }
        if (pmiBest24 != null) {
            ScanResult result = pmiBest24.getScanDetail().getScanResult();
            status.rssi24 = result.level;
            status.age24 = result.seen;
            status.BSSID24 = result.BSSID;
            logMsg += "2.4 GHz BSSID of " + result.BSSID;
        }

        Log.d(TAG, logMsg);

        return status;
    }

    public WifiConfiguration.Visibility getVisibility(long age) {
        if (mConfig.isPasspoint()) {
            return getVisibilityByPasspointMatch(age);
        } else {
            return getVisibilityByRssi(age);
        }
    }



    @Override
    public String toString() {
        StringBuilder sbuf = new StringBuilder();
        sbuf.append("Scan Cache:  ").append('\n');

        ArrayList<ScanDetail> list = sort();
        long now_ms = System.currentTimeMillis();
        if (list.size() > 0) {
            for (ScanDetail scanDetail : list) {
                ScanResult result = scanDetail.getScanResult();
                long milli = now_ms - scanDetail.getSeen();
                long ageSec = 0;
                long ageMin = 0;
                long ageHour = 0;
                long ageMilli = 0;
                long ageDay = 0;
                if (now_ms > scanDetail.getSeen() && scanDetail.getSeen() > 0) {
                    ageMilli = milli % 1000;
                    ageSec   = (milli / 1000) % 60;
                    ageMin   = (milli / (60*1000)) % 60;
                    ageHour  = (milli / (60*60*1000)) % 24;
                    ageDay   = (milli / (24*60*60*1000));
                }
                sbuf.append("{").append(result.BSSID).append(",").append(result.frequency);
                sbuf.append(",").append(String.format("%3d", result.level));
                if (result.autoJoinStatus > 0) {
                    sbuf.append(",st=").append(result.autoJoinStatus);
                }
                if (ageSec > 0 || ageMilli > 0) {
                    sbuf.append(String.format(",%4d.%02d.%02d.%02d.%03dms", ageDay,
                            ageHour, ageMin, ageSec, ageMilli));
                }
                if (result.numIpConfigFailures > 0) {
                    sbuf.append(",ipfail=");
                    sbuf.append(result.numIpConfigFailures);
                }
                sbuf.append("} ");
            }
            sbuf.append('\n');
        }

        return sbuf.toString();
    }

}
