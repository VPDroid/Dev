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

#ifndef ANDROID_STRUCTURED_ELEMENT_H
#define ANDROID_STRUCTURED_ELEMENT_H

#include "rsComponent.h"
#include "rsUtils.h"
#include "rsInternalDefines.h"
#include "rsObjectBase.h"

// ---------------------------------------------------------------------------
namespace android {
namespace renderscript {
/*****************************************************************************
 * CAUTION
 *
 * Any layout changes for this class may require a corresponding change to be
 * made to frameworks/compile/libbcc/lib/ScriptCRT/rs_core.c, which contains
 * a partial copy of the information below.
 *
 *****************************************************************************/
// An element is a group of Components that occupies one cell in a structure.
class Element : public ObjectBase {
public:
    struct Hal {
        mutable void *drv;

        struct State {
            RsDataType dataType;
            RsDataKind dataKind;
            uint32_t vectorSize;
            uint32_t elementSizeBytes;

            // Subelements
            const Element **fields;
            uint32_t *fieldArraySizes;
            const char **fieldNames;
            uint32_t *fieldNameLengths;
            uint32_t *fieldOffsetBytes;
            uint32_t fieldsCount;
        };
        State state;
    };
    Hal mHal;

    void operator delete(void* ptr);

    uint32_t getGLType() const;
    uint32_t getGLFormat() const;

    size_t getSizeBitsUnpadded() const;
    size_t getSizeBytesUnpadded() const {
        return (getSizeBitsUnpadded() + 7) >> 3;
    }

    size_t getSizeBits() const;
    size_t getSizeBytes() const {
        return (getSizeBits() + 7) >> 3;
    }

    size_t getFieldOffsetBits(uint32_t componentNumber) const {
        return mFields[componentNumber].offsetBits;
    }
    size_t getFieldOffsetBytes(uint32_t componentNumber) const {
        return mFields[componentNumber].offsetBits >> 3;
    }

    size_t getFieldOffsetBytesUnpadded(uint32_t componentNumber) const {
        return mFields[componentNumber].offsetBitsUnpadded >> 3;
    }

    uint32_t getFieldCount() const {return mFieldCount;}
    const Element * getField(uint32_t idx) const {return mFields[idx].e.get();}
    const char * getFieldName(uint32_t idx) const {return mFields[idx].name;}
    uint32_t getFieldArraySize(uint32_t idx) const {return mFields[idx].arraySize;}

    const Component & getComponent() const {return mComponent;}
    RsDataType getType() const {return mComponent.getType();}
    RsDataKind getKind() const {return mComponent.getKind();}
    uint32_t getBits() const {return mBits;}
    uint32_t getBitsUnpadded() const {return mBitsUnpadded;}
    uint32_t getVectorSize() const {return mComponent.getVectorSize();}

    void dumpLOGV(const char *prefix) const;
    virtual void serialize(Context *rsc, OStream *stream) const;
    virtual RsA3DClassID getClassId() const { return RS_A3D_CLASS_ID_ELEMENT; }
    static Element *createFromStream(Context *rsc, IStream *stream);

    static ObjectBaseRef<const Element> createRef(Context *rsc,
                                                  RsDataType dt,
                                                  RsDataKind dk,
                                                  bool isNorm,
                                                  uint32_t vecSize);
    static ObjectBaseRef<const Element> createRef(Context *rsc, size_t count,
                                                  const Element **,
                                                  const char **,
                                                  const size_t * lengths,
                                                  const uint32_t *asin);

    static const Element* create(Context *rsc,
                                 RsDataType dt,
                                 RsDataKind dk,
                                 bool isNorm,
                                 uint32_t vecSize) {
        ObjectBaseRef<const Element> elem = createRef(rsc, dt, dk, isNorm, vecSize);
        elem->incUserRef();
        return elem.get();
    }
    static const Element* create(Context *rsc, size_t count,
                                 const Element **ein,
                                 const char **nin,
                                 const size_t * lengths = nullptr,
                                 const uint32_t *asin = nullptr) {
        ObjectBaseRef<const Element> elem = createRef(rsc, count, ein, nin, lengths, asin);
        elem->incUserRef();
        return elem.get();
    }

    void incRefs(const void *) const;
    void decRefs(const void *) const;
    virtual void callUpdateCacheObject(const Context *rsc, void *dstObj) const;
    bool getHasReferences() const {return mHasReference;}

protected:
    // deallocate any components that are part of this element.
    void clear();

    typedef struct {
        const char *name;
        ObjectBaseRef<const Element> e;
        uint32_t offsetBits;
        uint32_t offsetBitsUnpadded;
        uint32_t arraySize;
    } ElementField_t;
    ElementField_t *mFields;
    size_t mFieldCount;
    bool mHasReference;


    virtual ~Element();
    Element(Context *);

    Component mComponent;
    uint32_t mBitsUnpadded;
    uint32_t mBits;

    void compute();

    virtual void preDestroy() const;
};


class ElementState {
public:
    ElementState();
    ~ElementState();

    // Cache of all existing elements.
    Vector<Element *> mElements;
};


}
}
#endif //ANDROID_STRUCTURED_ELEMENT_H
