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

#define LOG_TAG "APM::AudioPolicyEngine"
//#define LOG_NDEBUG 0

//#define VERY_VERBOSE_LOGGING
#ifdef VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include "Engine.h"
#include "Strategy.h"
#include "Stream.h"
#include "InputSource.h"
#include "Usage.h"
#include <policy.h>
#include <ParameterManagerWrapper.h>

using std::string;
using std::map;

namespace android
{
namespace audio_policy
{
template <>
StrategyCollection &Engine::getCollection<routing_strategy>()
{
    return mStrategyCollection;
}
template <>
StreamCollection &Engine::getCollection<audio_stream_type_t>()
{
    return mStreamCollection;
}
template <>
UsageCollection &Engine::getCollection<audio_usage_t>()
{
    return mUsageCollection;
}
template <>
InputSourceCollection &Engine::getCollection<audio_source_t>()
{
    return mInputSourceCollection;
}

template <>
const StrategyCollection &Engine::getCollection<routing_strategy>() const
{
    return mStrategyCollection;
}
template <>
const StreamCollection &Engine::getCollection<audio_stream_type_t>() const
{
    return mStreamCollection;
}
template <>
const UsageCollection &Engine::getCollection<audio_usage_t>() const
{
    return mUsageCollection;
}
template <>
const InputSourceCollection &Engine::getCollection<audio_source_t>() const
{
    return mInputSourceCollection;
}

Engine::Engine()
    : mManagerInterface(this),
      mPluginInterface(this),
      mPolicyParameterMgr(new ParameterManagerWrapper()),
      mApmObserver(NULL)
{
}

Engine::~Engine()
{
    mStrategyCollection.clear();
    mStreamCollection.clear();
    mInputSourceCollection.clear();
    mUsageCollection.clear();
}


void Engine::setObserver(AudioPolicyManagerObserver *observer)
{
    ALOG_ASSERT(observer != NULL, "Invalid Audio Policy Manager observer");
    mApmObserver = observer;
}

status_t Engine::initCheck()
{
    if (mPolicyParameterMgr != NULL && mPolicyParameterMgr->start() != NO_ERROR) {
        ALOGE("%s: could not start Policy PFW", __FUNCTION__);
        delete mPolicyParameterMgr;
        mPolicyParameterMgr = NULL;
        return NO_INIT;
    }
    return (mApmObserver != NULL)? NO_ERROR : NO_INIT;
}

bool Engine::setVolumeProfileForStream(const audio_stream_type_t &streamType,
                                       Volume::device_category deviceCategory,
                                       const VolumeCurvePoints &points)
{
    Stream *stream = getFromCollection<audio_stream_type_t>(streamType);
    if (stream == NULL) {
        ALOGE("%s: stream %d not found", __FUNCTION__, streamType);
        return false;
    }
    return stream->setVolumeProfile(deviceCategory, points) == NO_ERROR;
}

template <typename Key>
Element<Key> *Engine::getFromCollection(const Key &key) const
{
    const Collection<Key> collection = getCollection<Key>();
    return collection.get(key);
}

template <typename Key>
status_t Engine::add(const std::string &name, const Key &key)
{
    Collection<Key> &collection = getCollection<Key>();
    return collection.add(name, key);
}

template <typename Property, typename Key>
Property Engine::getPropertyForKey(Key key) const
{
    Element<Key> *element = getFromCollection<Key>(key);
    if (element == NULL) {
        ALOGE("%s: Element not found within collection", __FUNCTION__);
        return static_cast<Property>(0);
    }
    return element->template get<Property>();
}

routing_strategy Engine::ManagerInterfaceImpl::getStrategyForUsage(audio_usage_t usage)
{
    const SwAudioOutputCollection &outputs = mPolicyEngine->mApmObserver->getOutputs();

    if (usage == AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY &&
            (outputs.isStreamActive(AUDIO_STREAM_RING) ||
             outputs.isStreamActive(AUDIO_STREAM_ALARM))) {
        return STRATEGY_SONIFICATION;
    }
    return mPolicyEngine->getPropertyForKey<routing_strategy, audio_usage_t>(usage);
}

audio_devices_t Engine::ManagerInterfaceImpl::getDeviceForStrategy(routing_strategy strategy) const
{
    const SwAudioOutputCollection &outputs = mPolicyEngine->mApmObserver->getOutputs();

    /** This is the only case handled programmatically because the PFW is unable to know the
     * activity of streams.
     *
     * -While media is playing on a remote device, use the the sonification behavior.
     * Note that we test this usecase before testing if media is playing because
     * the isStreamActive() method only informs about the activity of a stream, not
     * if it's for local playback. Note also that we use the same delay between both tests
     *
     * -When media is not playing anymore, fall back on the sonification behavior
     */
    if (strategy == STRATEGY_SONIFICATION_RESPECTFUL &&
            !is_state_in_call(getPhoneState()) &&
            !outputs.isStreamActiveRemotely(AUDIO_STREAM_MUSIC,
                                    SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY) &&
            outputs.isStreamActive(AUDIO_STREAM_MUSIC, SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
        return mPolicyEngine->getPropertyForKey<audio_devices_t, routing_strategy>(STRATEGY_MEDIA);
    }
    return mPolicyEngine->getPropertyForKey<audio_devices_t, routing_strategy>(strategy);
}

template <typename Property, typename Key>
bool Engine::setPropertyForKey(const Property &property, const Key &key)
{
    Element<Key> *element = getFromCollection<Key>(key);
    if (element == NULL) {
        ALOGE("%s: Element not found within collection", __FUNCTION__);
        return BAD_VALUE;
    }
    return element->template set<Property>(property) == NO_ERROR;
}

float Engine::volIndexToDb(Volume::device_category category,
                             audio_stream_type_t streamType,
                             int indexInUi)
{
    Stream *stream = getFromCollection<audio_stream_type_t>(streamType);
    if (stream == NULL) {
        ALOGE("%s: Element indexed by key=%d not found", __FUNCTION__, streamType);
        return 1.0f;
    }
    return stream->volIndexToDb(category, indexInUi);
}

status_t Engine::initStreamVolume(audio_stream_type_t streamType,
                                           int indexMin, int indexMax)
{
    Stream *stream = getFromCollection<audio_stream_type_t>(streamType);
    if (stream == NULL) {
        ALOGE("%s: Stream Type %d not found", __FUNCTION__, streamType);
        return BAD_TYPE;
    }
    mApmObserver->getStreamDescriptors().setVolumeIndexMin(streamType, indexMin);
    mApmObserver->getStreamDescriptors().setVolumeIndexMax(streamType, indexMax);

    return stream->initVolume(indexMin, indexMax);
}

status_t Engine::setPhoneState(audio_mode_t mode)
{
    return mPolicyParameterMgr->setPhoneState(mode);
}

audio_mode_t Engine::getPhoneState() const
{
    return mPolicyParameterMgr->getPhoneState();
}

status_t Engine::setForceUse(audio_policy_force_use_t usage,
                                      audio_policy_forced_cfg_t config)
{
    return mPolicyParameterMgr->setForceUse(usage, config);
}

audio_policy_forced_cfg_t Engine::getForceUse(audio_policy_force_use_t usage) const
{
    return mPolicyParameterMgr->getForceUse(usage);
}

status_t Engine::setDeviceConnectionState(audio_devices_t devices, audio_policy_dev_state_t state,
                                          const char *deviceAddress)
{
    return mPolicyParameterMgr->setDeviceConnectionState(devices, state, deviceAddress);
}

template <>
AudioPolicyManagerInterface *Engine::queryInterface()
{
    return &mManagerInterface;
}

template <>
AudioPolicyPluginInterface *Engine::queryInterface()
{
    return &mPluginInterface;
}

} // namespace audio_policy
} // namespace android


