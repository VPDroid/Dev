/*
 * Copyright (C) 2013 The Android Open Source Project
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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

// This is needed for stdint.h to define INT64_MAX in C++
#define __STDC_LIMIT_MACROS

#include <math.h>

#include <cutils/log.h>

#include <ui/Fence.h>

#include <utils/String8.h>
#include <utils/Thread.h>
#include <utils/Trace.h>
#include <utils/Vector.h>

#include "DispSync.h"
#include "EventLog/EventLog.h"

namespace android {

// Setting this to true enables verbose tracing that can be used to debug
// vsync event model or phase issues.
static const bool kTraceDetailedInfo = false;

// This is the threshold used to determine when hardware vsync events are
// needed to re-synchronize the software vsync model with the hardware.  The
// error metric used is the mean of the squared difference between each
// present time and the nearest software-predicted vsync.
static const nsecs_t kErrorThreshold = 160000000000;    // 400 usec squared

// This is the offset from the present fence timestamps to the corresponding
// vsync event.
static const int64_t kPresentTimeOffset = PRESENT_TIME_OFFSET_FROM_VSYNC_NS;

class DispSyncThread: public Thread {
public:

    DispSyncThread():
            mStop(false),
            mPeriod(0),
            mPhase(0),
            mWakeupLatency(0) {
    }

    virtual ~DispSyncThread() {}

    void updateModel(nsecs_t period, nsecs_t phase) {
        Mutex::Autolock lock(mMutex);
        mPeriod = period;
        mPhase = phase;
        mCond.signal();
    }

    void stop() {
        Mutex::Autolock lock(mMutex);
        mStop = true;
        mCond.signal();
    }

    virtual bool threadLoop() {
        status_t err;
        nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
        nsecs_t nextEventTime = 0;

        while (true) {
            Vector<CallbackInvocation> callbackInvocations;

            nsecs_t targetTime = 0;

            { // Scope for lock
                Mutex::Autolock lock(mMutex);

                if (mStop) {
                    return false;
                }

                if (mPeriod == 0) {
                    err = mCond.wait(mMutex);
                    if (err != NO_ERROR) {
                        ALOGE("error waiting for new events: %s (%d)",
                                strerror(-err), err);
                        return false;
                    }
                    continue;
                }

                nextEventTime = computeNextEventTimeLocked(now);
                targetTime = nextEventTime;

                bool isWakeup = false;

                if (now < targetTime) {
                    err = mCond.waitRelative(mMutex, targetTime - now);

                    if (err == TIMED_OUT) {
                        isWakeup = true;
                    } else if (err != NO_ERROR) {
                        ALOGE("error waiting for next event: %s (%d)",
                                strerror(-err), err);
                        return false;
                    }
                }

                now = systemTime(SYSTEM_TIME_MONOTONIC);

                if (isWakeup) {
                    mWakeupLatency = ((mWakeupLatency * 63) +
                            (now - targetTime)) / 64;
                    if (mWakeupLatency > 500000) {
                        // Don't correct by more than 500 us
                        mWakeupLatency = 500000;
                    }
                    if (kTraceDetailedInfo) {
                        ATRACE_INT64("DispSync:WakeupLat", now - nextEventTime);
                        ATRACE_INT64("DispSync:AvgWakeupLat", mWakeupLatency);
                    }
                }

                callbackInvocations = gatherCallbackInvocationsLocked(now);
            }

            if (callbackInvocations.size() > 0) {
                fireCallbackInvocations(callbackInvocations);
            }
        }

        return false;
    }

    status_t addEventListener(nsecs_t phase, const sp<DispSync::Callback>& callback) {
        Mutex::Autolock lock(mMutex);

        for (size_t i = 0; i < mEventListeners.size(); i++) {
            if (mEventListeners[i].mCallback == callback) {
                return BAD_VALUE;
            }
        }

        EventListener listener;
        listener.mPhase = phase;
        listener.mCallback = callback;

        // We want to allow the firstmost future event to fire without
        // allowing any past events to fire.  Because
        // computeListenerNextEventTimeLocked filters out events within a half
        // a period of the last event time, we need to initialize the last
        // event time to a half a period in the past.
        listener.mLastEventTime = systemTime(SYSTEM_TIME_MONOTONIC) - mPeriod / 2;

        mEventListeners.push(listener);

        mCond.signal();

        return NO_ERROR;
    }

    status_t removeEventListener(const sp<DispSync::Callback>& callback) {
        Mutex::Autolock lock(mMutex);

        for (size_t i = 0; i < mEventListeners.size(); i++) {
            if (mEventListeners[i].mCallback == callback) {
                mEventListeners.removeAt(i);
                mCond.signal();
                return NO_ERROR;
            }
        }

        return BAD_VALUE;
    }

    // This method is only here to handle the kIgnorePresentFences case.
    bool hasAnyEventListeners() {
        Mutex::Autolock lock(mMutex);
        return !mEventListeners.empty();
    }

private:

    struct EventListener {
        nsecs_t mPhase;
        nsecs_t mLastEventTime;
        sp<DispSync::Callback> mCallback;
    };

    struct CallbackInvocation {
        sp<DispSync::Callback> mCallback;
        nsecs_t mEventTime;
    };

    nsecs_t computeNextEventTimeLocked(nsecs_t now) {
        nsecs_t nextEventTime = INT64_MAX;
        for (size_t i = 0; i < mEventListeners.size(); i++) {
            nsecs_t t = computeListenerNextEventTimeLocked(mEventListeners[i],
                    now);

            if (t < nextEventTime) {
                nextEventTime = t;
            }
        }

        return nextEventTime;
    }

    Vector<CallbackInvocation> gatherCallbackInvocationsLocked(nsecs_t now) {
        Vector<CallbackInvocation> callbackInvocations;
        nsecs_t ref = now - mPeriod;

        for (size_t i = 0; i < mEventListeners.size(); i++) {
            nsecs_t t = computeListenerNextEventTimeLocked(mEventListeners[i],
                    ref);

            if (t < now) {
                CallbackInvocation ci;
                ci.mCallback = mEventListeners[i].mCallback;
                ci.mEventTime = t;
                callbackInvocations.push(ci);
                mEventListeners.editItemAt(i).mLastEventTime = t;
            }
        }

        return callbackInvocations;
    }

    nsecs_t computeListenerNextEventTimeLocked(const EventListener& listener,
            nsecs_t ref) {

        nsecs_t lastEventTime = listener.mLastEventTime;
        if (ref < lastEventTime) {
            ref = lastEventTime;
        }

        nsecs_t phase = mPhase + listener.mPhase;
        nsecs_t t = (((ref - phase) / mPeriod) + 1) * mPeriod + phase;

        if (t - listener.mLastEventTime < mPeriod / 2) {
            t += mPeriod;
        }

        return t;
    }

    void fireCallbackInvocations(const Vector<CallbackInvocation>& callbacks) {
        for (size_t i = 0; i < callbacks.size(); i++) {
            callbacks[i].mCallback->onDispSyncEvent(callbacks[i].mEventTime);
        }
    }

    bool mStop;

    nsecs_t mPeriod;
    nsecs_t mPhase;
    nsecs_t mWakeupLatency;

    Vector<EventListener> mEventListeners;

    Mutex mMutex;
    Condition mCond;
};

class ZeroPhaseTracer : public DispSync::Callback {
public:
    ZeroPhaseTracer() : mParity(false) {}

    virtual void onDispSyncEvent(nsecs_t /*when*/) {
        mParity = !mParity;
        ATRACE_INT("ZERO_PHASE_VSYNC", mParity ? 1 : 0);
    }

