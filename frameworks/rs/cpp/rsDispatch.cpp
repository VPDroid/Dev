/*
 * Copyright (C) 2011-2012 The Android Open Source Project
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
#define LOG_TAG "libDispatch"
#include <android/log.h>

#include "rsDispatch.h"
#include <dlfcn.h>

#define LOG_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "RS Dispatch", __VA_ARGS__);

bool loadSymbols(void* handle, dispatchTable& dispatchTab, int device_api) {
    //fucntion to set the native lib path for 64bit compat lib.
#ifdef __LP64__
    dispatchTab.SetNativeLibDir = (SetNativeLibDirFnPtr)dlsym(handle, "rsaContextSetNativeLibDir");
    if (dispatchTab.SetNativeLibDir == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.SetNativeLibDir");
        return false;
    }
#endif
    dispatchTab.AllocationGetType = (AllocationGetTypeFnPtr)dlsym(handle, "rsaAllocationGetType");
    if (dispatchTab.AllocationGetType == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationGetType");
        return false;
    }
    dispatchTab.TypeGetNativeData = (TypeGetNativeDataFnPtr)dlsym(handle, "rsaTypeGetNativeData");
    if (dispatchTab.TypeGetNativeData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.TypeGetNativeData");
        return false;
    }
    dispatchTab.ElementGetNativeData = (ElementGetNativeDataFnPtr)dlsym(handle, "rsaElementGetNativeData");
    if (dispatchTab.ElementGetNativeData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ElementGetNativeData");
        return false;
    }
    dispatchTab.ElementGetSubElements = (ElementGetSubElementsFnPtr)dlsym(handle, "rsaElementGetSubElements");
    if (dispatchTab.ElementGetSubElements == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ElementGetSubElements");
        return false;
    }
    dispatchTab.DeviceCreate = (DeviceCreateFnPtr)dlsym(handle, "rsDeviceCreate");
    if (dispatchTab.DeviceCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.DeviceCreate");
        return false;
    }
    dispatchTab.DeviceDestroy = (DeviceDestroyFnPtr)dlsym(handle, "rsDeviceDestroy");
    if (dispatchTab.DeviceDestroy == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.DeviceDestroy");
        return false;
    }
    dispatchTab.DeviceSetConfig = (DeviceSetConfigFnPtr)dlsym(handle, "rsDeviceSetConfig");
    if (dispatchTab.DeviceSetConfig == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.DeviceSetConfig");
        return false;
    }
    dispatchTab.ContextCreate = (ContextCreateFnPtr)dlsym(handle, "rsContextCreate");;
    if (dispatchTab.ContextCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextCreate");
        return false;
    }
    dispatchTab.GetName = (GetNameFnPtr)dlsym(handle, "rsaGetName");;
    if (dispatchTab.GetName == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.GetName");
        return false;
    }
    dispatchTab.ContextDestroy = (ContextDestroyFnPtr)dlsym(handle, "rsContextDestroy");
    if (dispatchTab.ContextDestroy == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextDestroy");
        return false;
    }
    dispatchTab.ContextGetMessage = (ContextGetMessageFnPtr)dlsym(handle, "rsContextGetMessage");
    if (dispatchTab.ContextGetMessage == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextGetMessage");
        return false;
    }
    dispatchTab.ContextPeekMessage = (ContextPeekMessageFnPtr)dlsym(handle, "rsContextPeekMessage");
    if (dispatchTab.ContextPeekMessage == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextPeekMessage");
        return false;
    }
    dispatchTab.ContextSendMessage = (ContextSendMessageFnPtr)dlsym(handle, "rsContextSendMessage");
    if (dispatchTab.ContextSendMessage == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextSendMessage");
        return false;
    }
    dispatchTab.ContextInitToClient = (ContextInitToClientFnPtr)dlsym(handle, "rsContextInitToClient");
    if (dispatchTab.ContextInitToClient == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextInitToClient");
        return false;
    }
    dispatchTab.ContextDeinitToClient = (ContextDeinitToClientFnPtr)dlsym(handle, "rsContextDeinitToClient");
    if (dispatchTab.ContextDeinitToClient == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextDeinitToClient");
        return false;
    }
    dispatchTab.TypeCreate = (TypeCreateFnPtr)dlsym(handle, "rsTypeCreate");
    if (dispatchTab.TypeCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.TypeCreate");
        return false;
    }
    dispatchTab.AllocationCreateTyped = (AllocationCreateTypedFnPtr)dlsym(handle, "rsAllocationCreateTyped");
    if (dispatchTab.AllocationCreateTyped == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCreateTyped");
        return false;
    }
    dispatchTab.AllocationCreateFromBitmap = (AllocationCreateFromBitmapFnPtr)dlsym(handle, "rsAllocationCreateFromBitmap");
    if (dispatchTab.AllocationCreateFromBitmap == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCreateFromBitmap");
        return false;
    }
    dispatchTab.AllocationCubeCreateFromBitmap = (AllocationCubeCreateFromBitmapFnPtr)dlsym(handle, "rsAllocationCubeCreateFromBitmap");
    if (dispatchTab.AllocationCubeCreateFromBitmap == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCubeCreateFromBitmap");
        return false;
    }
    dispatchTab.AllocationGetSurface = (AllocationGetSurfaceFnPtr)dlsym(handle, "rsAllocationGetSurface");
    if (dispatchTab.AllocationGetSurface == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationGetSurface");
        return false;
    }
    dispatchTab.AllocationSetSurface = (AllocationSetSurfaceFnPtr)dlsym(handle, "rsAllocationSetSurface");
    if (dispatchTab.AllocationSetSurface == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationSetSurface");
        return false;
    }
    dispatchTab.ContextFinish = (ContextFinishFnPtr)dlsym(handle, "rsContextFinish");
    if (dispatchTab.ContextFinish == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextFinish");
        return false;
    }
    dispatchTab.ContextDump = (ContextDumpFnPtr)dlsym(handle, "rsContextDump");
    if (dispatchTab.ContextDump == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextDump");
        return false;
    }
    dispatchTab.ContextSetPriority = (ContextSetPriorityFnPtr)dlsym(handle, "rsContextSetPriority");
    if (dispatchTab.ContextSetPriority == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ContextSetPriority");
        return false;
    }
    dispatchTab.AssignName = (AssignNameFnPtr)dlsym(handle, "rsAssignName");
    if (dispatchTab.AssignName == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AssignName");
        return false;
    }
    dispatchTab.ObjDestroy = (ObjDestroyFnPtr)dlsym(handle, "rsObjDestroy");
    if (dispatchTab.ObjDestroy == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ObjDestroy");
        return false;
    }
    dispatchTab.ElementCreate = (ElementCreateFnPtr)dlsym(handle, "rsElementCreate");
    if (dispatchTab.ElementCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ElementCreate");
        return false;
    }
    dispatchTab.ElementCreate2 = (ElementCreate2FnPtr)dlsym(handle, "rsElementCreate2");
    if (dispatchTab.ElementCreate2 == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ElementCreate2");
        return false;
    }
    dispatchTab.AllocationCopyToBitmap = (AllocationCopyToBitmapFnPtr)dlsym(handle, "rsAllocationCopyToBitmap");
    if (dispatchTab.AllocationCopyToBitmap == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCopyToBitmap");
        return false;
    }
    dispatchTab.Allocation1DData = (Allocation1DDataFnPtr)dlsym(handle, "rsAllocation1DData");
    if (dispatchTab.Allocation1DData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation1DData");
        return false;
    }
    dispatchTab.Allocation1DElementData = (Allocation1DElementDataFnPtr)dlsym(handle, "rsAllocation1DElementData");
    if (dispatchTab.Allocation1DElementData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation1DElementData");
        return false;
    }
    dispatchTab.Allocation2DData = (Allocation2DDataFnPtr)dlsym(handle, "rsAllocation2DData");
    if (dispatchTab.Allocation2DData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation2DData");
        return false;
    }
    dispatchTab.Allocation3DData = (Allocation3DDataFnPtr)dlsym(handle, "rsAllocation3DData");
    if (dispatchTab.Allocation3DData == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation3DData");
        return false;
    }
    dispatchTab.AllocationGenerateMipmaps = (AllocationGenerateMipmapsFnPtr)dlsym(handle, "rsAllocationGenerateMipmaps");
    if (dispatchTab.AllocationGenerateMipmaps == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationGenerateMipmaps");
        return false;
    }
    dispatchTab.AllocationRead = (AllocationReadFnPtr)dlsym(handle, "rsAllocationRead");
    if (dispatchTab.AllocationRead == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationRead");
        return false;
    }
    dispatchTab.Allocation1DRead = (Allocation1DReadFnPtr)dlsym(handle, "rsAllocation1DRead");
    if (dispatchTab.Allocation1DRead == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation1DRead");
        return false;
    }
    dispatchTab.Allocation2DRead = (Allocation2DReadFnPtr)dlsym(handle, "rsAllocation2DRead");
    if (dispatchTab.Allocation2DRead == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.Allocation2DRead");
        return false;
    }
    dispatchTab.AllocationSyncAll = (AllocationSyncAllFnPtr)dlsym(handle, "rsAllocationSyncAll");
    if (dispatchTab.AllocationSyncAll == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationSyncAll");
        return false;
    }
    dispatchTab.AllocationResize1D = (AllocationResize1DFnPtr)dlsym(handle, "rsAllocationResize1D");
    if (dispatchTab.AllocationResize1D == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationResize1D");
        return false;
    }
    dispatchTab.AllocationCopy2DRange = (AllocationCopy2DRangeFnPtr)dlsym(handle, "rsAllocationCopy2DRange");
    if (dispatchTab.AllocationCopy2DRange == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCopy2DRange");
        return false;
    }
    dispatchTab.AllocationCopy3DRange = (AllocationCopy3DRangeFnPtr)dlsym(handle, "rsAllocationCopy3DRange");
    if (dispatchTab.AllocationCopy3DRange == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationCopy3DRange");
        return false;
    }
    dispatchTab.SamplerCreate = (SamplerCreateFnPtr)dlsym(handle, "rsSamplerCreate");
    if (dispatchTab.SamplerCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.SamplerCreate");
        return false;
    }
    dispatchTab.ScriptBindAllocation = (ScriptBindAllocationFnPtr)dlsym(handle, "rsScriptBindAllocation");
    if (dispatchTab.ScriptBindAllocation == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptBindAllocation");
        return false;
    }
    dispatchTab.ScriptSetTimeZone = (ScriptSetTimeZoneFnPtr)dlsym(handle, "rsScriptSetTimeZone");
    if (dispatchTab.ScriptSetTimeZone == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetTimeZone");
        return false;
    }
    dispatchTab.ScriptInvoke = (ScriptInvokeFnPtr)dlsym(handle, "rsScriptInvoke");
    if (dispatchTab.ScriptInvoke == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptInvoke");
        return false;
    }
    dispatchTab.ScriptInvokeV = (ScriptInvokeVFnPtr)dlsym(handle, "rsScriptInvokeV");
    if (dispatchTab.ScriptInvokeV == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptInvokeV");
        return false;
    }
    dispatchTab.ScriptForEach = (ScriptForEachFnPtr)dlsym(handle, "rsScriptForEach");
    if (dispatchTab.ScriptForEach == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptForEach");
        return false;
    }
    dispatchTab.ScriptSetVarI = (ScriptSetVarIFnPtr)dlsym(handle, "rsScriptSetVarI");
    if (dispatchTab.ScriptSetVarI == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarI");
        return false;
    }
    dispatchTab.ScriptSetVarObj = (ScriptSetVarObjFnPtr)dlsym(handle, "rsScriptSetVarObj");
    if (dispatchTab.ScriptSetVarObj == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarObj");
        return false;
    }
    dispatchTab.ScriptSetVarJ = (ScriptSetVarJFnPtr)dlsym(handle, "rsScriptSetVarJ");
    if (dispatchTab.ScriptSetVarJ == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarJ");
        return false;
    }
    dispatchTab.ScriptSetVarF = (ScriptSetVarFFnPtr)dlsym(handle, "rsScriptSetVarF");
    if (dispatchTab.ScriptSetVarF == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarF");
        return false;
    }
    dispatchTab.ScriptSetVarD = (ScriptSetVarDFnPtr)dlsym(handle, "rsScriptSetVarD");
    if (dispatchTab.ScriptSetVarD == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarD");
        return false;
    }
    dispatchTab.ScriptSetVarV = (ScriptSetVarVFnPtr)dlsym(handle, "rsScriptSetVarV");
    if (dispatchTab.ScriptSetVarV == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarV");
        return false;
    }
    dispatchTab.ScriptGetVarV = (ScriptGetVarVFnPtr)dlsym(handle, "rsScriptGetVarV");
    if (dispatchTab.ScriptGetVarV == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptGetVarV");
        return false;
    }
    dispatchTab.ScriptSetVarVE = (ScriptSetVarVEFnPtr)dlsym(handle, "rsScriptSetVarVE");
    if (dispatchTab.ScriptSetVarVE == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptSetVarVE");
        return false;
    }
    dispatchTab.ScriptCCreate = (ScriptCCreateFnPtr)dlsym(handle, "rsScriptCCreate");
    if (dispatchTab.ScriptCCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptCCreate");
        return false;
    }
    dispatchTab.ScriptIntrinsicCreate = (ScriptIntrinsicCreateFnPtr)dlsym(handle, "rsScriptIntrinsicCreate");
    if (dispatchTab.ScriptIntrinsicCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptIntrinsicCreate");
        return false;
    }
    dispatchTab.ScriptKernelIDCreate = (ScriptKernelIDCreateFnPtr)dlsym(handle, "rsScriptKernelIDCreate");
    if (dispatchTab.ScriptKernelIDCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptKernelIDCreate");
        return false;
    }
    dispatchTab.ScriptFieldIDCreate = (ScriptFieldIDCreateFnPtr)dlsym(handle, "rsScriptFieldIDCreate");
    if (dispatchTab.ScriptFieldIDCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptFieldIDCreate");
        return false;
    }
    dispatchTab.ScriptGroupCreate = (ScriptGroupCreateFnPtr)dlsym(handle, "rsScriptGroupCreate");
    if (dispatchTab.ScriptGroupCreate == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptGroupCreate");
        return false;
    }
    dispatchTab.ScriptGroupSetOutput = (ScriptGroupSetOutputFnPtr)dlsym(handle, "rsScriptGroupSetOutput");
    if (dispatchTab.ScriptGroupSetOutput == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptGroupSetOutput");
        return false;
    }
    dispatchTab.ScriptGroupSetInput = (ScriptGroupSetInputFnPtr)dlsym(handle, "rsScriptGroupSetInput");
    if (dispatchTab.ScriptGroupSetInput == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptGroupSetInput");
        return false;
    }
    dispatchTab.ScriptGroupExecute = (ScriptGroupExecuteFnPtr)dlsym(handle, "rsScriptGroupExecute");
    if (dispatchTab.ScriptGroupExecute == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.ScriptGroupExecute");
        return false;
    }
    dispatchTab.AllocationIoSend = (AllocationIoSendFnPtr)dlsym(handle, "rsAllocationIoSend");
    if (dispatchTab.AllocationIoSend == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationIoSend");
        return false;
    }
    dispatchTab.AllocationIoReceive = (AllocationIoReceiveFnPtr)dlsym(handle, "rsAllocationIoReceive");
    if (dispatchTab.AllocationIoReceive == NULL) {
        LOG_ERR("Couldn't initialize dispatchTab.AllocationIoReceive");
        return false;
    }
    // API_21 functions
    if (device_api >= 21) {
        dispatchTab.AllocationGetPointer = (AllocationGetPointerFnPtr)dlsym(handle, "rsAllocationGetPointer");
        if (dispatchTab.AllocationGetPointer == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.AllocationGetPointer");
            return false;
        }
    }
    // API_23 functions
    if (device_api >= 23) {
        //ScriptGroup V2 functions
        dispatchTab.ScriptInvokeIDCreate = (ScriptInvokeIDCreateFnPtr)dlsym(handle, "rsScriptInvokeIDCreate");
        if (dispatchTab.ScriptInvokeIDCreate == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ScriptInvokeIDCreate");
            return false;
        }
        dispatchTab.ClosureCreate = (ClosureCreateFnPtr)dlsym(handle, "rsClosureCreate");
        if (dispatchTab.ClosureCreate == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ClosureCreate");
            return false;
        }
        dispatchTab.InvokeClosureCreate = (InvokeClosureCreateFnPtr)dlsym(handle, "rsInvokeClosureCreate");
        if (dispatchTab.InvokeClosureCreate == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.InvokeClosureCreate");
            return false;
        }
        dispatchTab.ClosureSetArg = (ClosureSetArgFnPtr)dlsym(handle, "rsClosureSetArg");
        if (dispatchTab.ClosureSetArg == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ClosureSetArg");
            return false;
        }
        dispatchTab.ClosureSetGlobal = (ClosureSetGlobalFnPtr)dlsym(handle, "rsClosureSetGlobal");
        if (dispatchTab.ClosureSetGlobal == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ClosureSetGlobal");
            return false;
        }
        dispatchTab.ScriptGroup2Create = (ScriptGroup2CreateFnPtr)dlsym(handle, "rsScriptGroup2Create");
        if (dispatchTab.ScriptGroup2Create == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ScriptGroup2Create");
            return false;
        }
        dispatchTab.AllocationElementData = (AllocationElementDataFnPtr)dlsym(handle, "rsAllocationElementData");
        if (dispatchTab.AllocationElementData == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.AllocationElementData");
            return false;
        }
        dispatchTab.AllocationElementRead = (AllocationElementReadFnPtr)dlsym(handle, "rsAllocationElementRead");
        if (dispatchTab.AllocationElementRead == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.AllocationElementRead");
            return false;
        }
        dispatchTab.Allocation3DRead = (Allocation3DReadFnPtr)dlsym(handle, "rsAllocation3DRead");
        if (dispatchTab.Allocation3DRead == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.Allocation3DRead");
            return false;
        }
        dispatchTab.ScriptForEachMulti = (ScriptForEachMultiFnPtr)dlsym(handle, "rsScriptForEachMulti");
        if (dispatchTab.ScriptForEachMulti == NULL) {
            LOG_ERR("Couldn't initialize dispatchTab.ScriptForEachMulti");
            return false;
        }
    }

    return true;

}


bool loadIOSuppSyms(void* handleIO, ioSuppDT& ioDispatch){
    ioDispatch.sAllocationSetSurface = (sAllocationSetSurfaceFnPtr)dlsym(handleIO, "AllocationSetSurface");
    if (ioDispatch.sAllocationSetSurface == NULL) {
        LOG_ERR("Couldn't initialize ioDispatch.sAllocationSetSurface");
        return false;
    }
    return true;
}
