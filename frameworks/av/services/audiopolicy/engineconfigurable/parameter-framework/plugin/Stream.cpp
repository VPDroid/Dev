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

#include "Stream.h"
#include "PolicyMappingKeys.h"
#include "PolicySubsystem.h"

using std::string;
using android::routing_strategy;

Stream::Stream(const string &mappingValue,
                   CInstanceConfigurableElement *instanceConfigurableElement,
                   const CMappingContext &context)
    : CFormattedSubsystemObject(instanceConfigurableElement,
                                mappingValue,
                                MappingKeyAmend1,
                                (MappingKeyAmendEnd - MappingKeyAmend1 + 1),
                                context),
      mPolicySubsystem(static_cast<const PolicySubsystem *>(
                           instanceConfigurableElement->getBelongingSubsystem())),
      mPolicyPluginInterface(mPolicySubsystem->getPolicyPluginInterface()),
      mApplicableStrategy(mDefaultApplicableStrategy)
{
    mId = static_cast<audio_stream_type_t>(context.getItemAsInteger(MappingKeyIdentifier));

    // Declares the strategy to audio policy engine
    mPolicyPluginInterface->addStream(getFormattedMappingValue(), mId);
}

bool Stream::receiveFromHW(string & /*error*/)
{
    blackboardWrite(&mApplicableStrategy, sizeof(mApplicableStrategy));
    return true;
}

bool Stream::sendToHW(string & /*error*/)
{
    uint32_t applicableStrategy;
    blackboardRead(&applicableStrategy, sizeof(applicableStrategy));
    mApplicableStrategy = applicableStrategy;
    return mPolicyPluginInterface->setStrategyForStream(mId,
                                              static_cast<routing_strategy>(mApplicableStrategy));
}