private:
    bool mParity;
};

DispSync::DispSync() :
        mRefreshSkipCount(0),
        mThread(new DispSyncThread()) {

    mThread->run("DispSync", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);

    reset();
    beginResync();

    if (kTraceDetailedInfo) {
        // If we're not getting present fences then the ZeroPhaseTracer
        // would prevent HW vsync event from ever being turned off.
        // Even if we're just ignoring the fences, the zero-phase tracing is
        // not needed because any time there is an event registered we will
        // turn on the HW vsync events.
        if (!kIgnorePresentFences) {
            addEventListener(0, new ZeroPhaseTracer());
        }
    }
}

DispSync::~DispSync() {}

void DispSync::reset() {
    Mutex::Autolock lock(mMutex);

    mNumResyncSamples = 0;
    mFirstResyncSample = 0;
    mNumResyncSamplesSincePresent = 0;
    resetErrorLocked();
}

bool DispSync::addPresentFence(const sp<Fence>& fence) {
    Mutex::Autolock lock(mMutex);

    mPresentFences[mPresentSampleOffset] = fence;
    mPresentTimes[mPresentSampleOffset] = 0;
    mPresentSampleOffset = (mPresentSampleOffset + 1) % NUM_PRESENT_SAMPLES;
    mNumResyncSamplesSincePresent = 0;

    for (size_t i = 0; i < NUM_PRESENT_SAMPLES; i++) {
        const sp<Fence>& f(mPresentFences[i]);
        if (f != NULL) {
            nsecs_t t = f->getSignalTime();
            if (t < INT64_MAX) {
                mPresentFences[i].clear();
                mPresentTimes[i] = t + kPresentTimeOffset;
            }
        }
    }

    updateErrorLocked();

    return mPeriod == 0 || mError > kErrorThreshold;
}

