
#define REQUIRE_ARGUMENT_INTEGER(ARG_INDEX, NAME) \
  int32_t NAME; \
  status = napi_get_value_int32(env, argv[ARG_INDEX], &NAME); \
  if (status != napi_ok) { \
    error = invalid_arguments_error; \
    goto out; \
  }

#define REQUIRE_ARGUMENT_DOUBLE(ARG_INDEX, NAME) \
  double NAME; \
  status = napi_get_value_double(env, argv[ARG_INDEX], &NAME); \
  if (status != napi_ok) { \
    error = invalid_arguments_error; \
    goto out; \
  }

#define REQUIRE_ARGUMENT_BOOLEAN(ARG_INDEX, NAME) \
  bool NAME; \
  status = napi_get_value_bool(env, argv[ARG_INDEX], &NAME); \
  if (status != napi_ok) { \
    error = invalid_arguments_error; \
    goto out; \
  }

#define REQUIRE_ARGUMENT_BUFFER(ARG_INDEX, DATA, LENGTH) \
  size_t LENGTH; \
  void *DATA; \
  { \
    bool is_buffer; \
    status = napi_is_buffer(env, argv[ARG_INDEX], &is_buffer); \
    if (status != napi_ok || !is_buffer) { \
      error = invalid_arguments_error; \
      goto out; \
    } \
  } \
  status = napi_get_buffer_info(env, argv[ARG_INDEX], &DATA, &LENGTH); \
  if (status != napi_ok) { \
    error = invalid_arguments_error; \
    goto out; \
  }

#define REQUIRE_ARGUMENT_BUFFER_REF(ARG_INDEX, DATA, LENGTH, REF) \
  size_t LENGTH; \
  void *DATA; \
  { \
    bool is_buffer; \
    status = napi_is_buffer(env, argv[ARG_INDEX], &is_buffer); \
    if (status != napi_ok || !is_buffer) { \
      error = invalid_arguments_error; \
      goto out; \
    } \
  } \
  status = napi_get_buffer_info(env, argv[ARG_INDEX], &DATA, &LENGTH); \
  if (status != napi_ok) { \
    error = invalid_arguments_error; \
    goto out; \
  } \
  status = napi_create_reference(env, argv[ARG_INDEX], 1, &REF); \
  if (status != napi_ok) goto out;
