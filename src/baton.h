#ifndef NODE_PNG_ENCODE_SRC_BATON_H
#define NODE_PNG_ENCODE_SRC_BATON_H

struct Baton {
    uv_work_t request;
    Persistent<Function> callback;
    Persistent<Object> context;
    
    Baton(Handle<Function> cb_) {
        request.data = this;
        callback = Persistent<Function>::New(cb_);
    }
    virtual ~Baton() {
        callback.Dispose();
        context.Dispose();
    }
};

#endif