void DispSync::beginResync() {
    Mutex::Autolock lock(mMutex);

    mNumResyncSamples = 0;
}

bool DispSync::addResyncSample(nsecs_t timestamp) {
    Mutex::Autolock lock(mMutex);

    size_t idx = (mFirstResyncSample + mNumResyncSamples) % MAX_RESYNC_SAMPLES;
    mResyncSamples[idx] = timestamp;

    if (mNumResyncSamples < MAX_RESYNC_SAMPLES) {
        mNumResyncSamples++;
    } else {
        mFirstResyncSample = (mFirstResyncSample + 1) % MAX_RESYNC_SAMPLES;
    }

    updateModelLocked();

    if (mNumResyncSamplesSincePresent++ > MAX_RESYNC_SAMPLES_WITHOUT_PRESENT) {
        resetErrorLocked();
    }

    if (kIgnorePresentFences) {
        // If we don't have the sync framework we will never have
        // addPresentFence called.  This means we have no way to know whether
        // or not we're synchronized with the HW vsyncs, so we just request
        // that the HW vsync events be turned on whenever we need to generate
        // SW vsync events.
        return mThread->hasAnyEventListeners();
    }

    return mPeriod == 0 || mError > kErrorThreshold;
}

void DispSync::endResync() {
}

status_t DispSync::addEventListener(nsecs_t phase,
        const sp<Callback>& callback) {

    Mutex::Autolock lock(mMutex);
    return mThread->addEventListener(phase, callback);
}

void DispSync::setRefreshSkipCount(int count) {
    Mutex::Autolock lock(mMutex);
    ALOGD("setRefreshSkipCount(%d)", count);
    mRefreshSkipCount = count;
    updateModelLocked();
}

status_t DispSync::removeEventListener(const sp<Callback>& callback) {
    Mutex::Autolock lock(mMutex);
    return mThread->removeEventListener(callback);
}

void DispSync::setPeriod(nsecs_t period) {
    Mutex::Autolock lock(mMutex);
    mPeriod = period;
    mPhase = 0;
    mThread->updateModel(mPeriod, mPhase);
}

nsecs_t DispSync::getPeriod() {
    // lock mutex as mPeriod changes multiple times in updateModelLocked
    Mutex::Autolock lock(mMutex);
    return mPeriod;
}

void DispSync::updateModelLocked() {
    if (mNumResyncSamples >= MIN_RESYNC_SAMPLES_FOR_UPDATE) {
        nsecs_t durationSum = 0;
        for (size_t i = 1; i < mNumResyncSamples; i++) {
            size_t idx = (mFirstResyncSample + i) % MAX_RESYNC_SAMPLES;
            size_t prev = (idx + MAX_RESYNC_SAMPLES - 1) % MAX_RESYNC_SAMPLES;
            durationSum += mResyncSamples[idx] - mResyncSamples[prev];
        }

        mPeriod = durationSum / (mNumResyncSamples - 1);

        double sampleAvgX = 0;
        double sampleAvgY = 0;
        double scale = 2.0 * M_PI / double(mPeriod);
        for (size_t i = 0; i < mNumResyncSamples; i++) {
            size_t idx = (mFirstResyncSample + i) % MAX_RESYNC_SAMPLES;
            nsecs_t sample = mResyncSamples[idx];
            double samplePhase = double(sample % mPeriod) * scale;
            sampleAvgX += cos(samplePhase);
            sampleAvgY += sin(samplePhase);
        }

        sampleAvgX /= double(mNumResyncSamples);
        sampleAvgY /= double(mNumResyncSamples);

        mPhase = nsecs_t(atan2(sampleAvgY, sampleAvgX) / scale);

        if (mPhase < 0) {
            mPhase += mPeriod;
        }

        if (kTraceDetailedInfo) {
            ATRACE_INT64("DispSync:Period", mPeriod);
            ATRACE_INT64("DispSync:Phase", mPhase);
        }

        // Artificially inflate the period if requested.
        mPeriod += mPeriod * mRefreshSkipCount;

        mThread->updateModel(mPeriod, mPhase);
    }
}

