/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "DummyConsumer.h"

#include <gtest/gtest.h>

#include <binder/IMemory.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/BufferItemConsumer.h>
#include <ui/Rect.h>
#include <utils/String8.h>

#include <private/gui/ComposerService.h>
#include <binder/ProcessState.h>

namespace android {

class SurfaceTest : public ::testing::Test {
protected:

    SurfaceTest() {
        ProcessState::self()->startThreadPool();
    }

    virtual void SetUp() {
        mComposerClient = new SurfaceComposerClient;
        ASSERT_EQ(NO_ERROR, mComposerClient->initCheck());

        mSurfaceControl = mComposerClient->createSurface(
                String8("Test Surface"), 32, 32, PIXEL_FORMAT_RGBA_8888, 0);

        ASSERT_TRUE(mSurfaceControl != NULL);
        ASSERT_TRUE(mSurfaceControl->isValid());

        SurfaceComposerClient::openGlobalTransaction();
        ASSERT_EQ(NO_ERROR, mSurfaceControl->setLayer(0x7fffffff));
        ASSERT_EQ(NO_ERROR, mSurfaceControl->show());
        SurfaceComposerClient::closeGlobalTransaction();

        mSurface = mSurfaceControl->getSurface();
        ASSERT_TRUE(mSurface != NULL);
    }

    virtual void TearDown() {
        mComposerClient->dispose();
    }

    sp<Surface> mSurface;
    sp<SurfaceComposerClient> mComposerClient;
    sp<SurfaceControl> mSurfaceControl;
};

TEST_F(SurfaceTest, QueuesToWindowComposerIsTrueWhenVisible) {
    sp<ANativeWindow> anw(mSurface);
    int result = -123;
    int err = anw->query(anw.get(), NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER,
            &result);
    EXPECT_EQ(NO_ERROR, err);
    EXPECT_EQ(1, result);
}

TEST_F(SurfaceTest, QueuesToWindowComposerIsTrueWhenPurgatorized) {
    mSurfaceControl.clear();

    sp<ANativeWindow> anw(mSurface);
    int result = -123;
    int err = anw->query(anw.get(), NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER,
            &result);
    EXPECT_EQ(NO_ERROR, err);
    EXPECT_EQ(1, result);
}

// This test probably doesn't belong here.
TEST_F(SurfaceTest, ScreenshotsOfProtectedBuffersSucceed) {
    sp<ANativeWindow> anw(mSurface);

    // Verify the screenshot works with no protected buffers.
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);
    sp<CpuConsumer> cpuConsumer = new CpuConsumer(consumer, 1);
    sp<ISurfaceComposer> sf(ComposerService::getComposerService());
    sp<IBinder> display(sf->getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain));
    ASSERT_EQ(NO_ERROR, sf->captureScreen(display, producer, Rect(),
            64, 64, 0, 0x7fffffff, false));

    // Set the PROTECTED usage bit and verify that the screenshot fails.  Note
    // that we need to dequeue a buffer in order for it to actually get
    // allocated in SurfaceFlinger.
    ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(),
            GRALLOC_USAGE_PROTECTED));
    ASSERT_EQ(NO_ERROR, native_window_set_buffer_count(anw.get(), 3));
    ANativeWindowBuffer* buf = 0;

    status_t err = native_window_dequeue_buffer_and_wait(anw.get(), &buf);
    if (err) {
        // we could fail if GRALLOC_USAGE_PROTECTED is not supported.
        // that's okay as long as this is the reason for the failure.
        // try again without the GRALLOC_USAGE_PROTECTED bit.
        ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(), 0));
        ASSERT_EQ(NO_ERROR, native_window_dequeue_buffer_and_wait(anw.get(),
                &buf));
        return;
    }
    ASSERT_EQ(NO_ERROR, anw->cancelBuffer(anw.get(), buf, -1));

    for (int i = 0; i < 4; i++) {
        // Loop to make sure SurfaceFlinger has retired a protected buffer.
        ASSERT_EQ(NO_ERROR, native_window_dequeue_buffer_and_wait(anw.get(),
                &buf));
        ASSERT_EQ(NO_ERROR, anw->queueBuffer(anw.get(), buf, -1));
    }
    ASSERT_EQ(NO_ERROR, sf->captureScreen(display, producer, Rect(),
            64, 64, 0, 0x7fffffff, false));
}

