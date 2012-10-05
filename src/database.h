#ifndef NODE_SQLITE3_SRC_DATABASE_H
#define NODE_SQLITE3_SRC_DATABASE_H

#include <node.h>

#include <string>
#include <queue>

#include <sqlite3.h>
#include <gif_lib.h>
#include "async.h"

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
            printf("~Baton");
            foozle->Unref();
            callback.Dispose();
            printf("~Baton}");
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
            printf("~SlurpBaton");
            buffer.Dispose();
            printf("~SlurpBaton}");
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
            printf("~StretchBaton");
            dest_buffer.Dispose();
            printf("~StretchBaton}");
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
};

class Database : public ObjectWrap {
public:
    static Persistent<FunctionTemplate> constructor_template;
    static void Init(Handle<Object> target);

    static inline bool HasInstance(Handle<Value> val) {
        if (!val->IsObject()) return false;
        Local<Object> obj = val->ToObject();
        return constructor_template->HasInstance(obj);
    }

    struct Baton {
        uv_work_t request;
        Database* db;
        Persistent<Function> callback;
        int status;
        std::string message;

        Baton(Database* db_, Handle<Function> cb_) :
                db(db_), status(SQLITE_OK) {
            db->Ref();
            request.data = this;
            callback = Persistent<Function>::New(cb_);
        }
        virtual ~Baton() {
            db->Unref();
            callback.Dispose();
        }
    };

    struct OpenBaton : Baton {
        std::string filename;
        int mode;
        OpenBaton(Database* db_, Handle<Function> cb_, const char* filename_, int mode_) :
            Baton(db_, cb_), filename(filename_), mode(mode_) {}
    };

    struct ExecBaton : Baton {
        std::string sql;
        ExecBaton(Database* db_, Handle<Function> cb_, const char* sql_) :
            Baton(db_, cb_), sql(sql_) {}
    };

    struct LoadExtensionBaton : Baton {
        std::string filename;
        LoadExtensionBaton(Database* db_, Handle<Function> cb_, const char* filename_) :
            Baton(db_, cb_), filename(filename_) {}
    };

    typedef void (*Work_Callback)(Baton* baton);

    struct Call {
        Call(Work_Callback cb_, Baton* baton_, bool exclusive_ = false) :
            callback(cb_), exclusive(exclusive_), baton(baton_) {};
        Work_Callback callback;
        bool exclusive;
        Baton* baton;
    };

    struct ProfileInfo {
        std::string sql;
        sqlite3_int64 nsecs;
    };

    struct UpdateInfo {
        int type;
        std::string database;
        std::string table;
        sqlite3_int64 rowid;
    };

    bool IsOpen() { return open; }
    bool IsLocked() { return locked; }

    typedef Async<std::string, Database> AsyncTrace;
    typedef Async<ProfileInfo, Database> AsyncProfile;
    typedef Async<UpdateInfo, Database> AsyncUpdate;

    friend class Statement;

protected:
    Database() : ObjectWrap(),
        handle(NULL),
        open(false),
        locked(false),
        pending(0),
        serialize(false),
        debug_trace(NULL),
        debug_profile(NULL) {

    }

    ~Database() {
        RemoveCallbacks();
        sqlite3_close(handle);
        handle = NULL;
        open = false;
    }

    static Handle<Value> New(const Arguments& args);
    static void Work_BeginOpen(Baton* baton);
    static void Work_Open(uv_work_t* req);
    static void Work_AfterOpen(uv_work_t* req);

    static Handle<Value> OpenGetter(Local<String> str, const AccessorInfo& accessor);

    void Schedule(Work_Callback callback, Baton* baton, bool exclusive = false);
    void Process();

    static Handle<Value> Exec(const Arguments& args);
    static void Work_BeginExec(Baton* baton);
    static void Work_Exec(uv_work_t* req);
    static void Work_AfterExec(uv_work_t* req);

    static Handle<Value> Close(const Arguments& args);
    static void Work_BeginClose(Baton* baton);
    static void Work_Close(uv_work_t* req);
    static void Work_AfterClose(uv_work_t* req);

    static Handle<Value> LoadExtension(const Arguments& args);
    static void Work_BeginLoadExtension(Baton* baton);
    static void Work_LoadExtension(uv_work_t* req);
    static void Work_AfterLoadExtension(uv_work_t* req);

    static Handle<Value> Serialize(const Arguments& args);
    static Handle<Value> Parallelize(const Arguments& args);

    static Handle<Value> Configure(const Arguments& args);

    static void SetBusyTimeout(Baton* baton);

    static void RegisterTraceCallback(Baton* baton);
    static void TraceCallback(void* db, const char* sql);
    static void TraceCallback(Database* db, std::string* sql);

    static void RegisterProfileCallback(Baton* baton);
    static void ProfileCallback(void* db, const char* sql, sqlite3_uint64 nsecs);
    static void ProfileCallback(Database* db, ProfileInfo* info);

    static void RegisterUpdateCallback(Baton* baton);
    static void UpdateCallback(void* db, int type, const char* database, const char* table, sqlite3_int64 rowid);
    static void UpdateCallback(Database* db, UpdateInfo* info);

    void RemoveCallbacks();

protected:
    sqlite3* handle;

    bool open;
    bool locked;
    unsigned int pending;

    bool serialize;

    std::queue<Call*> queue;

    AsyncTrace* debug_trace;
    AsyncProfile* debug_profile;
    AsyncUpdate* update_event;
};

}

#endif
