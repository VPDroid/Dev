/* 
 ** Copyright 2007, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License"); 
 ** you may not use this file except in compliance with the License. 
 ** You may obtain a copy of the License at 
 **
 **     http://www.apache.org/licenses/LICENSE-2.0 
 **
 ** Unless required by applicable law or agreed to in writing, software 
 ** distributed under the License is distributed on an "AS IS" BASIS, 
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
 ** See the License for the specific language governing permissions and 
 ** limitations under the License.
 */

#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include "../hooks.h"
#include "../egl_impl.h"

using namespace android;

// ----------------------------------------------------------------------------
// extensions for the framework
// ----------------------------------------------------------------------------

extern "C" {
GL_API void GL_APIENTRY glColorPointerBounds(GLint size, GLenum type, GLsizei stride,
        const GLvoid *ptr, GLsizei count);
GL_API void GL_APIENTRY glNormalPointerBounds(GLenum type, GLsizei stride,
        const GLvoid *pointer, GLsizei count);
GL_API void GL_APIENTRY glTexCoordPointerBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei count);
GL_API void GL_APIENTRY glVertexPointerBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei count);
GL_API void GL_APIENTRY glPointSizePointerOESBounds(GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei count);
GL_API void GL_APIENTRY glMatrixIndexPointerOESBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei count);
GL_API void GL_APIENTRY glWeightPointerOESBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei count);
}

void glColorPointerBounds(GLint size, GLenum type, GLsizei stride,
        const GLvoid *ptr, GLsizei /*count*/) {
    glColorPointer(size, type, stride, ptr);
}
void glNormalPointerBounds(GLenum type, GLsizei stride,
        const GLvoid *pointer, GLsizei /*count*/) {
    glNormalPointer(type, stride, pointer);
}
void glTexCoordPointerBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei /*count*/) {
    glTexCoordPointer(size, type, stride, pointer);
}
void glVertexPointerBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei /*count*/) {
    glVertexPointer(size, type, stride, pointer);
}

void GL_APIENTRY glPointSizePointerOESBounds(GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei /*count*/) {
    glPointSizePointerOES(type, stride, pointer);
}

