/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "APM::Devices"
//#define LOG_NDEBUG 0

#include "DeviceDescriptor.h"
#include "AudioGain.h"
#include "HwModule.h"
#include "ConfigParsingUtils.h"

namespace android {

DeviceDescriptor::DeviceDescriptor(audio_devices_t type) :
    AudioPort(String8(""), AUDIO_PORT_TYPE_DEVICE,
              audio_is_output_device(type) ? AUDIO_PORT_ROLE_SINK :
                                             AUDIO_PORT_ROLE_SOURCE),
    mTag(""), mAddress(""), mDeviceType(type), mId(0)
{

}

audio_port_handle_t DeviceDescriptor::getId() const
{
    return mId;
}

void DeviceDescriptor::attach(const sp<HwModule>& module)
{
    AudioPort::attach(module);
    mId = getNextUniqueId();
}

bool DeviceDescriptor::equals(const sp<DeviceDescriptor>& other) const
{
    // Devices are considered equal if they:
    // - are of the same type (a device type cannot be AUDIO_DEVICE_NONE)
    // - have the same address or one device does not specify the address
    // - have the same channel mask or one device does not specify the channel mask
    if (other == 0) {
        return false;
    }
    return (mDeviceType == other->mDeviceType) &&
           (mAddress == "" || other->mAddress == "" || mAddress == other->mAddress) &&
           (mChannelMask == 0 || other->mChannelMask == 0 ||
                mChannelMask == other->mChannelMask);
}

void DeviceDescriptor::loadGains(cnode *root)
{
    AudioPort::loadGains(root);
    if (mGains.size() > 0) {
        mGains[0]->getDefaultConfig(&mGain);
    }
}

void DeviceVector::refreshTypes()
{
    mDeviceTypes = AUDIO_DEVICE_NONE;
    for(size_t i = 0; i < size(); i++) {
        mDeviceTypes |= itemAt(i)->type();
    }
    ALOGV("DeviceVector::refreshTypes() mDeviceTypes %08x", mDeviceTypes);
}

ssize_t DeviceVector::indexOf(const sp<DeviceDescriptor>& item) const
{
    for(size_t i = 0; i < size(); i++) {
        if (item->equals(itemAt(i))) {
            return i;
        }
    }
    return -1;
}

ssize_t DeviceVector::add(const sp<DeviceDescriptor>& item)
{
    ssize_t ret = indexOf(item);

    if (ret < 0) {
        ret = SortedVector::add(item);
        if (ret >= 0) {
            refreshTypes();
        }
    } else {
        ALOGW("DeviceVector::add device %08x already in", item->type());
        ret = -1;
    }
    return ret;
}

ssize_t DeviceVector::remove(const sp<DeviceDescriptor>& item)
{
    size_t i;
    ssize_t ret = indexOf(item);

    if (ret < 0) {
        ALOGW("DeviceVector::remove device %08x not in", item->type());
    } else {
        ret = SortedVector::removeAt(ret);
        if (ret >= 0) {
            refreshTypes();
        }
    }
    return ret;
}

audio_devices_t DeviceVector::getDevicesFromHwModule(audio_module_handle_t moduleHandle) const
{
    audio_devices_t devices = AUDIO_DEVICE_NONE;
    for (size_t i = 0; i < size(); i++) {
        if (itemAt(i)->getModuleHandle() == moduleHandle) {
            devices |= itemAt(i)->type();
        }
    }
    return devices;
}

void DeviceVector::loadDevicesFromType(audio_devices_t types)
{
    DeviceVector deviceList;

    uint32_t role_bit = AUDIO_DEVICE_BIT_IN & types;
    types &= ~role_bit;

    while (types) {
        uint32_t i = 31 - __builtin_clz(types);
        uint32_t type = 1 << i;
        types &= ~type;
        add(new DeviceDescriptor(type | role_bit));
    }
}

void DeviceVector::loadDevicesFromTag(char *tag,
                                       const DeviceVector& declaredDevices)
{
    char *devTag = strtok(tag, "|");
    while (devTag != NULL) {
        if (strlen(devTag) != 0) {
            audio_devices_t type = ConfigParsingUtils::stringToEnum(sDeviceTypeToEnumTable,
                                 ARRAY_SIZE(sDeviceTypeToEnumTable),
                                 devTag);
            if (type != AUDIO_DEVICE_NONE) {
                sp<DeviceDescriptor> dev = new DeviceDescriptor(type);
                if (type == AUDIO_DEVICE_IN_REMOTE_SUBMIX ||
                        type == AUDIO_DEVICE_OUT_REMOTE_SUBMIX ) {
                    dev->mAddress = String8("0");
                }
                add(dev);
            } else {
                sp<DeviceDescriptor> deviceDesc =
                        declaredDevices.getDeviceFromTag(String8(devTag));
                if (deviceDesc != 0) {
                    add(deviceDesc);
                }
            }
         }
         devTag = strtok(NULL, "|");
     }
}

sp<DeviceDescriptor> DeviceVector::getDevice(audio_devices_t type, String8 address) const
{
    sp<DeviceDescriptor> device;
    for (size_t i = 0; i < size(); i++) {
        if (itemAt(i)->type() == type) {
            if (address == "" || itemAt(i)->mAddress == address) {
                device = itemAt(i);
                if (itemAt(i)->mAddress == address) {
                    break;
                }
            }
        }
    }
    ALOGV("DeviceVector::getDevice() for type %08x address %s found %p",
          type, address.string(), device.get());
    return device;
}

sp<DeviceDescriptor> DeviceVector::getDeviceFromId(audio_port_handle_t id) const
{
    sp<DeviceDescriptor> device;
    for (size_t i = 0; i < size(); i++) {
        if (itemAt(i)->getId() == id) {
            device = itemAt(i);
            break;
        }
    }
    return device;
}

DeviceVector DeviceVector::getDevicesFromType(audio_devices_t type) const
{
    DeviceVector devices;
    bool isOutput = audio_is_output_devices(type);
    type &= ~AUDIO_DEVICE_BIT_IN;
    for (size_t i = 0; (i < size()) && (type != AUDIO_DEVICE_NONE); i++) {
        bool curIsOutput = audio_is_output_devices(itemAt(i)->mDeviceType);
        audio_devices_t curType = itemAt(i)->mDeviceType & ~AUDIO_DEVICE_BIT_IN;
        if ((isOutput == curIsOutput) && ((type & curType) != 0)) {
            devices.add(itemAt(i));
            type &= ~curType;
            ALOGV("DeviceVector::getDevicesFromType() for type %x found %p",
                  itemAt(i)->type(), itemAt(i).get());
        }
    }
    return devices;
}

DeviceVector DeviceVector::getDevicesFromTypeAddr(
        audio_devices_t type, String8 address) const
{
    DeviceVector devices;
    for (size_t i = 0; i < size(); i++) {
        if (itemAt(i)->type() == type) {
            if (itemAt(i)->mAddress == address) {
                devices.add(itemAt(i));
            }
        }
    }
    return devices;
}

sp<DeviceDescriptor> DeviceVector::getDeviceFromTag(const String8& tag) const
{
    sp<DeviceDescriptor> device;
    for (size_t i = 0; i < size(); i++) {
        if (itemAt(i)->mTag == tag) {
            device = itemAt(i);
            break;
        }
    }
    return device;
}


status_t DeviceVector::dump(int fd, const String8 &direction) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];

    snprintf(buffer, SIZE, "\n Available %s devices:\n", direction.string());
    write(fd, buffer, strlen(buffer));
    for (size_t i = 0; i < size(); i++) {
        itemAt(i)->dump(fd, 2, i);
    }
    return NO_ERROR;
}

