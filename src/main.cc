#include <node_api.h>

napi_value slurp(napi_env env, napi_callback_info cbinfo);
napi_value stretch(napi_env env, napi_callback_info cbinfo);

#define CREATE_FUNCTION(NAME, IMPLEMENTATION) \
  status = napi_create_function(env, nullptr, 0, IMPLEMENTATION, nullptr, &fn); \
  if (status != napi_ok) return nullptr; \
  status = napi_set_named_property(env, exports, NAME, fn); \
  if (status != napi_ok) return nullptr;

napi_value init(napi_env env, napi_value exports) {
  napi_status status;
  napi_value fn;

  CREATE_FUNCTION("slurp", slurp);
  CREATE_FUNCTION("stretch", stretch);
  
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
