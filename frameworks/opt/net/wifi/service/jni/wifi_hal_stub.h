/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef __WIFI_HAL_STUB_H__
#define __WIFI_HAL_STUB_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include "wifi_hal.h"
/* declare all HAL stub API here*/
wifi_error wifi_initialize_stub(wifi_handle *handle);
void wifi_cleanup_stub(wifi_handle handle, wifi_cleaned_up_handler handler);
void wifi_event_loop_stub(wifi_handle handle);
void wifi_get_error_info_stub(wifi_error err, const char **msg);
wifi_error wifi_get_supported_feature_set_stub(wifi_interface_handle handle, feature_set *set);
wifi_error wifi_get_concurrency_matrix_stub(wifi_interface_handle handle, int set_size_max,
        feature_set set[], int *set_size);
wifi_error wifi_get_ifaces_stub(wifi_handle handle, int *num_ifaces, wifi_interface_handle **ifaces);
wifi_error wifi_get_iface_name_stub(wifi_interface_handle iface, char *name, size_t size);
wifi_error wifi_set_iface_event_handler_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_event_handler eh);
wifi_error wifi_reset_iface_event_handler_stub(wifi_request_id id, wifi_interface_handle iface);
wifi_error wifi_set_nodfs_flag_stub(wifi_interface_handle handle, u32 nodfs);
wifi_error wifi_set_scanning_mac_oui_stub(wifi_interface_handle handle, unsigned char *oui);
wifi_error wifi_get_supported_channels_stub(wifi_handle handle, int *size, wifi_channel *list);
wifi_error wifi_is_epr_supported_stub(wifi_handle handle);
wifi_error wifi_start_gscan_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_scan_cmd_params params, wifi_scan_result_handler handler);
wifi_error wifi_stop_gscan_stub(wifi_request_id id, wifi_interface_handle iface);
wifi_error wifi_get_cached_gscan_results_stub(wifi_interface_handle iface, byte flush,
        int max, wifi_cached_scan_results *results, int *num);
wifi_error wifi_set_bssid_hotlist_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_bssid_hotlist_params params, wifi_hotlist_ap_found_handler handler);
wifi_error wifi_reset_bssid_hotlist_stub(wifi_request_id id, wifi_interface_handle iface);
wifi_error wifi_set_significant_change_handler_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_significant_change_params params, wifi_significant_change_handler handler);
wifi_error wifi_reset_significant_change_handler_stub(wifi_request_id id,
        wifi_interface_handle iface);
wifi_error wifi_get_gscan_capabilities_stub(wifi_interface_handle handle,
        wifi_gscan_capabilities *capabilities);
wifi_error wifi_set_link_stats_stub(wifi_interface_handle iface, wifi_link_layer_params params);
wifi_error wifi_get_link_stats_stub(wifi_request_id id,
         wifi_interface_handle iface, wifi_stats_result_handler handler);
wifi_error wifi_clear_link_stats_stub(wifi_interface_handle iface,
       u32 stats_clear_req_mask, u32 *stats_clear_rsp_mask, u8 stop_req, u8 *stop_rsp);
wifi_error wifi_get_valid_channels_stub(wifi_interface_handle handle,
         int band, int max_channels, wifi_channel *channels, int *num_channels);
wifi_error wifi_rtt_range_request_stub(wifi_request_id id, wifi_interface_handle iface,
         unsigned num_rtt_config, wifi_rtt_config rtt_config[], wifi_rtt_event_handler handler);
wifi_error wifi_rtt_range_cancel_stub(wifi_request_id id,  wifi_interface_handle iface,
         unsigned num_devices, mac_addr addr[]);
wifi_error wifi_get_rtt_capabilities_stub(wifi_interface_handle iface,
         wifi_rtt_capabilities *capabilities);
wifi_error wifi_set_nodfs_flag_stub(wifi_interface_handle iface, u32 nodfs);
wifi_error wifi_start_logging_stub(wifi_interface_handle iface, u32 verbose_level, u32 flags,
         u32 max_interval_sec, u32 min_data_size, char *buffer_name);
wifi_error wifi_set_epno_list_stub(int id, wifi_interface_info *iface, int num_networks,
         wifi_epno_network *networks, wifi_epno_handler handler);
wifi_error wifi_set_country_code_stub(wifi_interface_handle iface, const char *code);
wifi_error wifi_get_firmware_memory_dump_stub( wifi_interface_handle iface,
        wifi_firmware_memory_dump_handler handler);
wifi_error wifi_set_log_handler_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_ring_buffer_data_handler handler);
wifi_error wifi_reset_log_handler_stub(wifi_request_id id, wifi_interface_handle iface);
wifi_error wifi_set_alert_handler_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_alert_handler handler);
wifi_error wifi_reset_alert_handler_stub(wifi_request_id id, wifi_interface_handle iface);
wifi_error wifi_get_firmware_version_stub(wifi_interface_handle iface, char *buffer,
        int buffer_size);
wifi_error wifi_get_ring_buffers_status_stub(wifi_interface_handle iface,
        u32 *num_rings, wifi_ring_buffer_status *status);
wifi_error wifi_get_logger_supported_feature_set_stub(wifi_interface_handle iface,
        unsigned int *support);
wifi_error wifi_get_ring_data_stub(wifi_interface_handle iface, char *ring_name);
wifi_error wifi_set_epno_list_stub(int id, wifi_interface_info *iface, int num_networks,
        wifi_epno_network *networks, wifi_epno_handler handler);
wifi_error wifi_enable_tdls_stub(wifi_interface_handle iface, mac_addr addr,
        wifi_tdls_params *params, wifi_tdls_handler handler);
wifi_error wifi_disable_tdls_stub(wifi_interface_handle iface, mac_addr addr);
wifi_error wifi_get_tdls_status_stub(wifi_interface_handle iface, mac_addr addr,
        wifi_tdls_status *status);
wifi_error wifi_get_tdls_capabilities_stub(wifi_interface_handle iface,
        wifi_tdls_capabilities *capabilities);
wifi_error wifi_get_driver_version_stub(wifi_interface_handle iface, char *buffer,
        int buffer_size);
 wifi_error wifi_set_country_code_stub(wifi_interface_handle iface, const char *code);
wifi_error wifi_set_bssid_blacklist_stub(wifi_request_id id, wifi_interface_handle iface,
        wifi_bssid_params params);
wifi_error wifi_enable_lazy_roam_stub(wifi_request_id id, wifi_interface_handle iface, int enable);
wifi_error wifi_set_bssid_preference_stub(wifi_request_id id, wifi_interface_handle iface,
                                    int num_bssid, wifi_bssid_preference *prefs);
wifi_error wifi_set_gscan_roam_params_stub(wifi_request_id id, wifi_interface_handle iface,
                                        wifi_roam_params * params);
wifi_error wifi_set_ssid_white_list_stub(wifi_request_id id, wifi_interface_handle iface,
                       int num_networks, wifi_ssid *ssids);
wifi_error wifi_start_sending_offloaded_packet_stub(wifi_request_id id,
        wifi_interface_handle iface, u8 *ip_packet, u16 ip_packet_len,
        u8 *src_mac_addr, u8 *dst_mac_addr, u32 period_msec);
wifi_error wifi_stop_sending_offloaded_packet_stub(wifi_request_id id, wifi_interface_handle iface);
#ifdef __cplusplus
}
#endif
#endif //__WIFI_HAL_STUB_H__
