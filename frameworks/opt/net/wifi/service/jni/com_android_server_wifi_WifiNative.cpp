/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "wifi"

#include "jni.h"
#include "JniConstants.h"
#include <ScopedUtfChars.h>
#include <ScopedBytes.h>
#include <utils/misc.h>
#include <android_runtime/AndroidRuntime.h>
#include <utils/Log.h>
#include <utils/String16.h>
#include <ctype.h>
#include <sys/socket.h>
#include <linux/if.h>
#include "wifi.h"
#include "wifi_hal.h"
#include "jni_helper.h"
#include "rtt.h"
#include "wifi_hal_stub.h"
#define REPLY_BUF_SIZE 4096 + 1         // wpa_supplicant's maximum size + 1 for nul
#define EVENT_BUF_SIZE 2048

namespace android {

static jint DBG = false;

//Please put all HAL function call here and call from the function table instead of directly call
static wifi_hal_fn hal_fn;
int init_wifi_hal_func_table(wifi_hal_fn *hal_fn) {
    if (hal_fn == NULL) {
        return -1;
    }
    hal_fn->wifi_initialize = wifi_initialize_stub;
    hal_fn->wifi_cleanup = wifi_cleanup_stub;
    hal_fn->wifi_event_loop = wifi_event_loop_stub;
    hal_fn->wifi_get_error_info = wifi_get_error_info_stub;
    hal_fn->wifi_get_supported_feature_set = wifi_get_supported_feature_set_stub;
    hal_fn->wifi_get_concurrency_matrix = wifi_get_concurrency_matrix_stub;
    hal_fn->wifi_set_scanning_mac_oui =  wifi_set_scanning_mac_oui_stub;
    hal_fn->wifi_get_supported_channels = wifi_get_supported_channels_stub;
    hal_fn->wifi_is_epr_supported = wifi_is_epr_supported_stub;
    hal_fn->wifi_get_ifaces = wifi_get_ifaces_stub;
    hal_fn->wifi_get_iface_name = wifi_get_iface_name_stub;
    hal_fn->wifi_reset_iface_event_handler = wifi_reset_iface_event_handler_stub;
    hal_fn->wifi_start_gscan = wifi_start_gscan_stub;
    hal_fn->wifi_stop_gscan = wifi_stop_gscan_stub;
    hal_fn->wifi_get_cached_gscan_results = wifi_get_cached_gscan_results_stub;
    hal_fn->wifi_set_bssid_hotlist = wifi_set_bssid_hotlist_stub;
    hal_fn->wifi_reset_bssid_hotlist = wifi_reset_bssid_hotlist_stub;
    hal_fn->wifi_set_significant_change_handler = wifi_set_significant_change_handler_stub;
    hal_fn->wifi_reset_significant_change_handler = wifi_reset_significant_change_handler_stub;
    hal_fn->wifi_get_gscan_capabilities = wifi_get_gscan_capabilities_stub;
    hal_fn->wifi_set_link_stats = wifi_set_link_stats_stub;
    hal_fn->wifi_get_link_stats = wifi_get_link_stats_stub;
    hal_fn->wifi_clear_link_stats = wifi_clear_link_stats_stub;
    hal_fn->wifi_get_valid_channels = wifi_get_valid_channels_stub;
    hal_fn->wifi_rtt_range_request = wifi_rtt_range_request_stub;
    hal_fn->wifi_rtt_range_cancel = wifi_rtt_range_cancel_stub;
    hal_fn->wifi_get_rtt_capabilities = wifi_get_rtt_capabilities_stub;
    hal_fn->wifi_start_logging = wifi_start_logging_stub;
    hal_fn->wifi_set_epno_list = wifi_set_epno_list_stub;
    hal_fn->wifi_set_country_code = wifi_set_country_code_stub;
    hal_fn->wifi_enable_tdls = wifi_enable_tdls_stub;
    hal_fn->wifi_disable_tdls = wifi_disable_tdls_stub;
    hal_fn->wifi_get_tdls_status = wifi_get_tdls_status_stub;
    hal_fn->wifi_get_tdls_capabilities = wifi_get_tdls_capabilities_stub;
    hal_fn->wifi_set_nodfs_flag = wifi_set_nodfs_flag_stub;
    hal_fn->wifi_get_firmware_memory_dump = wifi_get_firmware_memory_dump_stub;
    hal_fn->wifi_set_log_handler = wifi_set_log_handler_stub;
    hal_fn->wifi_reset_log_handler = wifi_reset_log_handler_stub;
    hal_fn->wifi_set_alert_handler = wifi_set_alert_handler_stub;
    hal_fn->wifi_reset_alert_handler = wifi_reset_alert_handler_stub;
    hal_fn->wifi_get_firmware_version = wifi_get_firmware_version_stub;
    hal_fn->wifi_get_ring_buffers_status = wifi_get_ring_buffers_status_stub;
    hal_fn->wifi_get_logger_supported_feature_set = wifi_get_logger_supported_feature_set_stub;
    hal_fn->wifi_get_ring_data = wifi_get_ring_data_stub;
    hal_fn->wifi_get_driver_version = wifi_get_driver_version_stub;
    hal_fn->wifi_set_ssid_white_list = wifi_set_ssid_white_list_stub;
    hal_fn->wifi_set_gscan_roam_params = wifi_set_gscan_roam_params_stub;
    hal_fn->wifi_set_bssid_preference = wifi_set_bssid_preference_stub;
    hal_fn->wifi_enable_lazy_roam = wifi_enable_lazy_roam_stub;
    hal_fn->wifi_set_bssid_blacklist = wifi_set_bssid_blacklist_stub;
    hal_fn->wifi_start_sending_offloaded_packet = wifi_start_sending_offloaded_packet_stub;
    hal_fn->wifi_stop_sending_offloaded_packet = wifi_stop_sending_offloaded_packet_stub;
    return 0;
}

static bool doCommand(JNIEnv* env, jstring javaCommand,
                      char* reply, size_t reply_len) {
    ScopedUtfChars command(env, javaCommand);
    if (command.c_str() == NULL) {
        return false; // ScopedUtfChars already threw on error.
    }

    if (DBG) {
        ALOGD("doCommand: %s", command.c_str());
    }

    --reply_len; // Ensure we have room to add NUL termination.
    if (::wifi_command(command.c_str(), reply, &reply_len) != 0) {
        return false;
    }

    // Strip off trailing newline.
    if (reply_len > 0 && reply[reply_len-1] == '\n') {
        reply[reply_len-1] = '\0';
    } else {
        reply[reply_len] = '\0';
    }
    return true;
}

static jint doIntCommand(JNIEnv* env, jstring javaCommand) {
    char reply[REPLY_BUF_SIZE];
    if (!doCommand(env, javaCommand, reply, sizeof(reply))) {
        return -1;
    }
    return static_cast<jint>(atoi(reply));
}

static jboolean doBooleanCommand(JNIEnv* env, jstring javaCommand) {
    char reply[REPLY_BUF_SIZE];
    if (!doCommand(env, javaCommand, reply, sizeof(reply))) {
        return JNI_FALSE;
    }
    jboolean result = (strcmp(reply, "OK") == 0);
    if (!result) {
        ScopedUtfChars command(env, javaCommand);
        ALOGI("command '%s' returned '%s", command.c_str(), reply);
    }
    return result;
}

// Send a command to the supplicant, and return the reply as a String.
static jstring doStringCommand(JNIEnv* env, jstring javaCommand) {
    char reply[REPLY_BUF_SIZE];
    if (!doCommand(env, javaCommand, reply, sizeof(reply))) {
        return NULL;
    }
    return env->NewStringUTF(reply);
}

static jboolean android_net_wifi_isDriverLoaded(JNIEnv* env, jobject)
{
    return (::is_wifi_driver_loaded() == 1);
}

static jboolean android_net_wifi_loadDriver(JNIEnv* env, jobject)
{
    return (::wifi_load_driver() == 0);
}

static jboolean android_net_wifi_unloadDriver(JNIEnv* env, jobject)
{
    return (::wifi_unload_driver() == 0);
}

static jboolean android_net_wifi_startSupplicant(JNIEnv* env, jobject, jboolean p2pSupported)
{
    return (::wifi_start_supplicant(p2pSupported) == 0);
}

static jboolean android_net_wifi_killSupplicant(JNIEnv* env, jobject, jboolean p2pSupported)
{
    return (::wifi_stop_supplicant(p2pSupported) == 0);
}

static jboolean android_net_wifi_connectToSupplicant(JNIEnv* env, jobject)
{
    return (::wifi_connect_to_supplicant() == 0);
}

static void android_net_wifi_closeSupplicantConnection(JNIEnv* env, jobject)
{
    ::wifi_close_supplicant_connection();
}

static jstring android_net_wifi_waitForEvent(JNIEnv* env, jobject)
{
    char buf[EVENT_BUF_SIZE];
    int nread = ::wifi_wait_for_event(buf, sizeof buf);
    if (nread > 0) {
        return env->NewStringUTF(buf);
    } else {
        return NULL;
    }
}

static jboolean android_net_wifi_doBooleanCommand(JNIEnv* env, jobject, jstring javaCommand) {
    return doBooleanCommand(env, javaCommand);
}

static jint android_net_wifi_doIntCommand(JNIEnv* env, jobject, jstring javaCommand) {
    return doIntCommand(env, javaCommand);
}

static jstring android_net_wifi_doStringCommand(JNIEnv* env, jobject, jstring javaCommand) {
    return doStringCommand(env,javaCommand);
}

/* wifi_hal <==> WifiNative bridge */

static jclass mCls;                             /* saved WifiNative object */
static JavaVM *mVM;                             /* saved JVM pointer */

static const char *WifiHandleVarName = "sWifiHalHandle";
static const char *WifiIfaceHandleVarName = "sWifiIfaceHandles";
static jmethodID OnScanResultsMethodID;

static wifi_handle getWifiHandle(JNIHelper &helper, jclass cls) {
    return (wifi_handle) helper.getStaticLongField(cls, WifiHandleVarName);
}

static wifi_interface_handle getIfaceHandle(JNIHelper &helper, jclass cls, jint index) {
    return (wifi_interface_handle) helper.getStaticLongArrayField(cls, WifiIfaceHandleVarName, index);
}

jboolean setSSIDField(JNIHelper helper, jobject scanResult, const char *rawSsid) {

    int len = strlen(rawSsid);

    if (len > 0) {
        JNIObject<jbyteArray> ssidBytes = helper.newByteArray(len);
        helper.setByteArrayRegion(ssidBytes, 0, len, (jbyte *) rawSsid);
        jboolean ret = helper.callStaticMethod(mCls,
                "setSsid", "([BLandroid/net/wifi/ScanResult;)Z", ssidBytes.get(), scanResult);
        return ret;
    } else {
        //empty SSID or SSID start with \0
        return true;
    }
}
static JNIObject<jobject> createScanResult(JNIHelper &helper, wifi_scan_result *result) {

    // ALOGD("creating scan result");

    JNIObject<jobject> scanResult = helper.createObject("android/net/wifi/ScanResult");
    if (scanResult == NULL) {
        ALOGE("Error in creating scan result");
        return JNIObject<jobject>(helper, NULL);
    }

    ALOGV("setting SSID to %s", result->ssid);

    if (!setSSIDField(helper, scanResult, result->ssid)) {
        ALOGE("Error on set SSID");
        return JNIObject<jobject>(helper, NULL);
    }

    char bssid[32];
    sprintf(bssid, "%02x:%02x:%02x:%02x:%02x:%02x", result->bssid[0], result->bssid[1],
        result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5]);

