ContextDestroy {
    direct
}

ContextGetMessage {
    direct
    param void *data
    param size_t *receiveLen
    param uint32_t *usrID
    ret RsMessageToClientType
}

ContextPeekMessage {
    direct
    param size_t *receiveLen
    param uint32_t *usrID
    ret RsMessageToClientType
}

ContextSendMessage {
    param uint32_t id
    param const uint8_t *data
}

ContextInitToClient {
    direct
}

ContextDeinitToClient {
    direct
}

ContextSetCacheDir {
    direct
    param const char * cacheDir
}

TypeCreate {
    direct
    param RsElement e
    param uint32_t dimX
    param uint32_t dimY
    param uint32_t dimZ
    param bool mipmaps
    param bool faces
    param uint32_t yuv
    ret RsType
}

TypeCreate2 {
    direct
    param const RsTypeCreateParams *dat
    ret RsType
}

AllocationCreateTyped {
    direct
    param RsType vtype
    param RsAllocationMipmapControl mipmaps
    param uint32_t usages
    param uintptr_t ptr
    ret RsAllocation
}

AllocationCreateFromBitmap {
    direct
    param RsType vtype
    param RsAllocationMipmapControl mipmaps
    param const void *data
    param uint32_t usages
    ret RsAllocation
}

AllocationCubeCreateFromBitmap {
    direct
    param RsType vtype
    param RsAllocationMipmapControl mipmaps
    param const void *data
    param uint32_t usages
    ret RsAllocation
}

AllocationGetSurface {
    param RsAllocation alloc
    sync
    ret RsNativeWindow
}

AllocationSetSurface {
    param RsAllocation alloc
    param RsNativeWindow sur
    sync
    }

AllocationAdapterCreate {
    direct
    param RsType vtype
    param RsAllocation baseAlloc
    ret RsAllocation
}

AllocationAdapterOffset {
    param RsAllocation alloc
    param const uint32_t *offsets
}

ContextFinish {
    sync
    }

ContextDump {
    param int32_t bits
}

ContextSetPriority {
    param int32_t priority
    }

ContextDestroyWorker {
    sync
}

AssignName {
    param RsObjectBase obj
    param const char *name
    }

ObjDestroy {
    param RsAsyncVoidPtr objPtr
    handcodeApi
    }

ElementCreate {
        direct
    param RsDataType mType
    param RsDataKind mKind
    param bool mNormalized
    param uint32_t mVectorSize
    ret RsElement
    }

ElementCreate2 {
        direct
    param const RsElement * elements
    param const char ** names
    param const uint32_t * arraySize
    ret RsElement
    }

AllocationCopyToBitmap {
    param RsAllocation alloc
    param void * data
    }

AllocationGetPointer {
    param RsAllocation va
    param uint32_t lod
    param RsAllocationCubemapFace face
    param uint32_t z
    param uint32_t array
    param size_t *stride
    ret void *
    }

Allocation1DData {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t lod
    param uint32_t count
    param const void *data
    }

Allocation1DElementData {
    param RsAllocation va
    param uint32_t x
    param uint32_t lod
    param const void *data
    param size_t comp_offset
    }

AllocationElementData {
    param RsAllocation va
    param uint32_t x
    param uint32_t y
    param uint32_t z
    param uint32_t lod
    param const void *data
    param size_t comp_offset
    }

Allocation2DData {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t yoff
    param uint32_t lod
    param RsAllocationCubemapFace face
    param uint32_t w
    param uint32_t h
    param const void *data
    param size_t stride
    }

Allocation3DData {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t yoff
    param uint32_t zoff
    param uint32_t lod
    param uint32_t w
    param uint32_t h
    param uint32_t d
    param const void *data
    param size_t stride
    }

AllocationGenerateMipmaps {
    param RsAllocation va
}

AllocationRead {
    param RsAllocation va
    param void * data
    }

Allocation1DRead {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t lod
    param uint32_t count
    param void *data
    }

AllocationElementRead {
    param RsAllocation va
    param uint32_t x
    param uint32_t y
    param uint32_t z
    param uint32_t lod
    param void *data
    param size_t comp_offset
    }

Allocation2DRead {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t yoff
    param uint32_t lod
    param RsAllocationCubemapFace face
    param uint32_t w
    param uint32_t h
    param void *data
    param size_t stride
}

Allocation3DRead {
    param RsAllocation va
    param uint32_t xoff
    param uint32_t yoff
    param uint32_t zoff
    param uint32_t lod
    param uint32_t w
    param uint32_t h
    param uint32_t d
    param void *data
    param size_t stride
    }

AllocationSyncAll {
    param RsAllocation va
    param RsAllocationUsageType src
}

AllocationResize1D {
    param RsAllocation va
    param uint32_t dimX
    }

