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

#ifndef ANDROID_RS_THREAD_IO_H
#define ANDROID_RS_THREAD_IO_H

#include "rsUtils.h"
#include "rsFifoSocket.h"

// ---------------------------------------------------------------------------
namespace android {
namespace renderscript {

class Context;

class ThreadIO {
public:
    ThreadIO();
    ~ThreadIO();

    void init();
    void shutdown();

    size_t getMaxInlineSize() {
        return mMaxInlineSize;
    }
    bool isPureFifo() {
        return mPureFifo;
    }

    // Plays back commands from the client.
    // Returns true if any commands were processed.
    bool playCoreCommands(Context *con, int waitFd);

    void setTimeoutCallback(void (*)(void *), void *, uint64_t timeout);

    void * coreHeader(uint32_t, size_t dataLen);
    void coreCommit();

    void coreSetReturn(const void *data, size_t dataLen);
    void coreGetReturn(void *data, size_t dataLen);
    void coreWrite(const void *data, size_t len);
    void coreRead(void *data, size_t len);

    void asyncSetReturn(const void *data, size_t dataLen);
    void asyncGetReturn(void *data, size_t dataLen);
    void asyncWrite(const void *data, size_t len);
    void asyncRead(void *data, size_t len);


    RsMessageToClientType getClientHeader(size_t *receiveLen, uint32_t *usrID);
    RsMessageToClientType getClientPayload(void *data, size_t *receiveLen, uint32_t *subID, size_t bufferLen);
    bool sendToClient(RsMessageToClientType cmdID, uint32_t usrID, const void *data, size_t dataLen, bool waitForSpace);
    void clientShutdown();


protected:
    typedef struct CoreCmdHeaderRec {
        uint32_t cmdID;
        uint32_t bytes;
    } CoreCmdHeader;
    typedef struct ClientCmdHeaderRec {
        uint32_t cmdID;
        uint32_t bytes;
        uint32_t userID;
    } ClientCmdHeader;
    ClientCmdHeader mLastClientHeader;

    bool mRunning;
    bool mPureFifo;
    size_t mMaxInlineSize;

    FifoSocket mToClient;
    FifoSocket mToCore;

    intptr_t mToCoreRet;

    size_t mSendLen;
    uint8_t mSendBuffer[2 * 1024] __attribute__((aligned(sizeof(double))));

};


}
}
#endif