    helper.setStringField(scanResult, "BSSID", bssid);

    helper.setIntField(scanResult, "level", result->rssi);
    helper.setIntField(scanResult, "frequency", result->channel);
    helper.setLongField(scanResult, "timestamp", result->ts);

    return scanResult;
}

int set_iface_flags(const char *ifname, int dev_up) {
    struct ifreq ifr;
    int ret;
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ALOGD("Bad socket: %d\n", sock);
        return -errno;
    }

    //ALOGD("setting interface %s flags (%s)\n", ifname, dev_up ? "UP" : "DOWN");

    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);

    //ALOGD("reading old value\n");

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0) {
      ret = errno ? -errno : -999;
      ALOGE("Could not read interface %s flags: %d\n", ifname, errno);
      close(sock);
      return ret;
    } else {
      //ALOGD("writing new value\n");
    }

    if (dev_up) {
      if (ifr.ifr_flags & IFF_UP) {
        // ALOGD("interface %s is already up\n", ifname);
        close(sock);
        return 0;
      }
      ifr.ifr_flags |= IFF_UP;
    } else {
      if (!(ifr.ifr_flags & IFF_UP)) {
        // ALOGD("interface %s is already down\n", ifname);
        close(sock);
        return 0;
      }
      ifr.ifr_flags &= ~IFF_UP;
    }

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) != 0) {
      ALOGE("Could not set interface %s flags: %d\n", ifname, errno);
      ret = errno ? -errno : -999;
      close(sock);
      return ret;
    } else {
      ALOGD("set interface %s flags (%s)\n", ifname, dev_up ? "UP" : "DOWN");
    }
    close(sock);
    return 0;
}

static jboolean android_net_wifi_toggle_interface(JNIEnv* env, jclass cls, int toggle) {
    return(set_iface_flags("wlan0", toggle) == 0);
}

static jboolean android_net_wifi_startHal(JNIEnv* env, jclass cls) {
    JNIHelper helper(env);
    wifi_handle halHandle = getWifiHandle(helper, cls);
    if (halHandle == NULL) {

        if(init_wifi_hal_func_table(&hal_fn) != 0 ) {
            ALOGD("Can not initialize the basic function pointer table");
            return false;
        }

        wifi_error res = init_wifi_vendor_hal_func_table(&hal_fn);
        if (res != WIFI_SUCCESS) {
            ALOGD("Can not initialize the vendor function pointer table");
	    return false;
        }

        int ret = set_iface_flags("wlan0", 1);
        if(ret != 0) {
            return false;
        }

        res = hal_fn.wifi_initialize(&halHandle);
        if (res == WIFI_SUCCESS) {
            helper.setStaticLongField(cls, WifiHandleVarName, (jlong)halHandle);
            ALOGD("Did set static halHandle = %p", halHandle);
        }
        env->GetJavaVM(&mVM);
        mCls = (jclass) env->NewGlobalRef(cls);
        ALOGD("halHandle = %p, mVM = %p, mCls = %p", halHandle, mVM, mCls);
        return res == WIFI_SUCCESS;
    } else {
        return (set_iface_flags("wlan0", 1) == 0);
    }
}

void android_net_wifi_hal_cleaned_up_handler(wifi_handle handle) {
    ALOGD("In wifi cleaned up handler");

    JNIHelper helper(mVM);
    helper.setStaticLongField(mCls, WifiHandleVarName, 0);

    helper.deleteGlobalRef(mCls);
    mCls = NULL;
    mVM  = NULL;
}

static void android_net_wifi_stopHal(JNIEnv* env, jclass cls) {
    ALOGD("In wifi stop Hal");

    JNIHelper helper(env);
    wifi_handle halHandle = getWifiHandle(helper, cls);
    if (halHandle == NULL)
        return;

    ALOGD("halHandle = %p, mVM = %p, mCls = %p", halHandle, mVM, mCls);
    hal_fn.wifi_cleanup(halHandle, android_net_wifi_hal_cleaned_up_handler);
}

static void android_net_wifi_waitForHalEvents(JNIEnv* env, jclass cls) {

    ALOGD("waitForHalEvents called, vm = %p, obj = %p, env = %p", mVM, mCls, env);

    JNIHelper helper(env);
    wifi_handle halHandle = getWifiHandle(helper, cls);
    hal_fn.wifi_event_loop(halHandle);
    set_iface_flags("wlan0", 0);
}

static int android_net_wifi_getInterfaces(JNIEnv *env, jclass cls) {
    int n = 0;

    JNIHelper helper(env);

    wifi_handle halHandle = getWifiHandle(helper, cls);
    wifi_interface_handle *ifaceHandles = NULL;
    int result = hal_fn.wifi_get_ifaces(halHandle, &n, &ifaceHandles);
    if (result < 0) {
        return result;
    }

    if (n < 0) {
        THROW(helper,"android_net_wifi_getInterfaces no interfaces");
        return 0;
    }

    if (ifaceHandles == NULL) {
       THROW(helper,"android_net_wifi_getInterfaces null interface array");
       return 0;
    }

    if (n > 8) {
        THROW(helper,"Too many interfaces");
        return 0;
    }

    jlongArray array = (env)->NewLongArray(n);
    if (array == NULL) {
        THROW(helper,"Error in accessing array");
        return 0;
    }

    jlong elems[8];
    for (int i = 0; i < n; i++) {
        elems[i] = reinterpret_cast<jlong>(ifaceHandles[i]);
    }

    helper.setLongArrayRegion(array, 0, n, elems);
    helper.setStaticLongArrayField(cls, WifiIfaceHandleVarName, array);

    return (result < 0) ? result : n;
}

static jstring android_net_wifi_getInterfaceName(JNIEnv *env, jclass cls, jint i) {

    char buf[EVENT_BUF_SIZE];

    JNIHelper helper(env);

    jlong value = helper.getStaticLongArrayField(cls, WifiIfaceHandleVarName, i);
    wifi_interface_handle handle = (wifi_interface_handle) value;
    int result = hal_fn.wifi_get_iface_name(handle, buf, sizeof(buf));
    if (result < 0) {
        return NULL;
    } else {
        JNIObject<jstring> name = helper.newStringUTF(buf);
        return name.detach();
    }
}


static void onScanResultsAvailable(wifi_request_id id, unsigned num_results) {

    JNIHelper helper(mVM);

    // ALOGD("onScanResultsAvailable called, vm = %p, obj = %p, env = %p", mVM, mCls, env);

    helper.reportEvent(mCls, "onScanResultsAvailable", "(I)V", id);
}

static void onScanEvent(wifi_scan_event event, unsigned status) {

    JNIHelper helper(mVM);

    // ALOGD("onScanStatus called, vm = %p, obj = %p, env = %p", mVM, mCls, env);

    helper.reportEvent(mCls, "onScanStatus", "(I)V", event);
}

static void onFullScanResult(wifi_request_id id, wifi_scan_result *result) {

    JNIHelper helper(mVM);

    //ALOGD("onFullScanResult called, vm = %p, obj = %p, env = %p", mVM, mCls, env);

    JNIObject<jobject> scanResult = createScanResult(helper, result);

    //ALOGD("Creating a byte array of length %d", result->ie_length);

    JNIObject<jbyteArray> elements = helper.newByteArray(result->ie_length);
    if (elements == NULL) {
        ALOGE("Error in allocating array");
        return;
    }

    // ALOGD("Setting byte array");

    jbyte *bytes = (jbyte *)&(result->ie_data[0]);
    helper.setByteArrayRegion(elements, 0, result->ie_length, bytes);

    // ALOGD("Returning result");

    helper.reportEvent(mCls, "onFullScanResult", "(ILandroid/net/wifi/ScanResult;[B)V", id,
            scanResult.get(), elements.get());
}

static jboolean android_net_wifi_startScan(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject settings) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("starting scan on interface[%d] = %p", iface, handle);

    wifi_scan_cmd_params params;
    memset(&params, 0, sizeof(params));

    params.base_period = helper.getIntField(settings, "base_period_ms");
    params.max_ap_per_scan = helper.getIntField(settings, "max_ap_per_scan");
    params.report_threshold_percent = helper.getIntField(settings, "report_threshold_percent");
    params.report_threshold_num_scans = helper.getIntField(settings, "report_threshold_num_scans");

    ALOGD("Initialized common fields %d, %d, %d, %d", params.base_period, params.max_ap_per_scan,
            params.report_threshold_percent, params.report_threshold_num_scans);

    const char *bucket_array_type = "[Lcom/android/server/wifi/WifiNative$BucketSettings;";
    const char *channel_array_type = "[Lcom/android/server/wifi/WifiNative$ChannelSettings;";

    params.num_buckets = helper.getIntField(settings, "num_buckets");

    // ALOGD("Initialized num_buckets to %d", params.num_buckets);

    for (int i = 0; i < params.num_buckets; i++) {
        JNIObject<jobject> bucket = helper.getObjectArrayField(
                settings, "buckets", bucket_array_type, i);

        params.buckets[i].bucket = helper.getIntField(bucket, "bucket");
        params.buckets[i].band = (wifi_band) helper.getIntField(bucket, "band");
        params.buckets[i].period = helper.getIntField(bucket, "period_ms");

        int report_events = helper.getIntField(bucket, "report_events");
        params.buckets[i].report_events = report_events;

        ALOGD("bucket[%d] = %d:%d:%d:%d", i, params.buckets[i].bucket,
                params.buckets[i].band, params.buckets[i].period, report_events);

        params.buckets[i].num_channels = helper.getIntField(bucket, "num_channels");
        // ALOGD("Initialized num_channels to %d", params.buckets[i].num_channels);

        for (int j = 0; j < params.buckets[i].num_channels; j++) {
            JNIObject<jobject> channel = helper.getObjectArrayField(
                    bucket, "channels", channel_array_type, j);

            params.buckets[i].channels[j].channel = helper.getIntField(channel, "frequency");
            params.buckets[i].channels[j].dwellTimeMs = helper.getIntField(channel, "dwell_time_ms");

            bool passive = helper.getBoolField(channel, "passive");
            params.buckets[i].channels[j].passive = (passive ? 1 : 0);

            // ALOGD("Initialized channel %d", params.buckets[i].channels[j].channel);
        }
    }

    // ALOGD("Initialized all fields");

    wifi_scan_result_handler handler;
    memset(&handler, 0, sizeof(handler));
    handler.on_scan_results_available = &onScanResultsAvailable;
    handler.on_full_scan_result = &onFullScanResult;
    handler.on_scan_event = &onScanEvent;

    return hal_fn.wifi_start_gscan(id, handle, params, handler) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_stopScan(JNIEnv *env, jclass cls, jint iface, jint id) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("stopping scan on interface[%d] = %p", iface, handle);

    return hal_fn.wifi_stop_gscan(id, handle)  == WIFI_SUCCESS;
}

static int compare_scan_result_timestamp(const void *v1, const void *v2) {
    const wifi_scan_result *result1 = static_cast<const wifi_scan_result *>(v1);
    const wifi_scan_result *result2 = static_cast<const wifi_scan_result *>(v2);
    return result1->ts - result2->ts;
}

