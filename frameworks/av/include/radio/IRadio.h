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

#ifndef ANDROID_HARDWARE_IRADIO_H
#define ANDROID_HARDWARE_IRADIO_H

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/IMemory.h>
#include <binder/Parcel.h>
#include <system/radio.h>

namespace android {

class IRadio : public IInterface
{
public:

    DECLARE_META_INTERFACE(Radio);

    virtual void detach() = 0;

    virtual status_t setConfiguration(const struct radio_band_config *config) = 0;

    virtual status_t getConfiguration(struct radio_band_config *config) = 0;

    virtual status_t setMute(bool mute) = 0;

    virtual status_t getMute(bool *mute) = 0;

    virtual status_t step(radio_direction_t direction, bool skipSubChannel) = 0;

    virtual status_t scan(radio_direction_t direction, bool skipSubChannel) = 0;

    virtual status_t tune(unsigned int channel, unsigned int subChannel) = 0;

    virtual status_t cancel() = 0;

    virtual status_t getProgramInformation(struct radio_program_info *info) = 0;

    virtual status_t hasControl(bool *hasControl) = 0;
};

// ----------------------------------------------------------------------------

class BnRadio: public BnInterface<IRadio>
{
public:
    virtual status_t    onTransact( uint32_t code,
                                    const Parcel& data,
                                    Parcel* reply,
                                    uint32_t flags = 0);
};

}; // namespace android

#endif //ANDROID_HARDWARE_IRADIO_H
