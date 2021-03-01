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

#ifndef ANDROID_SENSOR_INTERFACE_H
#define ANDROID_SENSOR_INTERFACE_H

#include <stdint.h>
#include <sys/types.h>

#include <gui/Sensor.h>

#include "SensorDevice.h"

// ---------------------------------------------------------------------------

namespace android {
// ---------------------------------------------------------------------------

class SensorInterface {
public:
    virtual ~SensorInterface();

    virtual bool process(sensors_event_t* outEvent,
            const sensors_event_t& event) = 0;

    virtual status_t activate(void* ident, bool enabled) = 0;
    virtual status_t setDelay(void* ident, int handle, int64_t ns) = 0;

    // Not all sensors need to support batching.
    virtual status_t batch(void* ident, int handle, int /*flags*/, int64_t samplingPeriodNs,
                           int64_t maxBatchReportLatencyNs) {
        if (maxBatchReportLatencyNs == 0) {
            return setDelay(ident, handle, samplingPeriodNs);
        }
        return -EINVAL;
    }

    virtual status_t flush(void* /*ident*/, int /*handle*/) {
        return -EINVAL;
    }

    virtual Sensor getSensor() const = 0;
    virtual bool isVirtual() const = 0;
    virtual void autoDisable(void* /*ident*/, int /*handle*/) { }
};

// ---------------------------------------------------------------------------

class HardwareSensor : public SensorInterface
{
    SensorDevice& mSensorDevice;
    Sensor mSensor;

public:
    HardwareSensor(const sensor_t& sensor);

    virtual ~HardwareSensor();

    virtual bool process(sensors_event_t* outEvent,
            const sensors_event_t& event);

    virtual status_t activate(void* ident, bool enabled);
    virtual status_t batch(void* ident, int handle, int flags, int64_t samplingPeriodNs,
                           int64_t maxBatchReportLatencyNs);
    virtual status_t setDelay(void* ident, int handle, int64_t ns);
    virtual status_t flush(void* ident, int handle);
    virtual Sensor getSensor() const;
    virtual bool isVirtual() const { return false; }
    virtual void autoDisable(void *ident, int handle);
};


// ---------------------------------------------------------------------------
}; // namespace android

#endif // ANDROID_SENSOR_INTERFACE_H
