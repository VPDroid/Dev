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
 *
 */

/*
 * Graphics Test Library Header
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "EGLUtils.h"

void glTestPrintGLString(const char *name, GLenum s);
void glTestCheckEglError(const char* op, EGLBoolean returnVal = EGL_TRUE);
void glTestCheckGlError(const char* op);
void glTestPrintEGLConfiguration(EGLDisplay dpy, EGLConfig config);
