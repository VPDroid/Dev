/*
 * Copyright 2011, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __GLTRACE_CONTEXT_H_
#define __GLTRACE_CONTEXT_H_

#include <map>
#include <pthread.h>
#include <utils/KeyedVector.h>

#include "hooks.h"
#include "gltrace_transport.h"

namespace android {
namespace gltrace {

using ::android::gl_hooks_t;

enum FBBinding {CURRENTLY_BOUND_FB, FB0};

class GLTraceState;

class ElementArrayBuffer {
    GLvoid *mBuf;
    GLsizeiptr mSize;

public:
    ElementArrayBuffer():mBuf(NULL), mSize(0) {}
    ElementArrayBuffer(GLvoid *buf, GLsizeiptr size);
    ~ElementArrayBuffer();

    void updateSubBuffer(GLintptr offset, const GLvoid* data, GLsizeiptr size);
    GLvoid *getBuffer();
    GLsizeiptr getSize();
};

/** GL Trace Context info associated with each EGLContext */
class GLTraceContext {
    int mId;                    /* unique context id */
    int mVersion;               /* GL version, e.g: egl_connection_t::GLESv2_INDEX */
    int mVersionMajor;          /* GL major version. Lazily parsed in getVersionX(). */
    int mVersionMinor;          /* GL minor version. Lazily parsed in getVersionX(). */
    bool mVersionParsed;        /* True if major and minor versions have been parsed. */
    GLTraceState *mState;       /* parent GL Trace state (for per process GL Trace State Info) */

    void *fbcontents;           /* memory area to read framebuffer contents */
    void *fbcompressed;         /* destination for lzf compressed framebuffer */
    unsigned fbcontentsSize;    /* size of fbcontents & fbcompressed buffers */

    BufferedOutputStream *mBufferedOutputStream; /* stream where trace info is sent */

    /* list of element array buffers in use. */
    DefaultKeyedVector<GLuint, ElementArrayBuffer*> mElementArrayBuffers;

    /* Parses the GL version string returned from glGetString(GL_VERSION) to get find the major and
       minor versions of the GLES API. The context must be current before calling. */
    void parseGlesVersion();
    void resizeFBMemory(unsigned minSize);
public:
    gl_hooks_t *hooks;

    GLTraceContext(int id, int version, GLTraceState *state, BufferedOutputStream *stream);
    int getId();
    int getVersion();
    int getVersionMajor();
    int getVersionMinor();
    GLTraceState *getGlobalTraceState();
    void getCompressedFB(void **fb, unsigned *fbsize,
                            unsigned *fbwidth, unsigned *fbheight,
                            FBBinding fbToRead);

    // Methods to work with element array buffers
    void bindBuffer(GLuint bufferId, GLvoid *data, GLsizeiptr size);
    void getBuffer(GLuint bufferId, GLvoid **data, GLsizeiptr *size);
    void updateBufferSubData(GLuint bufferId, GLintptr offset, GLvoid *data, GLsizeiptr size);
    void deleteBuffer(GLuint bufferId);

    void traceGLMessage(GLMessage *msg);
};

/** Per process trace state. */
class GLTraceState {
    int mTraceContextIds;
    TCPStream *mStream;
    std::map<EGLContext, GLTraceContext*> mPerContextState;

    /* Options controlling additional data to be collected on
       certain trace calls. */
    bool mCollectFbOnEglSwap;
    bool mCollectFbOnGlDraw;
    bool mCollectTextureDataOnGlTexImage;
    pthread_rwlock_t mTraceOptionsRwLock;

    /* helper methods to get/set values using provided lock for mutual exclusion. */
    void safeSetValue(bool *ptr, bool value, pthread_rwlock_t *lock);
    bool safeGetValue(bool *ptr, pthread_rwlock_t *lock);
public:
    GLTraceState(TCPStream *stream);
    ~GLTraceState();

    GLTraceContext *createTraceContext(int version, EGLContext c);
    GLTraceContext *getTraceContext(EGLContext c);

    TCPStream *getStream();

    /* Methods to set trace options. */
    void setCollectFbOnEglSwap(bool en);
    void setCollectFbOnGlDraw(bool en);
    void setCollectTextureDataOnGlTexImage(bool en);

    /* Methods to retrieve trace options. */
    bool shouldCollectFbOnEglSwap();
    bool shouldCollectFbOnGlDraw();
    bool shouldCollectTextureDataOnGlTexImage();
};

void setupTraceContextThreadSpecific(GLTraceContext *context);
GLTraceContext *getGLTraceContext();
void releaseContext();

};
};

#endif