static jobject android_net_wifi_getScanResults(
        JNIEnv *env, jclass cls, jint iface, jboolean flush)  {

    JNIHelper helper(env);
    wifi_cached_scan_results scan_data[64];
    int num_scan_data = 64;

    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("getting scan results on interface[%d] = %p", iface, handle);

    byte b = flush ? 0xFF : 0;
    int result = hal_fn.wifi_get_cached_gscan_results(handle, b, num_scan_data, scan_data, &num_scan_data);
    if (result == WIFI_SUCCESS) {
        JNIObject<jobjectArray> scanData = helper.createObjectArray(
                "android/net/wifi/WifiScanner$ScanData", num_scan_data);
        if (scanData == NULL) {
            ALOGE("Error in allocating array of scanData");
            return NULL;
        }

        for (int i = 0; i < num_scan_data; i++) {

            JNIObject<jobject> data = helper.createObject("android/net/wifi/WifiScanner$ScanData");
            if (data == NULL) {
                ALOGE("Error in allocating scanData");
                return NULL;
            }

            helper.setIntField(data, "mId", scan_data[i].scan_id);
            helper.setIntField(data, "mFlags", scan_data[i].flags);

            /* sort all scan results by timestamp */
            qsort(scan_data[i].results, scan_data[i].num_results,
                    sizeof(wifi_scan_result), compare_scan_result_timestamp);

            JNIObject<jobjectArray> scanResults = helper.createObjectArray(
                    "android/net/wifi/ScanResult", scan_data[i].num_results);
            if (scanResults == NULL) {
                ALOGE("Error in allocating scanResult array");
                return NULL;
            }

            wifi_scan_result *results = scan_data[i].results;
            for (int j = 0; j < scan_data[i].num_results; j++) {

                JNIObject<jobject> scanResult = createScanResult(helper, &results[j]);
                if (scanResult == NULL) {
                    ALOGE("Error in creating scan result");
                    return NULL;
                }

                helper.setObjectArrayElement(scanResults, j, scanResult);
            }

            helper.setObjectField(data, "mResults", "[Landroid/net/wifi/ScanResult;", scanResults);
            helper.setObjectArrayElement(scanData, i, data);
        }

        // ALOGD("retrieved %d scan data from interface[%d] = %p", num_scan_data, iface, handle);
        return scanData.detach();
    } else {
        return NULL;
    }
}


static jboolean android_net_wifi_getScanCapabilities(
        JNIEnv *env, jclass cls, jint iface, jobject capabilities) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("getting scan capabilities on interface[%d] = %p", iface, handle);

    wifi_gscan_capabilities c;
    memset(&c, 0, sizeof(c));
    int result = hal_fn.wifi_get_gscan_capabilities(handle, &c);
    if (result != WIFI_SUCCESS) {
        ALOGD("failed to get capabilities : %d", result);
        return JNI_FALSE;
    }

    helper.setIntField(capabilities, "max_scan_cache_size", c.max_scan_cache_size);
    helper.setIntField(capabilities, "max_scan_buckets", c.max_scan_buckets);
    helper.setIntField(capabilities, "max_ap_cache_per_scan", c.max_ap_cache_per_scan);
    helper.setIntField(capabilities, "max_rssi_sample_size", c.max_rssi_sample_size);
    helper.setIntField(capabilities, "max_scan_reporting_threshold", c.max_scan_reporting_threshold);
    helper.setIntField(capabilities, "max_hotlist_bssids", c.max_hotlist_bssids);
    helper.setIntField(capabilities, "max_significant_wifi_change_aps",
                c.max_significant_wifi_change_aps);

    return JNI_TRUE;
}


static byte parseHexChar(char ch) {
    if (isdigit(ch))
        return ch - '0';
    else if ('A' <= ch && ch <= 'F')
        return ch - 'A' + 10;
    else if ('a' <= ch && ch <= 'f')
        return ch - 'a' + 10;
    else {
        ALOGE("invalid character in bssid %c", ch);
        return 0;
    }
}

static byte parseHexByte(const char * &str) {
    if (str[0] == '\0') {
        ALOGE("Passed an empty string");
        return 0;
    }
    byte b = parseHexChar(str[0]);
    if (str[1] == '\0' || str[1] == ':') {
        str ++;
    } else {
        b = b << 4 | parseHexChar(str[1]);
        str += 2;
    }

    // Skip trailing delimiter if not at the end of the string.
    if (str[0] != '\0') {
        str++;
    }
    return b;
}

static void parseMacAddress(const char *str, mac_addr addr) {
    addr[0] = parseHexByte(str);
    addr[1] = parseHexByte(str);
    addr[2] = parseHexByte(str);
    addr[3] = parseHexByte(str);
    addr[4] = parseHexByte(str);
    addr[5] = parseHexByte(str);
}

static bool parseMacAddress(JNIEnv *env, jobject obj, mac_addr addr) {
    JNIHelper helper(env);
    JNIObject<jstring> macAddrString = helper.getStringField(obj, "bssid");
    if (macAddrString == NULL) {
        ALOGE("Error getting bssid field");
        return false;
    }

    ScopedUtfChars chars(env, macAddrString);
    const char *bssid = chars.c_str();
    if (bssid == NULL) {
        ALOGE("Error getting bssid");
        return false;
    }

    parseMacAddress(bssid, addr);
    return true;
}

static void onHotlistApFound(wifi_request_id id,
        unsigned num_results, wifi_scan_result *results) {

    JNIHelper helper(mVM);
    ALOGD("onHotlistApFound called, vm = %p, obj = %p, num_results = %d", mVM, mCls, num_results);

    JNIObject<jobjectArray> scanResults = helper.newObjectArray(num_results,
            "android/net/wifi/ScanResult", NULL);
    if (scanResults == NULL) {
        ALOGE("Error in allocating array");
        return;
    }

    for (unsigned i = 0; i < num_results; i++) {

        JNIObject<jobject> scanResult = createScanResult(helper, &results[i]);
        if (scanResult == NULL) {
            ALOGE("Error in creating scan result");
            return;
        }

        helper.setObjectArrayElement(scanResults, i, scanResult);

        ALOGD("Found AP %32s", results[i].ssid);
    }

    helper.reportEvent(mCls, "onHotlistApFound", "(I[Landroid/net/wifi/ScanResult;)V",
        id, scanResults.get());
}

static void onHotlistApLost(wifi_request_id id,
        unsigned num_results, wifi_scan_result *results) {

    JNIHelper helper(mVM);
    ALOGD("onHotlistApLost called, vm = %p, obj = %p, num_results = %d", mVM, mCls, num_results);

    JNIObject<jobjectArray> scanResults = helper.newObjectArray(num_results,
            "android/net/wifi/ScanResult", NULL);
    if (scanResults == NULL) {
        ALOGE("Error in allocating array");
        return;
    }

    for (unsigned i = 0; i < num_results; i++) {

        JNIObject<jobject> scanResult = createScanResult(helper, &results[i]);
        if (scanResult == NULL) {
            ALOGE("Error in creating scan result");
            return;
        }

        helper.setObjectArrayElement(scanResults, i, scanResult);

        ALOGD("Lost AP %32s", results[i].ssid);
    }

    helper.reportEvent(mCls, "onHotlistApLost", "(I[Landroid/net/wifi/ScanResult;)V",
        id, scanResults.get());
}


static jboolean android_net_wifi_setHotlist(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject ap)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("setting hotlist on interface[%d] = %p", iface, handle);

    wifi_bssid_hotlist_params params;
    memset(&params, 0, sizeof(params));

    params.lost_ap_sample_size = helper.getIntField(ap, "apLostThreshold");

    JNIObject<jobjectArray> array = helper.getArrayField(
            ap, "bssidInfos", "[Landroid/net/wifi/WifiScanner$BssidInfo;");
    params.num_bssid = helper.getArrayLength(array);

    if (params.num_bssid == 0) {
        ALOGE("Error in accesing array");
        return false;
    }

    for (int i = 0; i < params.num_bssid; i++) {
        JNIObject<jobject> objAp = helper.getObjectArrayElement(array, i);

        JNIObject<jstring> macAddrString = helper.getStringField(objAp, "bssid");
        if (macAddrString == NULL) {
            ALOGE("Error getting bssid field");
            return false;
        }

        ScopedUtfChars chars(env, macAddrString);
        const char *bssid = chars.c_str();
        if (bssid == NULL) {
            ALOGE("Error getting bssid");
            return false;
        }
        parseMacAddress(bssid, params.ap[i].bssid);

        mac_addr addr;
        memcpy(addr, params.ap[i].bssid, sizeof(mac_addr));

        char bssidOut[32];
        sprintf(bssidOut, "%0x:%0x:%0x:%0x:%0x:%0x", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);

        ALOGD("Added bssid %s", bssidOut);

        params.ap[i].low = helper.getIntField(objAp, "low");
        params.ap[i].high = helper.getIntField(objAp, "high");
    }

    wifi_hotlist_ap_found_handler handler;
    memset(&handler, 0, sizeof(handler));

    handler.on_hotlist_ap_found = &onHotlistApFound;
    handler.on_hotlist_ap_lost  = &onHotlistApLost;
    return hal_fn.wifi_set_bssid_hotlist(id, handle, params, handler) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_resetHotlist(JNIEnv *env, jclass cls, jint iface, jint id)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("resetting hotlist on interface[%d] = %p", iface, handle);

    return hal_fn.wifi_reset_bssid_hotlist(id, handle) == WIFI_SUCCESS;
}

void onSignificantWifiChange(wifi_request_id id,
        unsigned num_results, wifi_significant_change_result **results) {

    JNIHelper helper(mVM);

    ALOGD("onSignificantWifiChange called, vm = %p, obj = %p", mVM, mCls);

    JNIObject<jobjectArray> scanResults = helper.newObjectArray(
            num_results, "android/net/wifi/ScanResult", NULL);
    if (scanResults == NULL) {
        ALOGE("Error in allocating array");
        return;
    }

    for (unsigned i = 0; i < num_results; i++) {

        wifi_significant_change_result &result = *(results[i]);

        JNIObject<jobject> scanResult = helper.createObject("android/net/wifi/ScanResult");
        if (scanResult == NULL) {
            ALOGE("Error in creating scan result");
            return;
        }

        // helper.setStringField(scanResult, "SSID", results[i].ssid);

        char bssid[32];
        sprintf(bssid, "%02x:%02x:%02x:%02x:%02x:%02x", result.bssid[0], result.bssid[1],
            result.bssid[2], result.bssid[3], result.bssid[4], result.bssid[5]);

        helper.setStringField(scanResult, "BSSID", bssid);

        helper.setIntField(scanResult, "level", result.rssi[0]);
        helper.setIntField(scanResult, "frequency", result.channel);
        // helper.setLongField(scanResult, "timestamp", result.ts);

        helper.setObjectArrayElement(scanResults, i, scanResult);
    }

    helper.reportEvent(mCls, "onSignificantWifiChange", "(I[Landroid/net/wifi/ScanResult;)V",
        id, scanResults.get());

}

