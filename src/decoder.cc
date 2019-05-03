#include <node_api.h>
#include <gif_lib.h>

using namespace v8;
using namespace node;

#include "macros.h"

static inline int clamp(int inclusive_min, int x, int inclusive_max) {
  return x <= inclusive_min ? inclusive_min
       : x >= inclusive_max ? inclusive_max
       : x;
}

struct DecodeBaton : Baton {
    GifFileType *gif_file;

    Persistent<Object> gif_byte_buffer;
    unsigned char *gif_bytes;
    size_t gif_byte_count;
    size_t gif_byte_offset;

    int status;

    DecodeBaton(Handle<Function> cb_, Handle<Object> buffer_) : Baton(cb_) {
        gif_byte_buffer = Persistent<Object>::New(buffer_);
        gif_bytes = (unsigned char *)Buffer::Data(gif_byte_buffer);
        gif_byte_count = Buffer::Length(gif_byte_buffer);
        gif_byte_offset = 0;

        status = GIF_OK;

        gif_file = NULL;
    }

    ~DecodeBaton() {
        gif_byte_buffer.Dispose();
        DGifCloseFile(gif_file);
    }

    static int ReadMemoryGif (GifFileType *gif_file, GifByteType *buffer, int size) {
        DecodeBaton *baton = static_cast<DecodeBaton *>(gif_file->UserData);
        if (baton->gif_byte_offset == baton->gif_byte_count) {
            return 0;
        }

        size_t copy_length = baton->gif_byte_count - baton->gif_byte_offset;
        if (copy_length > (size_t)size) {
            // Trim to request
            copy_length = (size_t)size;
        }

        memcpy(buffer, baton->gif_bytes + baton->gif_byte_offset, copy_length);
        baton->gif_byte_offset += copy_length;
        return copy_length;
    }

    void Work() {
        status = GIF_OK;
        gif_file = DGifOpen(this, DecodeBaton::ReadMemoryGif, &status);

        if (status == GIF_OK) {
            status = DGifSlurp(gif_file);
        }
    }

    static void Work(uv_work_t *req) {
        DecodeBaton *baton = static_cast<DecodeBaton *>(req->data);
        baton->Work();
    }

    void After() {
        if (status != GIF_OK) {
            Local<Value> argv[1];
            argv[0] = Local<Value>::New(String::New(GifErrorString(status)));

            if (!callback.IsEmpty() && callback->IsFunction()) {
                TRY_CATCH_CALL(context, callback, sizeof(argv)/sizeof(*argv), argv);
            }
            return;
        }

        size_t pixel_count = gif_file->SWidth * gif_file->SHeight;
        Buffer *pixelBuffer = Buffer::New(pixel_count);
        memcpy(Buffer::Data(pixelBuffer), gif_file->SavedImages[0].RasterBits, pixel_count);

        Buffer *paletteBuffer;
        if (gif_file->SColorMap) {
            size_t colorCount = clamp(0, gif_file->SColorMap->ColorCount, 256);

            paletteBuffer = Buffer::New(colorCount*4);
            uint32_t *colors = (uint32_t *)Buffer::Data(paletteBuffer);
            for (size_t i = 0; i < colorCount; i++) {
                // TODO: How is alpha handled?
                GifColorType color = gif_file->SColorMap->Colors[i];
                colors[i] = color.Red | (color.Green<<8) | (color.Blue<<16) | 0xff000000;
            }
        } else {
            paletteBuffer = Buffer::New(0);
        }

        Local<Value> argv[5];
        argv[0] = Local<Value>::New(Null());
        argv[1] = Integer::New(gif_file->SWidth);
        argv[2] = Integer::New(gif_file->SHeight);
        argv[3] = Local<Object>::New(paletteBuffer->handle_);
        argv[4] = Local<Object>::New(pixelBuffer->handle_);

        if (!callback.IsEmpty() && callback->IsFunction()) {
            TRY_CATCH_CALL(context, callback, sizeof(argv)/sizeof(*argv), argv);
        }
    }

    static void After(uv_work_t *req) {
        HandleScope scope;
        DecodeBaton *baton = static_cast<DecodeBaton *>(req->data);

        baton->After();

        delete baton;
    }
};

Handle<Value> Decode(const Arguments& args) {
    HandleScope scope;

    REQUIRE_ARGUMENT_BUFFER(0, buffer);
    REQUIRE_ARGUMENT_FUNCTION(1, callback);

    DecodeBaton *baton = new DecodeBaton(callback, buffer);
    baton->context = Persistent<Object>::New(args.This());

    uv_queue_work(uv_default_loop(),
        &baton->request, DecodeBaton::Work, DecodeBaton::After);

    return scope.Close(String::New("back from decode!"));
}