TEST_F(SurfaceTest, ConcreteTypeIsSurface) {
    sp<ANativeWindow> anw(mSurface);
    int result = -123;
    int err = anw->query(anw.get(), NATIVE_WINDOW_CONCRETE_TYPE, &result);
    EXPECT_EQ(NO_ERROR, err);
    EXPECT_EQ(NATIVE_WINDOW_SURFACE, result);
}

TEST_F(SurfaceTest, QueryConsumerUsage) {
    const int TEST_USAGE_FLAGS =
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_RENDER;
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);
    sp<BufferItemConsumer> c = new BufferItemConsumer(consumer,
            TEST_USAGE_FLAGS);
    sp<Surface> s = new Surface(producer);

    sp<ANativeWindow> anw(s);

    int flags = -1;
    int err = anw->query(anw.get(), NATIVE_WINDOW_CONSUMER_USAGE_BITS, &flags);

    ASSERT_EQ(NO_ERROR, err);
    ASSERT_EQ(TEST_USAGE_FLAGS, flags);
}

TEST_F(SurfaceTest, QueryDefaultBuffersDataSpace) {
    const android_dataspace TEST_DATASPACE = HAL_DATASPACE_SRGB;
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);
    sp<CpuConsumer> cpuConsumer = new CpuConsumer(consumer, 1);

    cpuConsumer->setDefaultBufferDataSpace(TEST_DATASPACE);

    sp<Surface> s = new Surface(producer);

    sp<ANativeWindow> anw(s);

    android_dataspace dataSpace;

    int err = anw->query(anw.get(), NATIVE_WINDOW_DEFAULT_DATASPACE,
            reinterpret_cast<int*>(&dataSpace));

    ASSERT_EQ(NO_ERROR, err);
    ASSERT_EQ(TEST_DATASPACE, dataSpace);
}

TEST_F(SurfaceTest, SettingGenerationNumber) {
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);
    sp<CpuConsumer> cpuConsumer = new CpuConsumer(consumer, 1);
    sp<Surface> surface = new Surface(producer);
    sp<ANativeWindow> window(surface);

    // Allocate a buffer with a generation number of 0
    ANativeWindowBuffer* buffer;
    int fenceFd;
    ASSERT_EQ(NO_ERROR, window->dequeueBuffer(window.get(), &buffer, &fenceFd));
    ASSERT_EQ(NO_ERROR, window->cancelBuffer(window.get(), buffer, fenceFd));

    // Detach the buffer and check its generation number
    sp<GraphicBuffer> graphicBuffer;
    sp<Fence> fence;
    ASSERT_EQ(NO_ERROR, surface->detachNextBuffer(&graphicBuffer, &fence));
    ASSERT_EQ(0U, graphicBuffer->getGenerationNumber());

    ASSERT_EQ(NO_ERROR, surface->setGenerationNumber(1));
    buffer = static_cast<ANativeWindowBuffer*>(graphicBuffer.get());

    // This should change the generation number of the GraphicBuffer
    ASSERT_EQ(NO_ERROR, surface->attachBuffer(buffer));

    // Check that the new generation number sticks with the buffer
    ASSERT_EQ(NO_ERROR, window->cancelBuffer(window.get(), buffer, -1));
    ASSERT_EQ(NO_ERROR, window->dequeueBuffer(window.get(), &buffer, &fenceFd));
    graphicBuffer = static_cast<GraphicBuffer*>(buffer);
    ASSERT_EQ(1U, graphicBuffer->getGenerationNumber());
}

TEST_F(SurfaceTest, GetConsumerName) {
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);

    sp<DummyConsumer> dummyConsumer(new DummyConsumer);
    consumer->consumerConnect(dummyConsumer, false);
    consumer->setConsumerName(String8("TestConsumer"));

    sp<Surface> surface = new Surface(producer);
    sp<ANativeWindow> window(surface);
    native_window_api_connect(window.get(), NATIVE_WINDOW_API_CPU);

    EXPECT_STREQ("TestConsumer", surface->getConsumerName().string());
}

}