static jboolean android_net_wifi_trackSignificantWifiChange(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject settings)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("tracking significant wifi change on interface[%d] = %p", iface, handle);

    wifi_significant_change_params params;
    memset(&params, 0, sizeof(params));

    params.rssi_sample_size = helper.getIntField(settings, "rssiSampleSize");
    params.lost_ap_sample_size = helper.getIntField(settings, "lostApSampleSize");
    params.min_breaching = helper.getIntField(settings, "minApsBreachingThreshold");

    const char *bssid_info_array_type = "[Landroid/net/wifi/WifiScanner$BssidInfo;";
    JNIObject<jobjectArray> bssids = helper.getArrayField(
            settings, "bssidInfos", bssid_info_array_type);
    params.num_bssid = helper.getArrayLength(bssids);

    if (params.num_bssid == 0) {
        ALOGE("Error in accessing array");
        return false;
    }

    ALOGD("Initialized common fields %d, %d, %d, %d", params.rssi_sample_size,
            params.lost_ap_sample_size, params.min_breaching, params.num_bssid);

    for (int i = 0; i < params.num_bssid; i++) {
        JNIObject<jobject> objAp = helper.getObjectArrayElement(bssids, i);

        JNIObject<jstring> macAddrString = helper.getStringField(objAp, "bssid");
        if (macAddrString == NULL) {
            ALOGE("Error getting bssid field");
            return false;
        }

        ScopedUtfChars chars(env, macAddrString.get());
        const char *bssid = chars.c_str();
        if (bssid == NULL) {
            ALOGE("Error getting bssid");
            return false;
        }

        mac_addr addr;
        parseMacAddress(bssid, addr);
        memcpy(params.ap[i].bssid, addr, sizeof(mac_addr));

        char bssidOut[32];
        sprintf(bssidOut, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);

        params.ap[i].low = helper.getIntField(objAp, "low");
        params.ap[i].high = helper.getIntField(objAp, "high");

        ALOGD("Added bssid %s, [%04d, %04d]", bssidOut, params.ap[i].low, params.ap[i].high);
    }

    ALOGD("Added %d bssids", params.num_bssid);

    wifi_significant_change_handler handler;
    memset(&handler, 0, sizeof(handler));

    handler.on_significant_change = &onSignificantWifiChange;
    return hal_fn.wifi_set_significant_change_handler(id, handle, params, handler) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_untrackSignificantWifiChange(
        JNIEnv *env, jclass cls, jint iface, jint id)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("resetting significant wifi change on interface[%d] = %p", iface, handle);

    return hal_fn.wifi_reset_significant_change_handler(id, handle) == WIFI_SUCCESS;
}

wifi_iface_stat link_stat;
wifi_radio_stat radio_stat; // L release has support for only one radio

void onLinkStatsResults(wifi_request_id id, wifi_iface_stat *iface_stat,
         int num_radios, wifi_radio_stat *radio_stats)
{
    if (iface_stat != 0) {
        memcpy(&link_stat, iface_stat, sizeof(wifi_iface_stat));
    } else {
        memset(&link_stat, 0, sizeof(wifi_iface_stat));
    }

    if (num_radios > 0 && radio_stats != 0) {
        memcpy(&radio_stat, radio_stats, sizeof(wifi_radio_stat));
    } else {
        memset(&radio_stat, 0, sizeof(wifi_radio_stat));
    }
}

static void android_net_wifi_setLinkLayerStats (JNIEnv *env, jclass cls, jint iface, int enable)  {
    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    wifi_link_layer_params params;
    params.aggressive_statistics_gathering = enable;
    params.mpdu_size_threshold = 128;

    ALOGD("android_net_wifi_setLinkLayerStats: %u\n", enable);

    hal_fn.wifi_set_link_stats(handle, params);
}

static jobject android_net_wifi_getLinkLayerStats (JNIEnv *env, jclass cls, jint iface)  {

    JNIHelper helper(env);
    wifi_stats_result_handler handler;
    memset(&handler, 0, sizeof(handler));
    handler.on_link_stats_results = &onLinkStatsResults;
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    int result = hal_fn.wifi_get_link_stats(0, handle, handler);
    if (result < 0) {
        ALOGE("android_net_wifi_getLinkLayerStats: failed to get link statistics\n");
        return NULL;
    }

    JNIObject<jobject> wifiLinkLayerStats = helper.createObject(
            "android/net/wifi/WifiLinkLayerStats");
    if (wifiLinkLayerStats == NULL) {
       ALOGE("Error in allocating wifiLinkLayerStats");
       return NULL;
    }

    helper.setIntField(wifiLinkLayerStats, "beacon_rx", link_stat.beacon_rx);
    helper.setIntField(wifiLinkLayerStats, "rssi_mgmt", link_stat.rssi_mgmt);
    helper.setLongField(wifiLinkLayerStats, "rxmpdu_be", link_stat.ac[WIFI_AC_BE].rx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "rxmpdu_bk", link_stat.ac[WIFI_AC_BK].rx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "rxmpdu_vi", link_stat.ac[WIFI_AC_VI].rx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "rxmpdu_vo", link_stat.ac[WIFI_AC_VO].rx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "txmpdu_be", link_stat.ac[WIFI_AC_BE].tx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "txmpdu_bk", link_stat.ac[WIFI_AC_BK].tx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "txmpdu_vi", link_stat.ac[WIFI_AC_VI].tx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "txmpdu_vo", link_stat.ac[WIFI_AC_VO].tx_mpdu);
    helper.setLongField(wifiLinkLayerStats, "lostmpdu_be", link_stat.ac[WIFI_AC_BE].mpdu_lost);
    helper.setLongField(wifiLinkLayerStats, "lostmpdu_bk", link_stat.ac[WIFI_AC_BK].mpdu_lost);
    helper.setLongField(wifiLinkLayerStats, "lostmpdu_vi",  link_stat.ac[WIFI_AC_VI].mpdu_lost);
    helper.setLongField(wifiLinkLayerStats, "lostmpdu_vo", link_stat.ac[WIFI_AC_VO].mpdu_lost);
    helper.setLongField(wifiLinkLayerStats, "retries_be", link_stat.ac[WIFI_AC_BE].retries);
    helper.setLongField(wifiLinkLayerStats, "retries_bk", link_stat.ac[WIFI_AC_BK].retries);
    helper.setLongField(wifiLinkLayerStats, "retries_vi", link_stat.ac[WIFI_AC_VI].retries);
    helper.setLongField(wifiLinkLayerStats, "retries_vo", link_stat.ac[WIFI_AC_VO].retries);

    helper.setIntField(wifiLinkLayerStats, "on_time", radio_stat.on_time);
    helper.setIntField(wifiLinkLayerStats, "tx_time", radio_stat.tx_time);
    helper.setIntField(wifiLinkLayerStats, "rx_time", radio_stat.rx_time);
    helper.setIntField(wifiLinkLayerStats, "on_time_scan", radio_stat.on_time_scan);

    return wifiLinkLayerStats.detach();
}

static jint android_net_wifi_getSupportedFeatures(JNIEnv *env, jclass cls, jint iface) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    feature_set set = 0;

    wifi_error result = WIFI_SUCCESS;
    /*
    set = WIFI_FEATURE_INFRA
        | WIFI_FEATURE_INFRA_5G
        | WIFI_FEATURE_HOTSPOT
        | WIFI_FEATURE_P2P
        | WIFI_FEATURE_SOFT_AP
        | WIFI_FEATURE_GSCAN
        | WIFI_FEATURE_PNO
        | WIFI_FEATURE_TDLS
        | WIFI_FEATURE_EPR;
    */

    result = hal_fn.wifi_get_supported_feature_set(handle, &set);
    if (result == WIFI_SUCCESS) {
        // ALOGD("wifi_get_supported_feature_set returned set = 0x%x", set);
        return set;
    } else {
        ALOGE("wifi_get_supported_feature_set returned error = 0x%x", result);
        return 0;
    }
}

static void onRttResults(wifi_request_id id, unsigned num_results, wifi_rtt_result* results[]) {

    JNIHelper helper(mVM);

    ALOGD("onRttResults called, vm = %p, obj = %p", mVM, mCls);

    JNIObject<jobjectArray> rttResults = helper.newObjectArray(
            num_results, "android/net/wifi/RttManager$RttResult", NULL);
    if (rttResults == NULL) {
        ALOGE("Error in allocating array");
        return;
    }

    for (unsigned i = 0; i < num_results; i++) {

        wifi_rtt_result *result = results[i];

        JNIObject<jobject> rttResult = helper.createObject("android/net/wifi/RttManager$RttResult");
        if (rttResult == NULL) {
            ALOGE("Error in creating rtt result");
            return;
        }

        char bssid[32];
        sprintf(bssid, "%02x:%02x:%02x:%02x:%02x:%02x", result->addr[0], result->addr[1],
            result->addr[2], result->addr[3], result->addr[4], result->addr[5]);

        helper.setStringField(rttResult, "bssid", bssid);
        helper.setIntField( rttResult, "burstNumber",              result->burst_num);
        helper.setIntField( rttResult, "measurementFrameNumber",   result->measurement_number);
        helper.setIntField( rttResult, "successMeasurementFrameNumber",   result->success_number);
        helper.setIntField(rttResult, "frameNumberPerBurstPeer",   result->number_per_burst_peer);
        helper.setIntField( rttResult, "status",                   result->status);
        helper.setIntField( rttResult, "measurementType",          result->type);
        helper.setIntField(rttResult, "retryAfterDuration",       result->retry_after_duration);
        helper.setLongField(rttResult, "ts",                       result->ts);
        helper.setIntField( rttResult, "rssi",                     result->rssi);
        helper.setIntField( rttResult, "rssiSpread",               result->rssi_spread);
        helper.setIntField( rttResult, "txRate",                   result->tx_rate.bitrate);
        helper.setIntField( rttResult, "rxRate",                   result->rx_rate.bitrate);
        helper.setLongField(rttResult, "rtt",                      result->rtt);
        helper.setLongField(rttResult, "rttStandardDeviation",     result->rtt_sd);
        helper.setIntField( rttResult, "distance",                 result->distance);
        helper.setIntField( rttResult, "distanceStandardDeviation", result->distance_sd);
        helper.setIntField( rttResult, "distanceSpread",           result->distance_spread);
        helper.setIntField( rttResult, "burstDuration",             result->burst_duration);
        helper.setIntField( rttResult, "negotiatedBurstNum",      result->negotiated_burst_num);

        JNIObject<jobject> LCI = helper.createObject(
                "android/net/wifi/RttManager$WifiInformationElement");
        if (result->LCI != NULL && result->LCI->len > 0) {
            ALOGD("Add LCI in result");
            helper.setByteField(LCI, "id", result->LCI->id);
            JNIObject<jbyteArray> elements = helper.newByteArray(result->LCI->len);
            jbyte *bytes = (jbyte *)&(result->LCI->data[0]);
            helper.setByteArrayRegion(elements, 0, result->LCI->len, bytes);
            helper.setObjectField(LCI, "data", "[B", elements);
        } else {
            ALOGD("No LCI in result");
            helper.setByteField(LCI, "id", (byte)(0xff));
        }
        helper.setObjectField(rttResult, "LCI",
            "Landroid/net/wifi/RttManager$WifiInformationElement;", LCI);

        JNIObject<jobject> LCR = helper.createObject(
                "android/net/wifi/RttManager$WifiInformationElement");
        if (result->LCR != NULL && result->LCR->len > 0) {
            ALOGD("Add LCR in result");
            helper.setByteField(LCR, "id",           result->LCR->id);
            JNIObject<jbyteArray> elements = helper.newByteArray(result->LCI->len);
            jbyte *bytes = (jbyte *)&(result->LCR->data[0]);
            helper.setByteArrayRegion(elements, 0, result->LCI->len, bytes);
            helper.setObjectField(LCR, "data", "[B", elements);
        } else {
            ALOGD("No LCR in result");
            helper.setByteField(LCR, "id", (byte)(0xff));
        }
        helper.setObjectField(rttResult, "LCR",
            "Landroid/net/wifi/RttManager$WifiInformationElement;", LCR);

        helper.setObjectArrayElement(rttResults, i, rttResult);
    }

    helper.reportEvent(mCls, "onRttResults", "(I[Landroid/net/wifi/RttManager$RttResult;)V",
        id, rttResults.get());
}

