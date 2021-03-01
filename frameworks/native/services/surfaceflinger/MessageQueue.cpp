/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>

#include <binder/IPCThreadState.h>

#include <utils/threads.h>
#include <utils/Timers.h>
#include <utils/Log.h>

#include <gui/IDisplayEventConnection.h>
#include <gui/BitTube.h>

#include "MessageQueue.h"
#include "EventThread.h"
#include "SurfaceFlinger.h"

namespace android {

// ---------------------------------------------------------------------------

MessageBase::MessageBase()
    : MessageHandler() {
}

MessageBase::~MessageBase() {
}

void MessageBase::handleMessage(const Message&) {
    this->handler();
    barrier.open();
};

// ---------------------------------------------------------------------------

void MessageQueue::Handler::dispatchRefresh() {
    if ((android_atomic_or(eventMaskRefresh, &mEventMask) & eventMaskRefresh) == 0) {
        mQueue.mLooper->sendMessage(this, Message(MessageQueue::REFRESH));
    }
}

void MessageQueue::Handler::dispatchInvalidate() {
    if ((android_atomic_or(eventMaskInvalidate, &mEventMask) & eventMaskInvalidate) == 0) {
        mQueue.mLooper->sendMessage(this, Message(MessageQueue::INVALIDATE));
    }
}

void MessageQueue::Handler::dispatchTransaction() {
    if ((android_atomic_or(eventMaskTransaction, &mEventMask) & eventMaskTransaction) == 0) {
        mQueue.mLooper->sendMessage(this, Message(MessageQueue::TRANSACTION));
    }
}

void MessageQueue::Handler::handleMessage(const Message& message) {
    switch (message.what) {
        case INVALIDATE:
            android_atomic_and(~eventMaskInvalidate, &mEventMask);
            mQueue.mFlinger->onMessageReceived(message.what);
            break;
        case REFRESH:
            android_atomic_and(~eventMaskRefresh, &mEventMask);
            mQueue.mFlinger->onMessageReceived(message.what);
            break;
        case TRANSACTION:
            android_atomic_and(~eventMaskTransaction, &mEventMask);
            mQueue.mFlinger->onMessageReceived(message.what);
            break;
    }
}

// ---------------------------------------------------------------------------

MessageQueue::MessageQueue()
{
}

MessageQueue::~MessageQueue() {
}

void MessageQueue::init(const sp<SurfaceFlinger>& flinger)
{
    mFlinger = flinger;
    mLooper = new Looper(true);
    mHandler = new Handler(*this);
}

void MessageQueue::setEventThread(const sp<EventThread>& eventThread)
{
    mEventThread = eventThread;
    mEvents = eventThread->createEventConnection();
    mEventTube = mEvents->getDataChannel();
    mLooper->addFd(mEventTube->getFd(), 0, Looper::EVENT_INPUT,
            MessageQueue::cb_eventReceiver, this);
}

void MessageQueue::waitMessage() {
    do {
        IPCThreadState::self()->flushCommands();
        int32_t ret = mLooper->pollOnce(-1);
        switch (ret) {
            case Looper::POLL_WAKE:
            case Looper::POLL_CALLBACK:
                continue;
            case Looper::POLL_ERROR:
                ALOGE("Looper::POLL_ERROR");
            case Looper::POLL_TIMEOUT:
                // timeout (should not happen)
                continue;
            default:
                // should not happen
                ALOGE("Looper::pollOnce() returned unknown status %d", ret);
                continue;
        }
    } while (true);
}

status_t MessageQueue::postMessage(
        const sp<MessageBase>& messageHandler, nsecs_t relTime)
{
    const Message dummyMessage;
    if (relTime > 0) {
        mLooper->sendMessageDelayed(relTime, messageHandler, dummyMessage);
    } else {
        mLooper->sendMessage(messageHandler, dummyMessage);
    }
    return NO_ERROR;
}


/* when INVALIDATE_ON_VSYNC is set SF only processes
 * buffer updates on VSYNC and performs a refresh immediately
 * after.
 *
 * when INVALIDATE_ON_VSYNC is set to false, SF will instead
 * perform the buffer updates immediately, but the refresh only
 * at the next VSYNC.
 * THIS MODE IS BUGGY ON GALAXY NEXUS AND WILL CAUSE HANGS
 */
#define INVALIDATE_ON_VSYNC 1

void MessageQueue::invalidateTransactionNow() {
    mHandler->dispatchTransaction();
}

void MessageQueue::invalidate() {
#if INVALIDATE_ON_VSYNC
    mEvents->requestNextVsync();
#else
    mHandler->dispatchInvalidate();
#endif
}

void MessageQueue::refresh() {
#if INVALIDATE_ON_VSYNC
    mHandler->dispatchRefresh();
#else
    mEvents->requestNextVsync();
#endif
}

int MessageQueue::cb_eventReceiver(int fd, int events, void* data) {
    MessageQueue* queue = reinterpret_cast<MessageQueue *>(data);
    return queue->eventReceiver(fd, events);
}

int MessageQueue::eventReceiver(int /*fd*/, int /*events*/) {
    ssize_t n;
    DisplayEventReceiver::Event buffer[8];
    while ((n = DisplayEventReceiver::getEvents(mEventTube, buffer, 8)) > 0) {
        for (int i=0 ; i<n ; i++) {
            if (buffer[i].header.type == DisplayEventReceiver::DISPLAY_EVENT_VSYNC) {
#if INVALIDATE_ON_VSYNC
                mHandler->dispatchInvalidate();
#else
                mHandler->dispatchRefresh();
#endif
                break;
            }
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------

}; // namespace android