AllocationCopy2DRange {
    param RsAllocation dest
    param uint32_t destXoff
    param uint32_t destYoff
    param uint32_t destMip
    param uint32_t destFace
    param uint32_t width
    param uint32_t height
    param RsAllocation src
    param uint32_t srcXoff
    param uint32_t srcYoff
    param uint32_t srcMip
    param uint32_t srcFace
    }

AllocationCopy3DRange {
    param RsAllocation dest
    param uint32_t destXoff
    param uint32_t destYoff
    param uint32_t destZoff
    param uint32_t destMip
    param uint32_t width
    param uint32_t height
    param uint32_t depth
    param RsAllocation src
    param uint32_t srcXoff
    param uint32_t srcYoff
    param uint32_t srcZoff
    param uint32_t srcMip
    }

ClosureCreate {
    direct
    param RsScriptKernelID kernelID
    param RsAllocation returnValue
    param RsScriptFieldID * fieldIDs
    param uintptr_t * values
    param int * sizes
    param RsClosure * depClosures
    param RsScriptFieldID * depFieldIDs
    ret RsClosure
    }

InvokeClosureCreate {
    direct
    param RsScriptInvokeID invokeID
    param const void * params
    param const RsScriptFieldID * fieldIDs
    param const uintptr_t * values
    param const int * sizes
    ret RsClosure
}

ClosureSetArg {
  param RsClosure closureID
  param uint32_t index
  param uintptr_t value
  param size_t valueSize
}

ClosureSetGlobal {
  param RsClosure closureID
  param RsScriptFieldID fieldID
  param uintptr_t value
  param size_t valueSize
}

SamplerCreate {
    direct
    param RsSamplerValue magFilter
    param RsSamplerValue minFilter
    param RsSamplerValue wrapS
    param RsSamplerValue wrapT
    param RsSamplerValue wrapR
    param float mAniso
    ret RsSampler
}

ScriptBindAllocation {
    param RsScript vtm
    param RsAllocation va
    param uint32_t slot
    }

ScriptSetTimeZone {
    param RsScript s
    param const char * timeZone
    }

ScriptInvokeIDCreate {
    param RsScript s
    param uint32_t slot
    ret RsScriptInvokeID
    }

ScriptInvoke {
    param RsScript s
    param uint32_t slot
    }

ScriptInvokeV {
    param RsScript s
    param uint32_t slot
    param const void * data
    }

ScriptForEach {
    param RsScript s
    param uint32_t slot
    param RsAllocation ain
    param RsAllocation aout
    param const void * usr
    param const RsScriptCall * sc
}

ScriptForEachMulti {
    param RsScript s
    param uint32_t slot
    param RsAllocation * ains
    param RsAllocation aout
    param const void * usr
    param const RsScriptCall * sc
}

ScriptSetVarI {
    param RsScript s
    param uint32_t slot
    param int value
    }

ScriptSetVarObj {
    param RsScript s
    param uint32_t slot
    param RsObjectBase value
    }

ScriptSetVarJ {
    param RsScript s
    param uint32_t slot
    param int64_t value
    }

ScriptSetVarF {
    param RsScript s
    param uint32_t slot
    param float value
    }

ScriptSetVarD {
    param RsScript s
    param uint32_t slot
    param double value
    }

ScriptSetVarV {
    param RsScript s
    param uint32_t slot
    param const void * data
    }

ScriptGetVarV {
    param RsScript s
    param uint32_t slot
    param void * data
    sync
    }

ScriptSetVarVE {
    param RsScript s
    param uint32_t slot
    param const void * data
    param RsElement e
    param const uint32_t * dims
    }


ScriptCCreate {
        param const char * resName
        param const char * cacheDir
    param const char * text
    ret RsScript
    }

ScriptIntrinsicCreate {
    param uint32_t id
    param RsElement eid
    ret RsScript
    }

ScriptKernelIDCreate {
    direct
    param RsScript sid
    param int slot
    param int sig
    ret RsScriptKernelID
    }

ScriptFieldIDCreate {
    direct
    param RsScript sid
    param int slot
    ret RsScriptFieldID
    }

ScriptGroupCreate {
    direct
    param RsScriptKernelID * kernels
    param RsScriptKernelID * src
    param RsScriptKernelID * dstK
    param RsScriptFieldID * dstF
    param const RsType * type
    ret RsScriptGroup
}

ScriptGroupSetOutput {
    param RsScriptGroup group
    param RsScriptKernelID kernel
    param RsAllocation alloc
}

ScriptGroupSetInput {
    param RsScriptGroup group
    param RsScriptKernelID kernel
    param RsAllocation alloc
}

ScriptGroupExecute {
    param RsScriptGroup group
}

ScriptGroup2Create{
    direct
    param const char * name
    param const char * cacheDir
    param RsClosure * closures
    ret RsScriptGroup2
}

AllocationIoSend {
    param RsAllocation alloc
    }

AllocationIoReceive {
    param RsAllocation alloc
    }
