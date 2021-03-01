/*
 * Copyright (C) 2007 The Android Open Source Project
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

// tag as surfaceflinger
#define LOG_TAG "SurfaceFlinger"

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

#include <binder/Parcel.h>
#include <binder/IMemory.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>

#include <ui/Point.h>
#include <ui/Rect.h>

#include <gui/IGraphicBufferProducer.h>
#include <gui/ISurfaceComposerClient.h>
#include <private/gui/LayerState.h>

// ---------------------------------------------------------------------------

namespace android {

enum {
    CREATE_SURFACE = IBinder::FIRST_CALL_TRANSACTION,
    DESTROY_SURFACE,
    CLEAR_LAYER_FRAME_STATS,
    GET_LAYER_FRAME_STATS
};

class BpSurfaceComposerClient : public BpInterface<ISurfaceComposerClient>
{
public:
    BpSurfaceComposerClient(const sp<IBinder>& impl)
        : BpInterface<ISurfaceComposerClient>(impl) {
    }

    virtual ~BpSurfaceComposerClient();

    virtual status_t createSurface(const String8& name, uint32_t width,
            uint32_t height, PixelFormat format, uint32_t flags,
            sp<IBinder>* handle,
            sp<IGraphicBufferProducer>* gbp) {

        char systemname[PROPERTY_VALUE_MAX];
        property_get("persist.sys.vm.name", systemname, "");

        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposerClient::getInterfaceDescriptor());
        data.writeString8(name);
        data.writeString8(String8(systemname));
        data.writeUint32(width);
        data.writeUint32(height);
        data.writeInt32(static_cast<int32_t>(format));
        data.writeUint32(flags);
        remote()->transact(CREATE_SURFACE, data, &reply);
        *handle = reply.readStrongBinder();
        *gbp = interface_cast<IGraphicBufferProducer>(reply.readStrongBinder());
        return reply.readInt32();
    }

    virtual status_t destroySurface(const sp<IBinder>& handle) {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposerClient::getInterfaceDescriptor());
        data.writeStrongBinder(handle);
        remote()->transact(DESTROY_SURFACE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t clearLayerFrameStats(const sp<IBinder>& handle) const {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposerClient::getInterfaceDescriptor());
        data.writeStrongBinder(handle);
        remote()->transact(CLEAR_LAYER_FRAME_STATS, data, &reply);
        return reply.readInt32();
    }

    virtual status_t getLayerFrameStats(const sp<IBinder>& handle, FrameStats* outStats) const {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposerClient::getInterfaceDescriptor());
        data.writeStrongBinder(handle);
        remote()->transact(GET_LAYER_FRAME_STATS, data, &reply);
        reply.read(*outStats);
        return reply.readInt32();
    }
};

// Out-of-line virtual method definition to trigger vtable emission in this
// translation unit (see clang warning -Wweak-vtables)
BpSurfaceComposerClient::~BpSurfaceComposerClient() {}

IMPLEMENT_META_INTERFACE(SurfaceComposerClient, "android.ui.ISurfaceComposerClient");

// ----------------------------------------------------------------------

status_t BnSurfaceComposerClient::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
     switch(code) {
        case CREATE_SURFACE: {
            CHECK_INTERFACE(ISurfaceComposerClient, data, reply);
            String8 name = data.readString8();
            String8 systemname = data.readString8();
            uint32_t width = data.readUint32();
            uint32_t height = data.readUint32();
            PixelFormat format = static_cast<PixelFormat>(data.readInt32());
            uint32_t createFlags = data.readUint32();
            sp<IBinder> handle;
            sp<IGraphicBufferProducer> gbp;
            status_t result = createSurfaceX(name, systemname,  width, height, format,
                    createFlags, &handle, &gbp);
            reply->writeStrongBinder(handle);
            reply->writeStrongBinder(IInterface::asBinder(gbp));
            reply->writeInt32(result);
            return NO_ERROR;
        }
        case DESTROY_SURFACE: {
            CHECK_INTERFACE(ISurfaceComposerClient, data, reply);
            reply->writeInt32(destroySurface( data.readStrongBinder() ) );
            return NO_ERROR;
        }
       case CLEAR_LAYER_FRAME_STATS: {
            CHECK_INTERFACE(ISurfaceComposerClient, data, reply);
            sp<IBinder> handle = data.readStrongBinder();
            status_t result = clearLayerFrameStats(handle);
            reply->writeInt32(result);
            return NO_ERROR;
        }
        case GET_LAYER_FRAME_STATS: {
            CHECK_INTERFACE(ISurfaceComposerClient, data, reply);
            sp<IBinder> handle = data.readStrongBinder();
            FrameStats stats;
            status_t result = getLayerFrameStats(handle, &stats);
            reply->write(stats);
            reply->writeInt32(result);
            return NO_ERROR;
        }
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

}; // namespace android