const int MaxRttConfigs = 16;

static jboolean android_net_wifi_requestRange(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject params)  {

    JNIHelper helper(env);

    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("sending rtt request [%d] = %p", id, handle);

    wifi_rtt_config configs[MaxRttConfigs];
    memset(&configs, 0, sizeof(configs));

    int len = helper.getArrayLength((jobjectArray)params);
    if (len > MaxRttConfigs) {
        return false;
    }

    for (int i = 0; i < len; i++) {

        JNIObject<jobject> param = helper.getObjectArrayElement((jobjectArray)params, i);
        if (param == NULL) {
            ALOGD("could not get element %d", i);
            continue;
        }

        wifi_rtt_config &config = configs[i];

        parseMacAddress(env, param, config.addr);
        config.type = (wifi_rtt_type)helper.getIntField(param, "requestType");
        config.peer = (rtt_peer_type)helper.getIntField(param, "deviceType");
        config.channel.center_freq = helper.getIntField(param, "frequency");
        config.channel.width = (wifi_channel_width) helper.getIntField(param, "channelWidth");
        config.channel.center_freq0 = helper.getIntField(param, "centerFreq0");
        config.channel.center_freq1 = helper.getIntField(param, "centerFreq1");

        config.num_burst = helper.getIntField(param, "numberBurst");
        config.burst_period = (unsigned) helper.getIntField(param, "interval");
        config.num_frames_per_burst = (unsigned) helper.getIntField(param, "numSamplesPerBurst");
        config.num_retries_per_rtt_frame = (unsigned) helper.getIntField(param,
                "numRetriesPerMeasurementFrame");
        config.num_retries_per_ftmr = (unsigned) helper.getIntField(param, "numRetriesPerFTMR");
        config.LCI_request = helper.getBoolField(param, "LCIRequest") ? 1 : 0;
        config.LCR_request = helper.getBoolField(param, "LCRRequest") ? 1 : 0;
        config.burst_duration = (unsigned) helper.getIntField(param, "burstTimeout");
        config.preamble = (wifi_rtt_preamble) helper.getIntField(param, "preamble");
        config.bw = (wifi_rtt_bw) helper.getIntField(param, "bandwidth");

        ALOGD("RTT request destination %d: type is %d, peer is %d, bw is %d, center_freq is %d ", i,
                config.type,config.peer, config.channel.width,  config.channel.center_freq);
        ALOGD("center_freq0 is %d, center_freq1 is %d, num_burst is %d,interval is %d",
                config.channel.center_freq0, config.channel.center_freq1, config.num_burst,
                config.burst_period);
        ALOGD("frames_per_burst is %d, retries of measurement frame is %d, retries_per_ftmr is %d",
                config.num_frames_per_burst, config.num_retries_per_rtt_frame,
                config.num_retries_per_ftmr);
        ALOGD("LCI_requestis %d, LCR_request is %d,  burst_timeout is %d, preamble is %d, bw is %d",
                config.LCI_request, config.LCR_request, config.burst_duration, config.preamble,
                config.bw);
    }

    wifi_rtt_event_handler handler;
    handler.on_rtt_results = &onRttResults;

    return hal_fn.wifi_rtt_range_request(id, handle, len, configs, handler) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_cancelRange(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject params)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("cancelling rtt request [%d] = %p", id, handle);

    mac_addr addrs[MaxRttConfigs];
    memset(&addrs, 0, sizeof(addrs));

    int len = helper.getArrayLength((jobjectArray)params);
    if (len > MaxRttConfigs) {
        return false;
    }

    for (int i = 0; i < len; i++) {

        JNIObject<jobject> param = helper.getObjectArrayElement(params, i);
        if (param == NULL) {
            ALOGD("could not get element %d", i);
            continue;
        }

        parseMacAddress(env, param, addrs[i]);
    }

    return hal_fn.wifi_rtt_range_cancel(id, handle, len, addrs) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_setScanningMacOui(JNIEnv *env, jclass cls,
        jint iface, jbyteArray param)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("setting scan oui %p", handle);

    static const unsigned oui_len = 3;          /* OUI is upper 3 bytes of mac_address */
    int len = helper.getArrayLength(param);
    if (len != oui_len) {
        ALOGE("invalid oui length %d", len);
        return false;
    }

    ScopedBytesRO paramBytes(env, param);
    const jbyte* bytes = paramBytes.get();
    if (bytes == NULL) {
        ALOGE("failed to get array");
        return false;
    }

    return hal_fn.wifi_set_scanning_mac_oui(handle, (byte *)bytes) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_is_get_channels_for_band_supported(JNIEnv *env, jclass cls){
    return (hal_fn.wifi_get_valid_channels == wifi_get_valid_channels_stub);
}

static jintArray android_net_wifi_getValidChannels(JNIEnv *env, jclass cls,
        jint iface, jint band)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("getting valid channels %p", handle);

    static const int MaxChannels = 64;
    wifi_channel channels[64];
    int num_channels = 0;
    wifi_error result = hal_fn.wifi_get_valid_channels(handle, band, MaxChannels,
            channels, &num_channels);

    if (result == WIFI_SUCCESS) {
        JNIObject<jintArray> channelArray = helper.newIntArray(num_channels);
        if (channelArray == NULL) {
            ALOGE("failed to allocate channel list");
            return NULL;
        }

        helper.setIntArrayRegion(channelArray, 0, num_channels, channels);
        return channelArray.detach();
    } else {
        ALOGE("failed to get channel list : %d", result);
        return NULL;
    }
}

static jboolean android_net_wifi_setDfsFlag(JNIEnv *env, jclass cls, jint iface, jboolean dfs) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("setting dfs flag to %s, %p", dfs ? "true" : "false", handle);

    u32 nodfs = dfs ? 0 : 1;
    wifi_error result = hal_fn.wifi_set_nodfs_flag(handle, nodfs);
    return result == WIFI_SUCCESS;
}

static jobject android_net_wifi_get_rtt_capabilities(JNIEnv *env, jclass cls, jint iface) {

    JNIHelper helper(env);
    wifi_rtt_capabilities rtt_capabilities;
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    wifi_error ret = hal_fn.wifi_get_rtt_capabilities(handle, &rtt_capabilities);

    if(WIFI_SUCCESS == ret) {
         JNIObject<jobject> capabilities = helper.createObject(
                "android/net/wifi/RttManager$RttCapabilities");
         helper.setBooleanField(capabilities, "oneSidedRttSupported",
                 rtt_capabilities.rtt_one_sided_supported == 1);
         helper.setBooleanField(capabilities, "twoSided11McRttSupported",
                 rtt_capabilities.rtt_ftm_supported == 1);
         helper.setBooleanField(capabilities, "lciSupported",
                 rtt_capabilities.lci_support);
         helper.setBooleanField(capabilities, "lcrSupported",
                 rtt_capabilities.lcr_support);
         helper.setIntField(capabilities, "preambleSupported",
                 rtt_capabilities.preamble_support);
         helper.setIntField(capabilities, "bwSupported",
                 rtt_capabilities.bw_support);
         ALOGD("One side RTT is: %s", rtt_capabilities.rtt_one_sided_supported ==1 ? "support" :
                 "not support");
         ALOGD("Two side RTT is: %s", rtt_capabilities.rtt_ftm_supported == 1 ? "support" :
                 "not support");
         ALOGD("LCR is: %s", rtt_capabilities.lcr_support == 1 ? "support" : "not support");

         ALOGD("LCI is: %s", rtt_capabilities.lci_support == 1 ? "support" : "not support");

         ALOGD("Support Preamble is : %d support BW is %d", rtt_capabilities.preamble_support,
                 rtt_capabilities.bw_support);
         return capabilities.detach();
    } else {
        return NULL;
    }
}

static jboolean android_net_wifi_set_Country_Code_Hal(JNIEnv *env,jclass cls, jint iface,
        jstring country_code) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    ScopedUtfChars chars(env, country_code);
    const char *country = chars.c_str();

    ALOGD("set country code: %s", country);
    wifi_error res = hal_fn.wifi_set_country_code(handle, country);
    return res == WIFI_SUCCESS;
}

static jboolean android_net_wifi_enable_disable_tdls(JNIEnv *env,jclass cls, jint iface,
        jboolean enable, jstring addr) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    mac_addr address;
    parseMacAddress(env, addr, address);
    wifi_tdls_handler tdls_handler;
    //tdls_handler.on_tdls_state_changed = &on_tdls_state_changed;

    if(enable) {
        return (hal_fn.wifi_enable_tdls(handle, address, NULL, tdls_handler) == WIFI_SUCCESS);
    } else {
        return (hal_fn.wifi_disable_tdls(handle, address) == WIFI_SUCCESS);
    }
}

static void on_tdls_state_changed(mac_addr addr, wifi_tdls_status status) {

    JNIHelper helper(mVM);

    ALOGD("on_tdls_state_changed is called: vm = %p, obj = %p", mVM, mCls);

    char mac[32];
    sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4],
            addr[5]);

    JNIObject<jstring> mac_address = helper.newStringUTF(mac);
    helper.reportEvent(mCls, "onTdlsStatus", "(Ljava/lang/StringII;)V",
        mac_address.get(), status.state, status.reason);

}

static jobject android_net_wifi_get_tdls_status(JNIEnv *env,jclass cls, jint iface,jstring addr) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    mac_addr address;
    parseMacAddress(env, addr, address);

    wifi_tdls_status status;

    wifi_error ret;
    ret = hal_fn.wifi_get_tdls_status(handle, address, &status );

    if (ret != WIFI_SUCCESS) {
        return NULL;
    } else {
        JNIObject<jobject> tdls_status = helper.createObject(
                "com/android/server/wifi/WifiNative$TdlsStatus");
        helper.setIntField(tdls_status, "channel", status.channel);
        helper.setIntField(tdls_status, "global_operating_class", status.global_operating_class);
        helper.setIntField(tdls_status, "state", status.state);
        helper.setIntField(tdls_status, "reason", status.reason);
        return tdls_status.detach();
    }
}

