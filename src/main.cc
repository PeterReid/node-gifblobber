#include "blobber.h"
#include "decoder.h"
#include "blobber.h"

namespace {
  void RegisterModule(v8::Handle<Object> target) {
      node_sqlite3::Foozle::Init(target);
      NODE_SET_METHOD(target, "decode", Decode);
  }
}


NODE_MODULE(node_gifblobber, RegisterModule);