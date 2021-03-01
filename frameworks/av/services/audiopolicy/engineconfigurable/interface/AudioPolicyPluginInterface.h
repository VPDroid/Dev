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

#pragma once

#include <RoutingStrategy.h>
#include <EngineDefinition.h>
#include <Volume.h>
#include <system/audio.h>
#include <utils/Errors.h>
#include <string>
#include <vector>

namespace android {

/**
 * This interface allows the parameter plugin to:
 *  - instantiate all the members of the policy engine (strategies, input sources, usages, profiles)
 *  - keep up to date the attributes of these policy members ( i.e. devices to be used for a
 *    strategy, strategy to be followed by a usage or a stream, ...)
 */
class AudioPolicyPluginInterface
{
public:
    /**
     * Add a strategy to the engine
     *
     * @param[in] name of the strategy to add
     * @param[in] identifier: the numerical value associated to this member. It MUST match either
     *            system/audio.h or system/audio_policy.h enumration value in order to link the
     *            parameter controled by the PFW and the policy manager component.
     *
     * @return NO_ERROR if the strategy has been added successfully, error code otherwise.
     *
     */
    virtual android::status_t addStrategy(const std::string &name, routing_strategy id) = 0;

    /**
     * Add a streams to the engine.
     *
     * @param[in] name of the stream to add
     * @param[in] identifier: the numerical value associated to this member. It MUST match either
     *            system/audio.h or system/audio_policy.h enumration value in order to link the
     *            parameter controled by the PFW and the policy manager component.
     *
     * @return NO_ERROR if the stream has been added successfully, error code otherwise.
     *
     */
    virtual android::status_t addStream(const std::string &name, audio_stream_type_t id) = 0;

    /**
     * Add a usage to the engine
     *
     * @param[in] name of the usage to add
     * @param[in] identifier: the numerical value associated to this member. It MUST match either
     *            system/audio.h or system/audio_policy.h enumration value in order to link the
     *            parameter controled by the PFW and the policy manager component.
     *
     * @return NO_ERROR if the usage has been added successfully, error code otherwise.
     *
     */
    virtual android::status_t addUsage(const std::string &name, audio_usage_t id) = 0;

    /**
     * Add an input source to the engine
     *
     * @param[in] name of the input source to add
     * @param[in] identifier: the numerical value associated to this member. It MUST match either
     *            system/audio.h or system/audio_policy.h enumration value in order to link the
     *            parameter controled by the PFW and the policy manager component.
     *
     * @return NO_ERROR if the input source has been added successfully, error code otherwise.
     *
     */
    virtual android::status_t addInputSource(const std::string &name, audio_source_t id) = 0;

    /**
     * Set the device to be used by a strategy.
     *
     * @param[in] strategy: name of the strategy for which the device to use has to be set
     * @param[in] devices; mask of devices to be used for the given strategy.
     *
     * @return true if the devices were set correclty for this strategy, false otherwise.
     */
    virtual bool setDeviceForStrategy(const routing_strategy &strategy, audio_devices_t devices) = 0;

    /**
     * Set the strategy to be followed by a stream.
     *
     * @param[in] stream: name of the stream for which the strategy to use has to be set
     * @param[in] strategy to follow for the given stream.
     *
     * @return true if the strategy were set correclty for this stream, false otherwise.
     */
    virtual bool setStrategyForStream(const audio_stream_type_t &stream, routing_strategy strategy) = 0;

    /**
     * Set the strategy to be followed by a stream.
     *
     * @param[in] stream: name of the stream for which the strategy to use has to be set
     * @param[in] strategy to follow for the given stream.
     *
     * @return true if the strategy were set correclty for this stream, false otherwise.
     */
    virtual bool setVolumeProfileForStream(const audio_stream_type_t &stream,
                                           Volume::device_category category,
                                           const VolumeCurvePoints &points) = 0;

    /**
     * Set the strategy to be followed by a usage.
     *
     * @param[in] usage: name of the usage for which the strategy to use has to be set
     * @param[in] strategy to follow for the given usage.
     *
     * @return true if the strategy were set correclty for this usage, false otherwise.
     */
    virtual bool setStrategyForUsage(const audio_usage_t &usage, routing_strategy strategy) = 0;

    /**
     * Set the input device to be used by an input source.
     *
     * @param[in] inputSource: name of the input source for which the device to use has to be set
     * @param[in] devices; mask of devices to be used for the given input source.
     *
     * @return true if the devices were set correclty for this input source, false otherwise.
     */
    virtual bool setDeviceForInputSource(const audio_source_t &inputSource,
                                         audio_devices_t device) = 0;

protected:
    virtual ~AudioPolicyPluginInterface() {}
};

}; // namespace android