static jobject android_net_wifi_get_tdls_capabilities(JNIEnv *env, jclass cls, jint iface) {

    JNIHelper helper(env);
    wifi_tdls_capabilities tdls_capabilities;
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    wifi_error ret = hal_fn.wifi_get_tdls_capabilities(handle, &tdls_capabilities);

    if (WIFI_SUCCESS == ret) {
         JNIObject<jobject> capabilities = helper.createObject(
                 "com/android/server/wifi/WifiNative$TdlsCapabilities");
         helper.setIntField(capabilities, "maxConcurrentTdlsSessionNumber",
                 tdls_capabilities.max_concurrent_tdls_session_num);
         helper.setBooleanField(capabilities, "isGlobalTdlsSupported",
                 tdls_capabilities.is_global_tdls_supported == 1);
         helper.setBooleanField(capabilities, "isPerMacTdlsSupported",
                 tdls_capabilities.is_per_mac_tdls_supported == 1);
         helper.setBooleanField(capabilities, "isOffChannelTdlsSupported",
                 tdls_capabilities.is_off_channel_tdls_supported);

         ALOGD("TDLS Max Concurrent Tdls Session Number is: %d",
                 tdls_capabilities.max_concurrent_tdls_session_num);
         ALOGD("Global Tdls is: %s", tdls_capabilities.is_global_tdls_supported == 1 ? "support" :
                 "not support");
         ALOGD("Per Mac Tdls is: %s", tdls_capabilities.is_per_mac_tdls_supported == 1 ? "support" :
                 "not support");
         ALOGD("Off Channel Tdls is: %s", tdls_capabilities.is_off_channel_tdls_supported == 1 ?
                 "support" : "not support");

         return capabilities.detach();
    } else {
        return NULL;
    }
}

// ----------------------------------------------------------------------------
// Debug framework
// ----------------------------------------------------------------------------
static jint android_net_wifi_get_supported_logger_feature(JNIEnv *env, jclass cls, jint iface){
    //Not implemented yet
    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    return -1;
}

static jobject android_net_wifi_get_driver_version(JNIEnv *env, jclass cls, jint iface) {
     //Need to be fixed. The memory should be allocated from lower layer
    //char *buffer = NULL;
    JNIHelper helper(env);
    int buffer_length =  256;
    char *buffer = (char *)malloc(buffer_length);
    if (!buffer) return NULL;
    memset(buffer, 0, buffer_length);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    ALOGD("android_net_wifi_get_driver_version = %p", handle);

    if (handle == 0) {
        return NULL;
    }

    wifi_error result = hal_fn.wifi_get_driver_version(handle, buffer, buffer_length);

    if (result == WIFI_SUCCESS) {
        ALOGD("buffer is %p, length is %d", buffer, buffer_length);
        JNIObject<jstring> driver_version = helper.newStringUTF(buffer);
        free(buffer);
        return driver_version.detach();
    } else {
        ALOGD("Fail to get driver version");
        free(buffer);
        return NULL;
    }
}

static jobject android_net_wifi_get_firmware_version(JNIEnv *env, jclass cls, jint iface) {

    //char *buffer = NULL;
    JNIHelper helper(env);
    int buffer_length = 256;
    char *buffer = (char *)malloc(buffer_length);
    if (!buffer) return NULL;
    memset(buffer, 0, buffer_length);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    ALOGD("android_net_wifi_get_firmware_version = %p", handle);

    if (handle == 0) {
        return NULL;
    }

    wifi_error result = hal_fn.wifi_get_firmware_version(handle, buffer, buffer_length);

    if (result == WIFI_SUCCESS) {
        ALOGD("buffer is %p, length is %d", buffer, buffer_length);
        JNIObject<jstring> firmware_version = helper.newStringUTF(buffer);
        free(buffer);
        return firmware_version.detach();
    } else {
        ALOGD("Fail to get Firmware version");
        free(buffer);
        return NULL;
    }
}

static jobject android_net_wifi_get_ring_buffer_status (JNIEnv *env, jclass cls, jint iface) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    ALOGD("android_net_wifi_get_ring_buffer_status = %p", handle);

    if (handle == 0) {
        return NULL;
    }

    //wifi_ring_buffer_status *status = NULL;
    u32 num_rings = 10;
    wifi_ring_buffer_status *status =
        (wifi_ring_buffer_status *)malloc(sizeof(wifi_ring_buffer_status) * num_rings);
    if (!status) return NULL;
    memset(status, 0, sizeof(wifi_ring_buffer_status) * num_rings);
    wifi_error result = hal_fn.wifi_get_ring_buffers_status(handle, &num_rings, status);
    if (result == WIFI_SUCCESS) {
        ALOGD("status is %p, number is %d", status, num_rings);

        JNIObject<jobjectArray> ringBuffersStatus = helper.newObjectArray(
            num_rings, "com/android/server/wifi/WifiNative$RingBufferStatus", NULL);

        wifi_ring_buffer_status *tmp = status;

        for(u32 i = 0; i < num_rings; i++, tmp++) {

            JNIObject<jobject> ringStatus = helper.createObject(
                    "com/android/server/wifi/WifiNative$RingBufferStatus");

            if (ringStatus == NULL) {
                ALOGE("Error in creating ringBufferStatus");
                free(status);
                return NULL;
            }

            char name[32];
            for(int j = 0; j < 32; j++) {
                name[j] = tmp->name[j];
            }

            helper.setStringField(ringStatus, "name", name);
            helper.setIntField(ringStatus, "flag", tmp->flags);
            helper.setIntField(ringStatus, "ringBufferId", tmp->ring_id);
            helper.setIntField(ringStatus, "ringBufferByteSize", tmp->ring_buffer_byte_size);
            helper.setIntField(ringStatus, "verboseLevel", tmp->verbose_level);
            helper.setIntField(ringStatus, "writtenBytes", tmp->written_bytes);
            helper.setIntField(ringStatus, "readBytes", tmp->read_bytes);
            helper.setIntField(ringStatus, "writtenRecords", tmp->written_records);

            helper.setObjectArrayElement(ringBuffersStatus, i, ringStatus);
        }

        free(status);
        return ringBuffersStatus.detach();
    } else {
        free(status);
        return NULL;
    }
}

static void on_ring_buffer_data(char *ring_name, char *buffer, int buffer_size,
        wifi_ring_buffer_status *status) {

    if (!ring_name || !buffer || !status ||
            (unsigned int)buffer_size <= sizeof(wifi_ring_buffer_entry)) {
        ALOGE("Error input for on_ring_buffer_data!");
        return;
    }


    JNIHelper helper(mVM);
    /* ALOGD("on_ring_buffer_data called, vm = %p, obj = %p, env = %p buffer size = %d", mVM,
            mCls, env, buffer_size); */

    JNIObject<jobject> ringStatus = helper.createObject(
                    "com/android/server/wifi/WifiNative$RingBufferStatus");
    if (status == NULL) {
        ALOGE("Error in creating ringBufferStatus");
        return;
    }

    helper.setStringField(ringStatus, "name", ring_name);
    helper.setIntField(ringStatus, "flag", status->flags);
    helper.setIntField(ringStatus, "ringBufferId", status->ring_id);
    helper.setIntField(ringStatus, "ringBufferByteSize", status->ring_buffer_byte_size);
    helper.setIntField(ringStatus, "verboseLevel", status->verbose_level);
    helper.setIntField(ringStatus, "writtenBytes", status->written_bytes);
    helper.setIntField(ringStatus, "readBytes", status->read_bytes);
    helper.setIntField(ringStatus, "writtenRecords", status->written_records);

    JNIObject<jbyteArray> bytes = helper.newByteArray(buffer_size);
    helper.setByteArrayRegion(bytes, 0, buffer_size, (jbyte*)buffer);

    helper.reportEvent(mCls,"onRingBufferData",
            "(Lcom/android/server/wifi/WifiNative$RingBufferStatus;[B)V",
            ringStatus.get(), bytes.get());
}

static void on_alert_data(wifi_request_id id, char *buffer, int buffer_size, int err_code){

    JNIHelper helper(mVM);
    ALOGD("on_alert_data called, vm = %p, obj = %p, buffer_size = %d, error code = %d"
            , mVM, mCls, buffer_size, err_code);

    if (buffer_size > 0) {
        JNIObject<jbyteArray> records = helper.newByteArray(buffer_size);
        jbyte *bytes = (jbyte *) buffer;
        helper.setByteArrayRegion(records, 0,buffer_size, bytes);
        helper.reportEvent(mCls,"onWifiAlert","([BI)V", records.get(), err_code);
    } else {
        helper.reportEvent(mCls,"onWifiAlert","([BI)V", NULL, err_code);
    }
}


static jboolean android_net_wifi_start_logging_ring_buffer(JNIEnv *env, jclass cls, jint iface,
        jint verbose_level,jint flags, jint max_interval,jint min_data_size, jstring ring_name) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    ALOGD("android_net_wifi_start_logging_ring_buffer = %p", handle);

    if (handle == 0) {
        return false;
    }

    ScopedUtfChars chars(env, ring_name);
    const char* ring_name_const_char = chars.c_str();
    int ret = hal_fn.wifi_start_logging(handle, verbose_level,
            flags, max_interval, min_data_size, const_cast<char *>(ring_name_const_char));

    if (ret != WIFI_SUCCESS) {
        ALOGE("Fail to start logging for ring %s", ring_name_const_char);
    } else {
        ALOGD("start logging for ring %s", ring_name_const_char);
    }

    return ret == WIFI_SUCCESS;
}

static jboolean android_net_wifi_get_ring_buffer_data(JNIEnv *env, jclass cls, jint iface,
        jstring ring_name) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("android_net_wifi_get_ring_buffer_data = %p", handle);

    ScopedUtfChars chars(env, ring_name);
    const char* ring_name_const_char = chars.c_str();
    int result = hal_fn.wifi_get_ring_data(handle, const_cast<char *>(ring_name_const_char));
    return result == WIFI_SUCCESS;
}


void on_firmware_memory_dump(char *buffer, int buffer_size) {

    JNIHelper helper(mVM);
    /* ALOGD("on_firmware_memory_dump called, vm = %p, obj = %p, env = %p buffer_size = %d"
            , mVM, mCls, env, buffer_size); */

    if (buffer_size > 0) {
        JNIObject<jbyteArray> dump = helper.newByteArray(buffer_size);
        jbyte *bytes = (jbyte *) (buffer);
        helper.setByteArrayRegion(dump, 0, buffer_size, bytes);
        helper.reportEvent(mCls,"onWifiFwMemoryAvailable","([B)V", dump.get());
    }
}

static jboolean android_net_wifi_get_fw_memory_dump(JNIEnv *env, jclass cls, jint iface){

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    // ALOGD("android_net_wifi_get_fw_memory_dump = %p", handle);

    if (handle == NULL) {
        ALOGE("Can not get wifi_interface_handle");
        return false;
    }

    wifi_firmware_memory_dump_handler fw_dump_handle;
    fw_dump_handle.on_firmware_memory_dump = on_firmware_memory_dump;
    int result = hal_fn.wifi_get_firmware_memory_dump(handle, fw_dump_handle);
    return result == WIFI_SUCCESS;

}

static jboolean android_net_wifi_set_log_handler(JNIEnv *env, jclass cls, jint iface, jint id) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("android_net_wifi_set_log_handler = %p", handle);

    //initialize the handler on first time
    wifi_ring_buffer_data_handler handler;
    handler.on_ring_buffer_data = &on_ring_buffer_data;
    int result = hal_fn.wifi_set_log_handler(id, handle, handler);
    if (result != WIFI_SUCCESS) {
        ALOGE("Fail to set logging handler");
        return false;
    }

    //set alter handler This will start alert too
    wifi_alert_handler alert_handler;
    alert_handler.on_alert = &on_alert_data;
    result = hal_fn.wifi_set_alert_handler(id, handle, alert_handler);
    if (result != WIFI_SUCCESS) {
        ALOGE(" Fail to set alert handler");
        return false;
    }

    return true;
}