void DispSync::updateErrorLocked() {
    if (mPeriod == 0) {
        return;
    }

    // Need to compare present fences against the un-adjusted refresh period,
    // since they might arrive between two events.
    nsecs_t period = mPeriod / (1 + mRefreshSkipCount);

    int numErrSamples = 0;
    nsecs_t sqErrSum = 0;

    for (size_t i = 0; i < NUM_PRESENT_SAMPLES; i++) {
        nsecs_t sample = mPresentTimes[i];
        if (sample > mPhase) {
            nsecs_t sampleErr = (sample - mPhase) % period;
            if (sampleErr > period / 2) {
                sampleErr -= period;
            }
            sqErrSum += sampleErr * sampleErr;
            numErrSamples++;
        }
    }

    if (numErrSamples > 0) {
        mError = sqErrSum / numErrSamples;
    } else {
        mError = 0;
    }

    if (kTraceDetailedInfo) {
        ATRACE_INT64("DispSync:Error", mError);
    }
}

void DispSync::resetErrorLocked() {
    mPresentSampleOffset = 0;
    mError = 0;
    for (size_t i = 0; i < NUM_PRESENT_SAMPLES; i++) {
        mPresentFences[i].clear();
        mPresentTimes[i] = 0;
    }
}

nsecs_t DispSync::computeNextRefresh(int periodOffset) const {
    Mutex::Autolock lock(mMutex);
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    return (((now - mPhase) / mPeriod) + periodOffset + 1) * mPeriod + mPhase;
}

void DispSync::dump(String8& result) const {
    Mutex::Autolock lock(mMutex);
    result.appendFormat("present fences are %s\n",
            kIgnorePresentFences ? "ignored" : "used");
    result.appendFormat("mPeriod: %" PRId64 " ns (%.3f fps; skipCount=%d)\n",
            mPeriod, 1000000000.0 / mPeriod, mRefreshSkipCount);
    result.appendFormat("mPhase: %" PRId64 " ns\n", mPhase);
    result.appendFormat("mError: %" PRId64 " ns (sqrt=%.1f)\n",
            mError, sqrt(mError));
    result.appendFormat("mNumResyncSamplesSincePresent: %d (limit %d)\n",
            mNumResyncSamplesSincePresent, MAX_RESYNC_SAMPLES_WITHOUT_PRESENT);
    result.appendFormat("mNumResyncSamples: %zd (max %d)\n",
            mNumResyncSamples, MAX_RESYNC_SAMPLES);

    result.appendFormat("mResyncSamples:\n");
    nsecs_t previous = -1;
    for (size_t i = 0; i < mNumResyncSamples; i++) {
        size_t idx = (mFirstResyncSample + i) % MAX_RESYNC_SAMPLES;
        nsecs_t sampleTime = mResyncSamples[idx];
        if (i == 0) {
            result.appendFormat("  %" PRId64 "\n", sampleTime);
        } else {
            result.appendFormat("  %" PRId64 " (+%" PRId64 ")\n",
                    sampleTime, sampleTime - previous);
        }
        previous = sampleTime;
    }

    result.appendFormat("mPresentFences / mPresentTimes [%d]:\n",
            NUM_PRESENT_SAMPLES);
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    previous = 0;
    for (size_t i = 0; i < NUM_PRESENT_SAMPLES; i++) {
        size_t idx = (i + mPresentSampleOffset) % NUM_PRESENT_SAMPLES;
        bool signaled = mPresentFences[idx] == NULL;
        nsecs_t presentTime = mPresentTimes[idx];
        if (!signaled) {
            result.appendFormat("  [unsignaled fence]\n");
        } else if (presentTime == 0) {
            result.appendFormat("  0\n");
        } else if (previous == 0) {
            result.appendFormat("  %" PRId64 "  (%.3f ms ago)\n", presentTime,
                    (now - presentTime) / 1000000.0);
        } else {
            result.appendFormat("  %" PRId64 " (+%" PRId64 " / %.3f)  (%.3f ms ago)\n",
                    presentTime, presentTime - previous,
                    (presentTime - previous) / (double) mPeriod,
                    (now - presentTime) / 1000000.0);
        }
        previous = presentTime;
    }

    result.appendFormat("current monotonic time: %" PRId64 "\n", now);
}

} // namespace android
