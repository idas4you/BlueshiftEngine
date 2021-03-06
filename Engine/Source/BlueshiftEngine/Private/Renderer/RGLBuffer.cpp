// Copyright(c) 2017 POLYGONTEK
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http ://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Precompiled.h"
#include "Renderer/RendererGL.h"
#include "RGLInternal.h"
#include "SIMD/Simd.h"

BE_NAMESPACE_BEGIN

const GLenum ToGLBufferUsage(Renderer::BufferUsage usage) {
    switch (usage) {
    case Renderer::Static:
        return GL_STATIC_DRAW;
    case Renderer::Dynamic:
        return GL_DYNAMIC_DRAW;
    case Renderer::Stream:
        return GL_STREAM_DRAW;
    default:
        assert(0);
        return 0;
    }
}

const GLenum ToGLBufferTarget(Renderer::BufferType type) {
    switch (type) {
    case Renderer::VertexBuffer:
        return GL_ARRAY_BUFFER;
    case Renderer::IndexBuffer:
        return GL_ELEMENT_ARRAY_BUFFER;
    case Renderer::PixelPackBuffer:
        return GL_PIXEL_PACK_BUFFER;
    case Renderer::PixelUnpackBuffer:
        return GL_PIXEL_UNPACK_BUFFER;
    case Renderer::TexelBuffer:
        return GL_TEXTURE_BUFFER_EXT;
    case Renderer::UniformBuffer:
        return GL_UNIFORM_BUFFER;
    case Renderer::TransformFeedbackBuffer:
        return GL_TRANSFORM_FEEDBACK_BUFFER;
    case Renderer::CopyReadBuffer:
        return GL_COPY_READ_BUFFER;
    default:
        assert(0);
        return 0;
    }
}

Renderer::Handle RendererGL::CreateBuffer(BufferType type, BufferUsage usage, int size, int pitch, const void *data) {
    GLenum target = ToGLBufferTarget(type);

    GLBuffer *buffer    = new GLBuffer;
    buffer->type        = type;
    buffer->usage       = ToGLBufferUsage(usage);
    buffer->target      = target;
    buffer->size        = size;
    buffer->pitch       = pitch;
    buffer->writeOffset = 0;

    gglGenBuffers(1, &buffer->object);

    if (size > 0) {
        gglBindBuffer(target, buffer->object);
        gglBufferData(target, size, data, buffer->usage);
    }

    int handle = bufferList.FindNull();
    if (handle == -1) {
        handle = bufferList.Append(buffer);
    } else {
        bufferList[handle] = buffer;
    }

    if (size > 0) {
        Handle bufferHandle = currentContext->state->bufferHandles[type];
        const GLBuffer *buffer = bufferList[bufferHandle];
        gglBindBuffer(target, buffer->object);
    }

    return (Handle)handle;
}

void RendererGL::DeleteBuffer(Handle bufferHandle) {
    GLBuffer *buffer = bufferList[bufferHandle];

    for (int i = 0; i < COUNT_OF(currentContext->state->bufferHandles); i++) {
        if (bufferHandle == currentContext->state->bufferHandles[i]) {
            currentContext->state->bufferHandles[i] = NullBuffer;
            break;
        }
    }

    gglDeleteBuffers(1, &buffer->object);
    buffer->object = 0;

    delete bufferList[bufferHandle];
    bufferList[bufferHandle] = nullptr;
}

void RendererGL::BindBuffer(BufferType type, Handle bufferHandle) {
    Handle *bufferHandlePtr = &currentContext->state->bufferHandles[type];
    if (*bufferHandlePtr != bufferHandle) {
        *bufferHandlePtr = bufferHandle;
        const GLBuffer *buffer = bufferList[bufferHandle];
        gglBindBuffer(ToGLBufferTarget(type), buffer->object);
    }
}