static jboolean android_net_wifi_reset_log_handler(JNIEnv *env, jclass cls, jint iface, jint id) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);

    //reset alter handler
    ALOGD("android_net_wifi_reset_alert_handler = %p", handle);
    int result = hal_fn.wifi_reset_alert_handler(id, handle);
    if (result != WIFI_SUCCESS) {
        ALOGE(" Fail to reset alert handler");
        return false;
    }

    //reset log handler
    ALOGD("android_net_wifi_reset_log_handler = %p", handle);
    result = hal_fn.wifi_reset_log_handler(id, handle);
    if (result != WIFI_SUCCESS) {
        ALOGE("Fail to reset logging handler");
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// ePno framework
// ----------------------------------------------------------------------------


static void onPnoNetworkFound(wifi_request_id id,
                                          unsigned num_results, wifi_scan_result *results) {

    JNIHelper helper(mVM);

    ALOGD("onPnoNetworkFound called, vm = %p, obj = %p, num_results %u", mVM, mCls, num_results);

    if (results == 0 || num_results == 0) {
       ALOGE("onPnoNetworkFound: Error no results");
       return;
    }

    jbyte *bytes;
    JNIObject<jobjectArray> scanResults(helper, NULL);
    //jbyteArray elements;

    for (unsigned i=0; i<num_results; i++) {

        JNIObject<jobject> scanResult = createScanResult(helper, &results[i]);
        if (i == 0) {
            scanResults = helper.newObjectArray(
                    num_results, "android/net/wifi/ScanResult", scanResult);
            if (scanResults == 0) {
                ALOGD("cant allocate array");
            } else {
                ALOGD("allocated array %u", helper.getArrayLength(scanResults));
            }
        } else {
            helper.setObjectArrayElement(scanResults, i, scanResult);
        }

        ALOGD("Scan result with ie length %d, i %u, <%s> rssi=%d %02x:%02x:%02x:%02x:%02x:%02x",
                results->ie_length, i, results[i].ssid, results[i].rssi, results[i].bssid[0],
                results[i].bssid[1],results[i].bssid[2], results[i].bssid[3], results[i].bssid[4],
                results[i].bssid[5]);

        /*elements = helper.newByteArray(results->ie_length);
        if (elements == NULL) {
            ALOGE("Error in allocating array");
            return;
        }*/

        //ALOGD("onPnoNetworkFound: Setting byte array");

        //bytes = (jbyte *)&(results->ie_data[0]);
        //helper.setByteArrayRegion(elements, 0, results->ie_length, bytes);

        //ALOGD("onPnoNetworkFound: Returning result");
    }


    ALOGD("calling report");

    helper.reportEvent(mCls, "onPnoNetworkFound", "(I[Landroid/net/wifi/ScanResult;)V", id,
               scanResults.get());
        ALOGD("free ref");
}

static jboolean android_net_wifi_setPnoListNative(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject list)  {

    JNIHelper helper(env);
    wifi_epno_handler handler;
    handler.on_network_found = &onPnoNetworkFound;

    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("configure ePno list request [%d] = %p", id, handle);

    if (list == NULL) {
        // stop pno
        int result = hal_fn.wifi_set_epno_list(id, handle, 0, NULL, handler);
        ALOGE(" setPnoListNative: STOP result = %d", result);
        return result >= 0;
    }

    wifi_epno_network net_list[MAX_PNO_SSID];
    memset(&net_list, 0, sizeof(net_list));

    size_t len = helper.getArrayLength((jobjectArray)list);
    if (len > (size_t)MAX_PNO_SSID) {
        return false;
    }

    for (unsigned int i = 0; i < len; i++) {

        JNIObject<jobject> pno_net = helper.getObjectArrayElement((jobjectArray)list, i);
        if (pno_net == NULL) {
            ALOGD("setPnoListNative: could not get element %d", i);
            continue;
        }

        JNIObject<jstring> sssid = helper.getStringField(pno_net, "SSID");
        if (sssid == NULL) {
              ALOGE("Error setPnoListNative: getting ssid field");
              return false;
        }

        ScopedUtfChars chars(env, (jstring)sssid.get());
        const char *ssid = chars.c_str();
        if (ssid == NULL) {
             ALOGE("Error setPnoListNative: getting ssid");
             return false;
        }
        int ssid_len = strnlen((const char*)ssid, 33);
        if (ssid_len > 32) {
           ALOGE("Error setPnoListNative: long ssid %u", strnlen((const char*)ssid, 256));
           return false;
        }

        if (ssid_len > 1 && ssid[0] == '"' && ssid[ssid_len-1])
        {
            // strip leading and trailing '"'
            ssid++;
            ssid_len-=2;
        }
        if (ssid_len == 0) {
            ALOGE("Error setPnoListNative: zero length ssid, skip it");
            continue;
        }
        memcpy(net_list[i].ssid, ssid, ssid_len);

        int rssit = helper.getIntField(pno_net, "rssi_threshold");
        net_list[i].rssi_threshold = (byte)rssit;
        int a = helper.getIntField(pno_net, "auth");
        net_list[i].auth_bit_field = a;
        int f = helper.getIntField(pno_net, "flags");
        net_list[i].flags = f;
        ALOGE(" setPnoListNative: idx %u rssi %d/%d auth %x/%x flags %x/%x [%s]", i,
                (signed)net_list[i].rssi_threshold, net_list[i].rssi_threshold,
                net_list[i].auth_bit_field, a, net_list[i].flags, f, net_list[i].ssid);
    }

    int result = hal_fn.wifi_set_epno_list(id, handle, len, net_list, handler);
    ALOGE(" setPnoListNative: result %d", result);

    return result >= 0;
}

static jboolean android_net_wifi_setLazyRoam(
        JNIEnv *env, jclass cls, jint iface, jint id, jboolean enabled, jobject roam_param)  {

    JNIHelper helper(env);
    wifi_error status = WIFI_SUCCESS;
    wifi_roam_params params;
    memset(&params, 0, sizeof(params));

    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("configure lazy roam request [%d] = %p", id, handle);

    if (roam_param != NULL) {
        params.A_band_boost_threshold  = helper.getIntField(roam_param, "A_band_boost_threshold");
        params.A_band_penalty_threshold  = helper.getIntField(roam_param, "A_band_penalty_threshold");
        params.A_band_boost_factor = helper.getIntField(roam_param, "A_band_boost_factor");
        params.A_band_penalty_factor  = helper.getIntField(roam_param, "A_band_penalty_factor");
        params.A_band_max_boost  = helper.getIntField(roam_param, "A_band_max_boost");
        params.lazy_roam_hysteresis = helper.getIntField(roam_param, "lazy_roam_hysteresis");
        params.alert_roam_rssi_trigger = helper.getIntField(roam_param, "alert_roam_rssi_trigger");
        status = hal_fn.wifi_set_gscan_roam_params(id, handle, &params);
    }
    ALOGE("android_net_wifi_setLazyRoam configured params status=%d\n", status);

    if (status >= 0) {
        int doEnable = enabled ? 1 : 0;
        status = hal_fn.wifi_enable_lazy_roam(id, handle, doEnable);
        ALOGE("android_net_wifi_setLazyRoam enabled roam status=%d\n", status);
    }
    return status >= 0;
}

static jboolean android_net_wifi_setBssidBlacklist(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject list)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("configure BSSID black list request [%d] = %p", id, handle);

    wifi_bssid_params params;
    memset(&params, 0, sizeof(params));

    if (list != NULL) {
        size_t len = helper.getArrayLength((jobjectArray)list);
        if (len > (size_t)MAX_BLACKLIST_BSSID) {
            return false;
        }
        for (unsigned int i = 0; i < len; i++) {

            JNIObject<jobject> jbssid = helper.getObjectArrayElement(list, i);
            if (jbssid == NULL) {
                ALOGD("configure BSSID blacklist: could not get element %d", i);
                continue;
            }

            ScopedUtfChars chars(env, (jstring)jbssid.get());
            const char *bssid = chars.c_str();
            if (bssid == NULL) {
                ALOGE("Error getting bssid");
                return false;
            }

            mac_addr addr;
            parseMacAddress(bssid, addr);
            memcpy(params.bssids[i], addr, sizeof(mac_addr));

            char bssidOut[32];
            sprintf(bssidOut, "%0x:%0x:%0x:%0x:%0x:%0x", addr[0], addr[1],
                addr[2], addr[3], addr[4], addr[5]);

            ALOGD("BSSID blacklist: added bssid %s", bssidOut);

            params.num_bssid++;
        }
    }

    ALOGD("Added %d bssids", params.num_bssid);
    return hal_fn.wifi_set_bssid_blacklist(id, handle, params) == WIFI_SUCCESS;
}

static jboolean android_net_wifi_setSsidWhitelist(
        JNIEnv *env, jclass cls, jint iface, jint id, jobject list)  {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("configure SSID white list request [%d] = %p", id, handle);
    wifi_ssid *ssids = NULL;
    int num_ssids = 0;
    if (list != NULL) {
        size_t len = helper.getArrayLength((jobjectArray)list);
        if (len > 0) {
            ssids = (wifi_ssid *)malloc(len * sizeof (wifi_ssid));
            if (!ssids) return false;
            memset(ssids, 0, len * sizeof (wifi_ssid));
            for (unsigned int i = 0; i < len; i++) {

                JNIObject<jobject> jssid = helper.getObjectArrayElement(list, i);
                if (jssid == NULL) {
                    ALOGD("configure SSID whitelist: could not get element %d", i);
                    free(ssids);
                   return false;
                }

                ScopedUtfChars chars(env, (jstring)jssid.get());
                const char *utf = chars.c_str();
                if (utf == NULL) {
                    ALOGE("Error getting sssid");
                    free(ssids);
                    return false;
                }

                int slen = strnlen(utf, 33);
                if (slen <= 0 || slen > 32) {
                    ALOGE("Error wrong ssid length %d", slen);
                    free(ssids);
                    return false;
                }

                memcpy(ssids[i].ssid, utf, slen);
                num_ssids++;
                ALOGD("SSID white list: added ssid %s", utf);
            }
        }
    }

    ALOGD("android_net_wifi_setSsidWhitelist Added %d sssids", num_ssids);
    return hal_fn.wifi_set_ssid_white_list(id, handle, num_ssids, ssids) == WIFI_SUCCESS;
}

static jint android_net_wifi_start_sending_offloaded_packet(JNIEnv *env, jclass cls, jint iface,
                    jint idx, jbyteArray srcMac, jbyteArray dstMac, jbyteArray pkt, jint period)  {
    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("Start packet offload [%d] = %p", idx, handle);
    wifi_error ret;
    wifi_request_id id = idx;
    
    ScopedBytesRO pktBytes(env, pkt), srcMacBytes(env, srcMac), dstMacBytes(env, dstMac);

    byte * pkt_data = (byte*) pktBytes.get();
    unsigned short pkt_len = env->GetArrayLength(pkt);
    byte* src_mac_addr = (byte*) srcMacBytes.get();
    byte* dst_mac_addr = (byte*) dstMacBytes.get();
    int i;
    char macAddr[32];
    sprintf(macAddr, "%0x:%0x:%0x:%0x:%0x:%0x", src_mac_addr[0], src_mac_addr[1],
            src_mac_addr[2], src_mac_addr[3], src_mac_addr[4], src_mac_addr[5]);
    ALOGD("src_mac_addr %s", macAddr);
    sprintf(macAddr, "%0x:%0x:%0x:%0x:%0x:%0x", dst_mac_addr[0], dst_mac_addr[1],
            dst_mac_addr[2], dst_mac_addr[3], dst_mac_addr[4], dst_mac_addr[5]);
    ALOGD("dst_mac_addr %s", macAddr);
    ALOGD("pkt_len %d\n", pkt_len);
    ALOGD("Pkt data : ");
    for(i = 0; i < pkt_len; i++) {
        ALOGD(" %x ", pkt_data[i]);
    }
    ALOGD("\n");
    ret =  hal_fn.wifi_start_sending_offloaded_packet(id, handle, pkt_data, pkt_len,
                src_mac_addr, dst_mac_addr, period);
    ALOGD("ret= %d\n", ret);
    return ret;
}

static jint android_net_wifi_stop_sending_offloaded_packet(JNIEnv *env, jclass cls,
                    jint iface, jint idx) {
    int ret;
    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("Stop packet offload [%d] = %p", idx, handle);
    ret =  hal_fn.wifi_stop_sending_offloaded_packet(idx, handle);
    ALOGD("ret= %d\n", ret);
    return ret;
}

static void onRssiThresholdbreached(wifi_request_id id, u8 *cur_bssid, s8 cur_rssi) {

    ALOGD("RSSI threshold breached, cur RSSI - %d!!\n", cur_rssi);
    ALOGD("BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
            cur_bssid[0], cur_bssid[1], cur_bssid[2],
            cur_bssid[3], cur_bssid[4], cur_bssid[5]);
    JNIHelper helper(mVM);
    //ALOGD("onRssiThresholdbreached called, vm = %p, obj = %p, env = %p", mVM, mCls, env);
    helper.reportEvent(mCls, "onRssiThresholdBreached", "(IB)V", id, cur_rssi);
}

static jint android_net_wifi_start_rssi_monitoring_native(JNIEnv *env, jclass cls, jint iface,
        jint idx, jbyte maxRssi, jbyte minRssi) {

    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("Start Rssi monitoring = %p", handle);
    ALOGD("MinRssi %d MaxRssi %d", minRssi, maxRssi);
    wifi_error ret;
    wifi_request_id id = idx;
    wifi_rssi_event_handler eh;
    eh.on_rssi_threshold_breached = onRssiThresholdbreached;
    ret = hal_fn.wifi_start_rssi_monitoring(id, handle, maxRssi, minRssi, eh);
    return ret;
}

static jint android_net_wifi_stop_rssi_monitoring_native(JNIEnv *env, jclass cls,
        jint iface, jint idx) {
    JNIHelper helper(env);
    wifi_interface_handle handle = getIfaceHandle(helper, cls, iface);
    ALOGD("Stop Rssi monitoring = %p", handle);
    wifi_error ret;
    wifi_request_id id = idx;
    ret = hal_fn.wifi_stop_rssi_monitoring(id, handle);
    return ret;
}

// ----------------------------------------------------------------------------

/*
 * JNI registration.
 */
static JNINativeMethod gWifiMethods[] = {
    /* name, signature, funcPtr */

    { "loadDriver", "()Z",  (void *)android_net_wifi_loadDriver },
    { "isDriverLoaded", "()Z",  (void *)android_net_wifi_isDriverLoaded },
    { "unloadDriver", "()Z",  (void *)android_net_wifi_unloadDriver },
    { "startSupplicant", "(Z)Z",  (void *)android_net_wifi_startSupplicant },
    { "killSupplicant", "(Z)Z",  (void *)android_net_wifi_killSupplicant },
    { "connectToSupplicantNative", "()Z", (void *)android_net_wifi_connectToSupplicant },
    { "closeSupplicantConnectionNative", "()V",
            (void *)android_net_wifi_closeSupplicantConnection },
    { "waitForEventNative", "()Ljava/lang/String;", (void*)android_net_wifi_waitForEvent },
    { "doBooleanCommandNative", "(Ljava/lang/String;)Z", (void*)android_net_wifi_doBooleanCommand },
    { "doIntCommandNative", "(Ljava/lang/String;)I", (void*)android_net_wifi_doIntCommand },
    { "doStringCommandNative", "(Ljava/lang/String;)Ljava/lang/String;",
            (void*) android_net_wifi_doStringCommand },
    { "startHalNative", "()Z", (void*) android_net_wifi_startHal },
    { "stopHalNative", "()V", (void*) android_net_wifi_stopHal },
    { "waitForHalEventNative", "()V", (void*) android_net_wifi_waitForHalEvents },
    { "getInterfacesNative", "()I", (void*) android_net_wifi_getInterfaces},
    { "getInterfaceNameNative", "(I)Ljava/lang/String;", (void*) android_net_wifi_getInterfaceName},
    { "getScanCapabilitiesNative", "(ILcom/android/server/wifi/WifiNative$ScanCapabilities;)Z",
            (void *) android_net_wifi_getScanCapabilities},
    { "startScanNative", "(IILcom/android/server/wifi/WifiNative$ScanSettings;)Z",
            (void*) android_net_wifi_startScan},
    { "stopScanNative", "(II)Z", (void*) android_net_wifi_stopScan},
    { "getScanResultsNative", "(IZ)[Landroid/net/wifi/WifiScanner$ScanData;",
            (void *) android_net_wifi_getScanResults},
    { "setHotlistNative", "(IILandroid/net/wifi/WifiScanner$HotlistSettings;)Z",
            (void*) android_net_wifi_setHotlist},
    { "resetHotlistNative", "(II)Z", (void*) android_net_wifi_resetHotlist},
    { "trackSignificantWifiChangeNative", "(IILandroid/net/wifi/WifiScanner$WifiChangeSettings;)Z",
            (void*) android_net_wifi_trackSignificantWifiChange},
    { "untrackSignificantWifiChangeNative", "(II)Z",
            (void*) android_net_wifi_untrackSignificantWifiChange},
    { "getWifiLinkLayerStatsNative", "(I)Landroid/net/wifi/WifiLinkLayerStats;",
            (void*) android_net_wifi_getLinkLayerStats},
    { "setWifiLinkLayerStatsNative", "(II)V",
            (void*) android_net_wifi_setLinkLayerStats},
    { "getSupportedFeatureSetNative", "(I)I",
            (void*) android_net_wifi_getSupportedFeatures},
    { "requestRangeNative", "(II[Landroid/net/wifi/RttManager$RttParams;)Z",
            (void*) android_net_wifi_requestRange},
    { "cancelRangeRequestNative", "(II[Landroid/net/wifi/RttManager$RttParams;)Z",
            (void*) android_net_wifi_cancelRange},
    { "setScanningMacOuiNative", "(I[B)Z",  (void*) android_net_wifi_setScanningMacOui},
    { "getChannelsForBandNative", "(II)[I", (void*) android_net_wifi_getValidChannels},
    { "setDfsFlagNative",         "(IZ)Z",  (void*) android_net_wifi_setDfsFlag},
    { "toggleInterfaceNative",    "(I)Z",  (void*) android_net_wifi_toggle_interface},
    { "getRttCapabilitiesNative", "(I)Landroid/net/wifi/RttManager$RttCapabilities;",
            (void*) android_net_wifi_get_rtt_capabilities},
    {"setCountryCodeHalNative", "(ILjava/lang/String;)Z",
            (void*) android_net_wifi_set_Country_Code_Hal},
    { "setPnoListNative", "(II[Lcom/android/server/wifi/WifiNative$WifiPnoNetwork;)Z",
            (void*) android_net_wifi_setPnoListNative},
    {"enableDisableTdlsNative", "(IZLjava/lang/String;)Z",
            (void*) android_net_wifi_enable_disable_tdls},
    {"getTdlsStatusNative", "(ILjava/lang/String;)Lcom/android/server/wifi/WifiNative$TdlsStatus;",
            (void*) android_net_wifi_get_tdls_status},
    {"getTdlsCapabilitiesNative", "(I)Lcom/android/server/wifi/WifiNative$TdlsCapabilities;",
            (void*) android_net_wifi_get_tdls_capabilities},
    {"getSupportedLoggerFeatureSetNative","(I)I",
            (void*) android_net_wifi_get_supported_logger_feature},
    {"getDriverVersionNative", "(I)Ljava/lang/String;",
            (void*) android_net_wifi_get_driver_version},
    {"getFirmwareVersionNative", "(I)Ljava/lang/String;",
            (void*) android_net_wifi_get_firmware_version},
    {"getRingBufferStatusNative", "(I)[Lcom/android/server/wifi/WifiNative$RingBufferStatus;",
            (void*) android_net_wifi_get_ring_buffer_status},
    {"startLoggingRingBufferNative", "(IIIIILjava/lang/String;)Z",
            (void*) android_net_wifi_start_logging_ring_buffer},
    {"getRingBufferDataNative", "(ILjava/lang/String;)Z",
            (void*) android_net_wifi_get_ring_buffer_data},
    {"getFwMemoryDumpNative","(I)Z", (void*) android_net_wifi_get_fw_memory_dump},
    { "setLazyRoamNative", "(IIZLcom/android/server/wifi/WifiNative$WifiLazyRoamParams;)Z",
            (void*) android_net_wifi_setLazyRoam},
    { "setBssidBlacklistNative", "(II[Ljava/lang/String;)Z",
            (void*)android_net_wifi_setBssidBlacklist},
    { "setSsidWhitelistNative", "(II[Ljava/lang/String;)Z",
            (void*)android_net_wifi_setSsidWhitelist},
    {"setLoggingEventHandlerNative", "(II)Z", (void *) android_net_wifi_set_log_handler},
    {"resetLogHandlerNative", "(II)Z", (void *) android_net_wifi_reset_log_handler},
    { "startSendingOffloadedPacketNative", "(II[B[B[BI)I",
             (void*)android_net_wifi_start_sending_offloaded_packet},
    { "stopSendingOffloadedPacketNative", "(II)I",
             (void*)android_net_wifi_stop_sending_offloaded_packet},
    {"startRssiMonitoringNative", "(IIBB)I",
            (void*)android_net_wifi_start_rssi_monitoring_native},
    {"stopRssiMonitoringNative", "(II)I",
            (void*)android_net_wifi_stop_rssi_monitoring_native},
    {"isGetChannelsForBandSupportedNative", "()Z",
            (void*)android_net_wifi_is_get_channels_for_band_supported}
};

int register_android_net_wifi_WifiNative(JNIEnv* env) {
    return AndroidRuntime::registerNativeMethods(env,
            "com/android/server/wifi/WifiNative", gWifiMethods, NELEM(gWifiMethods));
}


/* User to register native functions */
extern "C"
jint Java_com_android_server_wifi_WifiNative_registerNatives(JNIEnv* env, jclass clazz) {
    return AndroidRuntime::registerNativeMethods(env,
            "com/android/server/wifi/WifiNative", gWifiMethods, NELEM(gWifiMethods));
}

}; // namespace android
