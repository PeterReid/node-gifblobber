#ifndef NODE_SQLITE3_SRC_DATABASE_H
#define NODE_SQLITE3_SRC_DATABASE_H

#include <node.h>
#include <node_buffer.h>

#include <string>
#include <queue>

#include <gif_lib.h>

using namespace v8;
using namespace node;

namespace node_sqlite3 {

class Database;


class Foozle : public ObjectWrap {
public:
    Foozle();
    virtual ~Foozle();
    
    static Persistent<FunctionTemplate> constructor_template;
    static void Init(Handle<Object> target);
    
    static Handle<Value> New(const Arguments& args);

    static Handle<Value> Mash(const Arguments& args);
    
    
    struct Baton {
        uv_work_t request;
        Foozle* foozle;
        Persistent<Function> callback;
        //int status;
        //std::string message;

        Baton(Foozle* foozle_, Handle<Function> cb_) :
                foozle(foozle_) {
            foozle->Ref();
            request.data = this;
            callback = Persistent<Function>::New(cb_);
        }
        virtual ~Baton() {
            foozle->Unref();
            callback.Dispose();
        }
    };

    struct SlurpBaton : Baton {
        Persistent<Object> buffer;
        const char *gif_buffer;
        size_t gif_length;
        size_t spewed_length;
        int status;
        
        SlurpBaton(Foozle *foozle, Handle<Function> cb_, Handle<Object> buffer_):
            Baton(foozle, cb_)/*, buffer(buffer_)*/, status(GIF_OK) {
            buffer = Persistent<Object>::New(buffer_);
            gif_buffer = Buffer::Data(buffer_);
            gif_length = Buffer::Length(buffer_);
            spewed_length = 0;
        }
        
        virtual ~SlurpBaton() {
            buffer.Dispose();
        }
        
        static int ReadMemoryGif (GifFileType *gif_file, GifByteType *buffer, int size);
    };
    
    struct StretchBaton : Baton {
        double source_left;
        double source_right;
        double source_top;
        double source_bottom;
        
        int result_width;
        int result_height;
        
        // length must be result_width * result_height; check on creation
        Persistent<Object> dest_buffer;
        int *dest_pixels;
        
        StretchBaton(Foozle *foozle, Handle<Function> cb_, Handle<Object> dest_buffer_,
                   double source_left_, double source_right_, double source_top_, double source_bottom_,
                   int result_width_, int result_height_):
            Baton(foozle, cb_),
            source_left(source_left_), source_right(source_right_), source_top(source_top_), source_bottom(source_bottom_),
            result_width(result_width_), result_height(result_height_)
        {
            dest_buffer = Persistent<Object>::New(dest_buffer_);
            dest_pixels = (int *)Buffer::Data(dest_buffer);
        }
        
        virtual ~StretchBaton() {
            dest_buffer.Dispose();
        }
    };
    
    static void Work_BeginSlurp(Baton* baton);
    static void Work_Slurp(uv_work_t* req);
    static void Work_AfterSlurp(uv_work_t* req);
    
    static void Work_BeginStretch(Baton* baton);
    static void Work_Stretch(uv_work_t* req);
    static void Work_AfterStretch(uv_work_t* req);
    
private:
    unsigned char *pixels;
    int gif_width, gif_height;
    int palette[256];
};

}

#endif