void *RendererGL::MapBufferRange(Handle bufferHandle, BufferLockMode lockMode, int offset, int size) {
    GLBuffer *buffer = bufferList[bufferHandle];
    
    if (size < 0) {
        size = buffer->size;
    }

    assert(offset + size <= buffer->size);

    GLbitfield access = 0;
    access |= GL_MAP_UNSYNCHRONIZED_BIT;

    switch (lockMode) {
    case WriteOnly:
        access |= GL_MAP_WRITE_BIT;
        break;
    case WriteOnlyExplicitFlush:
        access |= (GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
        break;
    case WriteOnlyPersistent:
        access |= GL_MAP_WRITE_BIT;
#ifdef GL_ARB_buffer_storage
        if (OpenGL::SupportsBufferStorage()) {
            access |= (GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

            gglBufferStorage(buffer->target, size, 0, access);
        }
#endif
        break;
    }
    
    void *ptr = nullptr;
    if (offset > 0 || offset + size < buffer->size) {
        ptr = gglMapBufferRange(buffer->target, offset, size, access | GL_MAP_INVALIDATE_RANGE_BIT);
    } else {
        ptr = gglMapBufferRange(buffer->target, 0, size, access | GL_MAP_INVALIDATE_BUFFER_BIT);
    }

    assert(ptr);
    return ptr;
}

bool RendererGL::UnmapBuffer(Handle bufferHandle) {
    GLBuffer *buffer = bufferList[bufferHandle];
    // glUnmapBuffer returns GL_TRUE unless the data store contents have become corrupt during the time the data
    // store was mapped. This can occur for system-specific reasons that affect the availability of graphics
    // memory, such as screen mode changes. In such situations, GL_FALSE is returned and the data store contents
    // are undefined. An application must detect this rare condition and reinitialize the data store.
    return !!gglUnmapBuffer(buffer->target);
}

void RendererGL::FlushMappedBufferRange(Handle bufferHandle, int offset, int size) {
    GLBuffer *buffer = bufferList[bufferHandle];

    if (size < 0) {
        size = buffer->size;
    }

    assert(offset + size <= buffer->size);

    gglFlushMappedBufferRange(buffer->target, offset, size);
}

int RendererGL::BufferDiscardWrite(Handle bufferHandle, int size, const void *data) {
    GLBuffer *buffer = bufferList[bufferHandle];

    if (gglMapBufferRange) {
        // glMapBufferRange 함수는 buffer alloc 되어 있지 않다면 GL_INVALID_VALUE error 발생
        gglBufferData(buffer->target, size, nullptr, buffer->usage);
        void *ptr = gglMapBufferRange(buffer->target, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        BE1::simdProcessor->Memcpy(ptr, data, size);
        gglUnmapBuffer(buffer->target);
    } else {
        // buffer respecification using glBufferData
        gglBufferData(buffer->target, size, nullptr, buffer->usage);
        gglBufferData(buffer->target, size, data, buffer->usage);
    }

    buffer->size = size;
    buffer->writeOffset = 0;

    return 0;
}

int RendererGL::BufferWrite(Handle bufferHandle, int alignSize, int size, const void *data) {
    GLBuffer *writeBuffer = bufferList[bufferHandle];

    if (writeBuffer->pitch > 0 && size > writeBuffer->pitch) {
        return -1;
    }

    int base = writeBuffer->writeOffset + alignSize - 1;
    base -= base % alignSize;

    if (writeBuffer->pitch > 0) {
        int startRow = base / writeBuffer->pitch;
        int endRow = (base + size) / writeBuffer->pitch;
        
        if (endRow > startRow) {
            base -= base % writeBuffer->pitch;
            base += writeBuffer->pitch;
        }
    }
    
    int endPos = base + size;

    if (endPos > writeBuffer->size) {
        return -1;
    }

    // If date == nullptr, buffer memory is reserved
    if (data) {
        /*if (gglMapBufferRange) {
            byte *ptr = (byte *)gglMapBufferRange(buffer->target, offset, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            BE1::simdProcessor->Memcpy(ptr, data, size);
            gglUnmapBuffer(buffer->target);
        }*/
        gglBufferSubData(writeBuffer->target, base, size, data);
    }
    
    writeBuffer->writeOffset = endPos;

    return base;
}

int	RendererGL::BufferCopy(Handle readBufferHandle, Handle writeBufferHandle, int alignSize, int size) {
    GLBuffer *writeBuffer = bufferList[writeBufferHandle];
    const GLBuffer *readBuffer = bufferList[readBufferHandle];

    if (writeBuffer->pitch > 0 && size > writeBuffer->pitch) {
        return -1;
    }

    int base = writeBuffer->writeOffset + alignSize - 1;
    base -= base % alignSize;
    
    if (writeBuffer->pitch > 0) {
        int startRow = base / writeBuffer->pitch;
        int endRow = (base + size) / writeBuffer->pitch;
        
        if (endRow > startRow) {
            base -= base % writeBuffer->pitch;
            base += writeBuffer->pitch;
        }
    }
    
    int endPos = base + size;

    if (endPos > writeBuffer->size) {
        return -1;
    }

    gglBindBuffer(GL_COPY_READ_BUFFER, readBuffer->object);
    gglBindBuffer(GL_COPY_WRITE_BUFFER, writeBuffer->object);
    gglCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, base, size);
    gglBindBuffer(GL_COPY_READ_BUFFER, 0);
    gglBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    
    writeBuffer->writeOffset = endPos;

    return base;
}

void RendererGL::BufferRewind(Handle bufferHandle) {
    GLBuffer *buffer = bufferList[bufferHandle];
    
    buffer->writeOffset = 0;
}

BE_NAMESPACE_END