GL_API void GL_APIENTRY glMatrixIndexPointerOESBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei /*count*/) {
    glMatrixIndexPointerOES(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glWeightPointerOESBounds(GLint size, GLenum type,
        GLsizei stride, const GLvoid *pointer, GLsizei /*count*/) {
    glWeightPointerOES(size, type, stride, pointer);
}

// ----------------------------------------------------------------------------
// Actual GL entry-points
// ----------------------------------------------------------------------------

#undef API_ENTRY
#undef CALL_GL_API
#undef CALL_GL_API_RETURN

#if USE_SLOW_BINDING

    #define API_ENTRY(_api) _api

    #define CALL_GL_API(_api, ...)                                       \
        gl_hooks_t::gl_t const * const _c = &getGlThreadSpecific()->gl;  \
        if (_c) return _c->_api(__VA_ARGS__);

#elif defined(__arm__)

    #define GET_TLS(reg) "mrc p15, 0, " #reg ", c13, c0, 3 \n"

    #define API_ENTRY(_api) __attribute__((noinline)) _api

    #define CALL_GL_API(_api, ...)                              \
         asm volatile(                                          \
            GET_TLS(r12)                                        \
            "ldr   r12, [r12, %[tls]] \n"                       \
            "cmp   r12, #0            \n"                       \
            "ldrne pc,  [r12, %[api]] \n"                       \
            :                                                   \
            : [tls] "J"(TLS_SLOT_OPENGL_API*4),                 \
              [api] "J"(__builtin_offsetof(gl_hooks_t, gl._api))    \
            : "r12"                                             \
            );

#elif defined(__aarch64__)

    #define API_ENTRY(_api) __attribute__((noinline)) _api

    #define CALL_GL_API(_api, ...)                                  \
        asm volatile(                                               \
            "mrs x16, tpidr_el0\n"                                  \
            "ldr x16, [x16, %[tls]]\n"                              \
            "cbz x16, 1f\n"                                         \
            "ldr x16, [x16, %[api]]\n"                              \
            "br  x16\n"                                             \
            "1:\n"                                                  \
            :                                                       \
            : [tls] "i" (TLS_SLOT_OPENGL_API * sizeof(void*)),      \
              [api] "i" (__builtin_offsetof(gl_hooks_t, gl._api))   \
            : "x16"                                                 \
        );

#elif defined(__i386__)

    #define API_ENTRY(_api) __attribute__((noinline,optimize("omit-frame-pointer"))) _api

    #define CALL_GL_API(_api, ...)                                  \
        register void* fn;                                          \
        __asm__ volatile(                                           \
            "mov %%gs:0, %[fn]\n"                                   \
            "mov %P[tls](%[fn]), %[fn]\n"                           \
            "test %[fn], %[fn]\n"                                   \
            "je 1f\n"                                               \
            "jmp *%P[api](%[fn])\n"                                 \
            "1:\n"                                                  \
            : [fn] "=r" (fn)                                        \
            : [tls] "i" (TLS_SLOT_OPENGL_API*sizeof(void*)),        \
              [api] "i" (__builtin_offsetof(gl_hooks_t, gl._api))   \
            : "cc"                                                  \
            );

#elif defined(__x86_64__)

    #define API_ENTRY(_api) __attribute__((noinline,optimize("omit-frame-pointer"))) _api

    #define CALL_GL_API(_api, ...)                                  \
         register void** fn;                                        \
         __asm__ volatile(                                          \
            "mov %%fs:0, %[fn]\n"                                   \
            "mov %P[tls](%[fn]), %[fn]\n"                           \
            "test %[fn], %[fn]\n"                                   \
            "je 1f\n"                                               \
            "jmp *%P[api](%[fn])\n"                                 \
            "1:\n"                                                  \
            : [fn] "=r" (fn)                                        \
            : [tls] "i" (TLS_SLOT_OPENGL_API*sizeof(void*)),        \
              [api] "i" (__builtin_offsetof(gl_hooks_t, gl._api))   \
            : "cc"                                                  \
            );

#elif defined(__mips64)

    #define API_ENTRY(_api) __attribute__((noinline)) _api

    #define CALL_GL_API(_api, ...)                            \
    register unsigned long _t0 asm("$12");                    \
    register unsigned long _fn asm("$25");                    \
    register unsigned long _tls asm("$3");                    \
    register unsigned long _v0 asm("$2");                     \
    asm volatile(                                             \
        ".set  push\n\t"                                      \
        ".set  noreorder\n\t"                                 \
        "rdhwr %[tls], $29\n\t"                               \
        "ld    %[t0], %[OPENGL_API](%[tls])\n\t"              \
        "beqz  %[t0], 1f\n\t"                                 \
        " move %[fn], $ra\n\t"                                \
        "ld    %[t0], %[API](%[t0])\n\t"                      \
        "beqz  %[t0], 1f\n\t"                                 \
        " nop\n\t"                                            \
        "move  %[fn], %[t0]\n\t"                              \
        "1:\n\t"                                              \
        "jalr  $0, %[fn]\n\t"                                 \
        " move %[v0], $0\n\t"                                 \
        ".set  pop\n\t"                                       \
        : [fn] "=c"(_fn),                                     \
          [tls] "=&r"(_tls),                                  \
          [t0] "=&r"(_t0),                                    \
          [v0] "=&r"(_v0)                                     \
        : [OPENGL_API] "I"(TLS_SLOT_OPENGL_API*sizeof(void*)),\
          [API] "I"(__builtin_offsetof(gl_hooks_t, gl._api))  \
        :                                                     \
        );

#elif defined(__mips__)

    #define API_ENTRY(_api) __attribute__((noinline)) _api

    #define CALL_GL_API(_api, ...)                               \
        register unsigned int _t0 asm("$8");                     \
        register unsigned int _fn asm("$25");                    \
        register unsigned int _tls asm("$3");                    \
        register unsigned int _v0 asm("$2");                     \
        asm volatile(                                            \
            ".set  push\n\t"                                     \
            ".set  noreorder\n\t"                                \
            ".set  mips32r2\n\t"                                 \
            "rdhwr %[tls], $29\n\t"                              \
            "lw    %[t0], %[OPENGL_API](%[tls])\n\t"             \
            "beqz  %[t0], 1f\n\t"                                \
            " move %[fn], $ra\n\t"                               \
            "lw    %[t0], %[API](%[t0])\n\t"                     \
            "beqz  %[t0], 1f\n\t"                                \
            " nop\n\t"                                           \
            "move  %[fn], %[t0]\n\t"                             \
            "1:\n\t"                                             \
            "jalr  $0, %[fn]\n\t"                                \
            " move %[v0], $0\n\t"                                \
            ".set  pop\n\t"                                      \
            : [fn] "=c"(_fn),                                    \
              [tls] "=&r"(_tls),                                 \
              [t0] "=&r"(_t0),                                   \
              [v0] "=&r"(_v0)                                    \
            : [OPENGL_API] "I"(TLS_SLOT_OPENGL_API*4),           \
              [API] "I"(__builtin_offsetof(gl_hooks_t, gl._api)) \
            :                                                    \
            );

#endif

#define CALL_GL_API_RETURN(_api, ...) \
    CALL_GL_API(_api, __VA_ARGS__) \
    return 0;


extern "C" {
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "gl_api.in"
#include "glext_api.in"
#pragma GCC diagnostic warning "-Wunused-parameter"
}

#undef API_ENTRY
#undef CALL_GL_API
#undef CALL_GL_API_RETURN

/*
 * glGetString() is special because we expose some extensions in the wrapper
 */

extern "C" const GLubyte * __glGetString(GLenum name);

const GLubyte * glGetString(GLenum name) {
    const GLubyte * ret = egl_get_string_for_current_context(name);
    if (ret == NULL) {
        gl_hooks_t::gl_t const * const _c = &getGlThreadSpecific()->gl;
        ret = _c->glGetString(name);
    }
    return ret;
}