audio_policy_dev_state_t DeviceVector::getDeviceConnectionState(const sp<DeviceDescriptor> &devDesc) const
{
    ssize_t index = indexOf(devDesc);
    return index >= 0 ? AUDIO_POLICY_DEVICE_STATE_AVAILABLE : AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
}

void DeviceDescriptor::toAudioPortConfig(struct audio_port_config *dstConfig,
                                         const struct audio_port_config *srcConfig) const
{
    dstConfig->config_mask = AUDIO_PORT_CONFIG_CHANNEL_MASK|AUDIO_PORT_CONFIG_GAIN;
    if (srcConfig != NULL) {
        dstConfig->config_mask |= srcConfig->config_mask;
    }

    AudioPortConfig::toAudioPortConfig(dstConfig, srcConfig);

    dstConfig->id = mId;
    dstConfig->role = audio_is_output_device(mDeviceType) ?
                        AUDIO_PORT_ROLE_SINK : AUDIO_PORT_ROLE_SOURCE;
    dstConfig->type = AUDIO_PORT_TYPE_DEVICE;
    dstConfig->ext.device.type = mDeviceType;

    //TODO Understand why this test is necessary. i.e. why at boot time does it crash
    // without the test?
    // This has been demonstrated to NOT be true (at start up)
    // ALOG_ASSERT(mModule != NULL);
    dstConfig->ext.device.hw_module = mModule != 0 ? mModule->mHandle : AUDIO_IO_HANDLE_NONE;
    strncpy(dstConfig->ext.device.address, mAddress.string(), AUDIO_DEVICE_MAX_ADDRESS_LEN);
}

void DeviceDescriptor::toAudioPort(struct audio_port *port) const
{
    ALOGV("DeviceDescriptor::toAudioPort() handle %d type %x", mId, mDeviceType);
    AudioPort::toAudioPort(port);
    port->id = mId;
    toAudioPortConfig(&port->active_config);
    port->ext.device.type = mDeviceType;
    port->ext.device.hw_module = mModule->mHandle;
    strncpy(port->ext.device.address, mAddress.string(), AUDIO_DEVICE_MAX_ADDRESS_LEN);
}

void DeviceDescriptor::importAudioPort(const sp<AudioPort> port) {
    AudioPort::importAudioPort(port);
    mSamplingRate = port->pickSamplingRate();
    mFormat = port->pickFormat();
    mChannelMask = port->pickChannelMask();
}

status_t DeviceDescriptor::dump(int fd, int spaces, int index) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    snprintf(buffer, SIZE, "%*sDevice %d:\n", spaces, "", index+1);
    result.append(buffer);
    if (mId != 0) {
        snprintf(buffer, SIZE, "%*s- id: %2d\n", spaces, "", mId);
        result.append(buffer);
    }
    snprintf(buffer, SIZE, "%*s- type: %-48s\n", spaces, "",
            ConfigParsingUtils::enumToString(sDeviceTypeToEnumTable,
                    ARRAY_SIZE(sDeviceTypeToEnumTable),
                    mDeviceType));
    result.append(buffer);
    if (mAddress.size() != 0) {
        snprintf(buffer, SIZE, "%*s- address: %-32s\n", spaces, "", mAddress.string());
        result.append(buffer);
    }
    write(fd, result.string(), result.size());
    AudioPort::dump(fd, spaces);

    return NO_ERROR;
}

void DeviceDescriptor::log() const
{
    ALOGI("Device id:%d type:0x%X:%s, addr:%s",
          mId,
          mDeviceType,
          ConfigParsingUtils::enumToString(
             sDeviceNameToEnumTable, ARRAY_SIZE(sDeviceNameToEnumTable), mDeviceType),
          mAddress.string());

    AudioPort::log("  ");
}

}; // namespace android
