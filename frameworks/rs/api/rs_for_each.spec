#
# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

header:
summary: Kernel Invocation Functions and Types
description:
 The @rsForEach() function can be used to invoke the root kernel of a script.

 The other functions are used to get the characteristics of the invocation of
 an executing kernel, like dimensions and current indexes.  These functions take
 a @rs_kernel_context as argument.
end:

type: rs_for_each_strategy_t
enum: rs_for_each_strategy
value: RS_FOR_EACH_STRATEGY_SERIAL = 0, "Prefer contiguous memory regions."
value: RS_FOR_EACH_STRATEGY_DONT_CARE = 1, "No prefrences."
#TODO explain this better
value: RS_FOR_EACH_STRATEGY_DST_LINEAR = 2, "Prefer DST."
value: RS_FOR_EACH_STRATEGY_TILE_SMALL = 3, "Prefer processing small rectangular regions."
value: RS_FOR_EACH_STRATEGY_TILE_MEDIUM = 4, "Prefer processing medium rectangular regions."
value: RS_FOR_EACH_STRATEGY_TILE_LARGE = 5, "Prefer processing large rectangular regions."
summary: Suggested cell processing order
description:
 This type is used to suggest how the invoked kernel should iterate over the cells of the
 allocations.  This is a hint only.  Implementations may not follow the suggestion.

 This specification can help the caching behavior of the running kernel, e.g. the cache
 locality when the processing is distributed over multiple cores.
end:

type: rs_kernel_context
version: 23
simple: const struct rs_kernel_context_t *
summary: Handle to a kernel invocation context
description:
 The kernel context contains common characteristics of the allocations being iterated
 over, like dimensions, and rarely used indexes, like the Array0 index or the current
 level of detail.

 A kernel may be executed in parallel over multiple threads.  Each thread will have its
 own context.

 You can access the context by adding a special parameter named "context" and of type
 rs_kernel_context to your kernel function.  See @rsGetDimX() and @rsGetArray0() for examples.
end:

type: rs_script_call_t
struct: rs_script_call
field: rs_for_each_strategy_t strategy, "Currently ignored.  In the future, will be suggested cell iteration strategy."
field: uint32_t xStart, "Starting index in the X dimension."
field: uint32_t xEnd, "Ending index (exclusive) in the X dimension."
field: uint32_t yStart, "Starting index in the Y dimension."
field: uint32_t yEnd, "Ending index (exclusive) in the Y dimension."
field: uint32_t zStart, "Starting index in the Z dimension."
field: uint32_t zEnd, "Ending index (exclusive) in the Z dimension."
field: uint32_t arrayStart, "Starting index in the Array0 dimension."
field: uint32_t arrayEnd, "Ending index (exclusive) in the Array0 dimension."
field: uint32_t array1Start, "Starting index in the Array1 dimension."
field: uint32_t array1End, "Ending index (exclusive) in the Array1 dimension."
field: uint32_t array2Start, "Starting index in the Array2 dimension."
field: uint32_t array2End, "Ending index (exclusive) in the Array2 dimension."
field: uint32_t array3Start, "Starting index in the Array3 dimension."
field: uint32_t array3End, "Ending index (exclusive) in the Array3 dimension."
summary: Cell iteration information
description:
 This structure is used to provide iteration information to a rsForEach call.
 It is currently used to restrict processing to a subset of cells.  In future
 versions, it will also be used to provide hint on how to best iterate over
 the cells.

 The Start fields are inclusive and the End fields are exclusive.  E.g. to iterate
 over cells 4, 5, 6, and 7 in the X dimension, set xStart to 4 and xEnd to 8.
end:

function: rsForEach
version: 9 13
ret: void
arg: rs_script script, "Script to call."
arg: rs_allocation input, "Allocation to source data from."
arg: rs_allocation output, "Allocation to write date into."
arg: const void* usrData, "User defined data to pass to the script.  May be NULL."
arg: const rs_script_call_t* sc, "Extra control information used to select a sub-region of the allocation to be processed or suggest a walking strategy.  May be NULL."
summary: Invoke the root kernel of a script
description:
 Invoke the kernel named "root" of the specified script.  Like other kernels, this root()
 function will be invoked repeatedly over the cells of the specificed allocation, filling
 the output allocation with the results.

 When rsForEach is called, the root script is launched immediately.  rsForEach returns
 only when the script has completed and the output allocation is ready to use.

 The rs_script argument is typically initialized using a global variable set from Java.

 The kernel can be invoked with just an input allocation or just an output allocation.
 This can be done by defining an rs_allocation variable and not initializing it.  E.g.<code><br/>
 rs_script gCustomScript;<br/>
 void specializedProcessing(rs_allocation in) {<br/>
 &nbsp;&nbsp;rs_allocation ignoredOut;<br/>
 &nbsp;&nbsp;rsForEach(gCustomScript, in, ignoredOut);<br/>
 }<br/></code>

 If both input and output allocations are specified, they must have the same dimensions.
test: none
end:

function: rsForEach
version: 9 13
ret: void
arg: rs_script script
arg: rs_allocation input
arg: rs_allocation output
arg: const void* usrData
test: none
end:

function: rsForEach
version: 14 20
ret: void
arg: rs_script script
arg: rs_allocation input
arg: rs_allocation output
arg: const void* usrData
arg: size_t usrDataLen, "Size of the userData structure.  This will be used to perform a shallow copy of the data if necessary."
arg: const rs_script_call_t* sc
test: none
end:

function: rsForEach
version: 14 20
ret: void
arg: rs_script script
arg: rs_allocation input
arg: rs_allocation output
arg: const void* usrData
arg: size_t usrDataLen
test: none
end:

function: rsForEach
version: 14
ret: void
arg: rs_script script
arg: rs_allocation input
arg: rs_allocation output
test: none
end:

function: rsGetArray0
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Index in the Array0 dimension for the specified context
description:
 Returns the index in the Array0 dimension of the cell being processed, as specified
 by the supplied context.

 This context is created when a kernel is launched and updated at each iteration.
 It contains common characteristics of the allocations being iterated over and rarely
 used indexes, like the Array0 index.

 You can access the context by adding a special parameter named "context" and of
 type rs_kernel_context to your kernel function.  E.g.<br/>
 <code>short RS_KERNEL myKernel(short value, uint32_t x, rs_kernel_context context) {<br/>
 &nbsp;&nbsp;// The current index in the common x, y, z, w dimensions are accessed by<br/>
 &nbsp;&nbsp;// adding these variables as arguments.  For the more rarely used indexes<br/>
 &nbsp;&nbsp;// to the other dimensions, extract them from the context:<br/>
 &nbsp;&nbsp;uint32_t index_a0 = rsGetArray0(context);<br/>
 &nbsp;&nbsp;//...<br/>
 }<br/></code>

 This function returns 0 if the Array0 dimension is not present.
test: none
end:

function: rsGetArray1
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Index in the Array1 dimension for the specified context
description:
 Returns the index in the Array1 dimension of the cell being processed, as specified
 by the supplied context.  See @rsGetArray0() for an explanation of the context.

 Returns 0 if the Array1 dimension is not present.
test: none
end:

function: rsGetArray2
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Index in the Array2 dimension for the specified context
description:
 Returns the index in the Array2 dimension of the cell being processed,
 as specified by the supplied context.  See @rsGetArray0() for an explanation
 of the context.

 Returns 0 if the Array2 dimension is not present.
test: none
end:

function: rsGetArray3
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Index in the Array3 dimension for the specified context
description:
 Returns the index in the Array3 dimension of the cell being processed, as specified
 by the supplied context.  See @rsGetArray0() for an explanation of the context.

 Returns 0 if the Array3 dimension is not present.
test: none
end:

function: rsGetDimArray0
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Array0 dimension for the specified context
description:
 Returns the size of the Array0 dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Array0 dimension is not present.
#TODO Add an hyperlink to something that explains Array0/1/2/3
# for the relevant functions.
test: none
end:

function: rsGetDimArray1
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Array1 dimension for the specified context
description:
 Returns the size of the Array1 dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Array1 dimension is not present.
test: none
end:

function: rsGetDimArray2
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Array2 dimension for the specified context
description:
 Returns the size of the Array2 dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Array2 dimension is not present.
test: none
end:

function: rsGetDimArray3
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Array3 dimension for the specified context
description:
 Returns the size of the Array3 dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Array3 dimension is not present.
test: none
end:

function: rsGetDimHasFaces
version: 23
ret: bool, "Returns true if more than one face is present, false otherwise."
arg: rs_kernel_context context
summary: Presence of more than one face for the specified context
description:
 If the context refers to a cubemap, this function returns true if there's more than
 one face present.  In all other cases, it returns false.  See @rsGetDimX() for an
 explanation of the context.

 @rsAllocationGetDimFaces() is similar but returns 0 or 1 instead of a bool.
test: none
end:

function: rsGetDimLod
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Number of levels of detail for the specified context
description:
 Returns the number of levels of detail for the specified context.  This is useful
 for mipmaps.  See @rsGetDimX() for an explanation of the context.

 Returns 0 if Level of Detail is not used.

 @rsAllocationGetDimLOD() is similar but returns 0 or 1 instead the actual
 number of levels.
test: none
end:

function: rsGetDimX
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the X dimension for the specified context
description:
 Returns the size of the X dimension for the specified context.

 This context is created when a kernel is launched.  It contains common
 characteristics of the allocations being iterated over by the kernel in
 a very efficient structure.  It also contains rarely used indexes.

 You can access it by adding a special parameter named "context" and of
 type rs_kernel_context to your kernel function.  E.g.<br/>
 <code>int4 RS_KERNEL myKernel(int4 value, rs_kernel_context context) {<br/>
 &nbsp;&nbsp;uint32_t size = rsGetDimX(context); //...<br/></code>

 To get the dimension of specific allocation, use @rsAllocationGetDimX().
test: none
end:

function: rsGetDimY
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Y dimension for the specified context
description:
 Returns the size of the X dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Y dimension is not present.

 To get the dimension of specific allocation, use @rsAllocationGetDimY().
test: none
end:

function: rsGetDimZ
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Size of the Z dimension for the specified context
description:
 Returns the size of the Z dimension for the specified context.
 See @rsGetDimX() for an explanation of the context.

 Returns 0 if the Z dimension is not present.

 To get the dimension of specific allocation, use @rsAllocationGetDimZ().
test: none
end:

function: rsGetFace
version: 23
ret: rs_allocation_cubemap_face
arg: rs_kernel_context context
summary: Coordinate of the Face for the specified context
description:
 Returns the face on which the cell being processed is found, as specified by the
 supplied context.  See @rsGetArray0() for an explanation of the context.

 Returns RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X if the face dimension is not
 present.
test: none
end:

function: rsGetLod
version: 23
ret: uint32_t
arg: rs_kernel_context context
summary: Index in the Levels of Detail dimension for the specified context
description:
 Returns the index in the Levels of Detail dimension of the cell being processed,
 as specified by the supplied context.  See @rsGetArray0() for an explanation of
 the context.

 Returns 0 if the Levels of Detail dimension is not present.
test: none
end:
