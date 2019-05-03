#include <node_api.h>
#include <gif_lib.h>
#include <stdlib.h>
#include <string.h>

#define RADAR_COLOR_COUNT 15

static const int UNFILTERED_BLANK_OUT_UNTIL = 6;
static const int FILTERED_BLANK_OUT_UNTIL = 9;


struct slurp_baton {
  napi_async_work work; // So we can delete when we are done
  napi_ref callback;
  napi_ref gif_buffer_ref;
  
  const char *gif_buffer;
  size_t gif_length;
  size_t spewed_length;
  int status;
  
  int width;
  int height;
  
  unsigned char *pixels;
  uint32_t filtered_palette[256];
  uint32_t unfiltered_palette[256];
};

int ReadMemoryGif (GifFileType *gif_file, GifByteType *buffer, int size) {
  slurp_baton *baton = static_cast<slurp_baton *>(gif_file->UserData);
  if (baton->spewed_length == baton->gif_length) {
    return 0;
  }

  size_t copy_length = baton->gif_length - baton->spewed_length;
  if (copy_length > (size_t)size) {
    // Trim to request
    copy_length = (size_t)size;
  }

  memcpy(buffer, baton->gif_buffer + baton->spewed_length, copy_length);
  baton->spewed_length += copy_length;
  return copy_length;
}

static inline int clamp(int inclusive_min, int x, int inclusive_max) {
  return x <= inclusive_min ? inclusive_min
       : x >= inclusive_max ? inclusive_max
       : x;
}


void slurp_gif_execute(napi_env env, void* data)
{
  slurp_baton *baton = (slurp_baton *)data;

  baton->status = GIF_OK;
  GifFileType *gif_file = DGifOpen(baton, ReadMemoryGif, &baton->status);
  if (baton->status != GIF_OK) {
    DGifCloseFile(gif_file);
    return;
  }
  baton->status = DGifSlurp(gif_file);

  if (baton->status != GIF_OK) {
    DGifCloseFile(gif_file);
    return;
  }

  if (gif_file->SColorMap) {
    int colors = clamp(0, gif_file->SColorMap->ColorCount, 256);

    for (int i = 0; i < colors; i++) {
      GifColorType color = gif_file->SColorMap->Colors[i];
      int alphaed = color.Red | (color.Green<<8) | (color.Blue<<16) | 0x40000000;
      baton->unfiltered_palette[i] = i <= UNFILTERED_BLANK_OUT_UNTIL ? 0 : alphaed;
      baton->filtered_palette[i] = i <= FILTERED_BLANK_OUT_UNTIL ? 0 : alphaed;
    }
  }

  baton->width = gif_file->SWidth;
  baton->height = gif_file->SHeight;
  int pixel_count = gif_file->SWidth * gif_file->SHeight;
  unsigned char *filtered = new unsigned char[pixel_count];
  unsigned char *unfiltered = gif_file->SavedImages[0].RasterBits;
  for (int i = 0; i < pixel_count; i++) {
    filtered[i] = unfiltered[i];
  }

  DGifCloseFile(gif_file);

  baton->pixels = filtered;
}

void slurp_gif_complete(napi_env env, napi_status status, void* data)
{
  slurp_baton *baton = (slurp_baton *)data;
  
  napi_value cb;
  status = napi_get_reference_value(env, baton->callback, &cb);
  if (status != napi_ok) goto out;
  
  napi_value err;
  napi_value width;
  napi_value height;
  napi_value pixel_buffer;
  napi_value unfiltered_palette_buffer;
  napi_value filtered_palette_buffer;
  
  void *buffer_data;
  
  status = napi_get_null(env, &err);
  if (status != napi_ok) goto out;

  status = napi_create_int32(env, baton->width, &width);
  if (status != napi_ok) goto out;
  
  status = napi_create_int32(env, baton->height, &height);
  if (status != napi_ok) goto out;
  
  // Pixel buffer
  size_t data_size = baton->width * baton->height;
  status = napi_create_buffer(env, data_size, &buffer_data, &pixel_buffer);
  if (status != napi_ok) goto out;
  memcpy(buffer_data, baton->pixels, data_size);
  
  // Unfiltered palette buffer
  status = napi_create_buffer(env, 256*4, &buffer_data, &unfiltered_palette_buffer);
  if (status != napi_ok) goto out;
  memcpy(buffer_data, baton->unfiltered_palette, 256*4);
  
  // Filtered palette buffer
  status = napi_create_buffer(env, 256*4, &buffer_data, &filtered_palette_buffer);
  if (status != napi_ok) goto out;
  memcpy(buffer_data, baton->filtered_palette, 256*4);
  
  napi_value args[6];
  args[0] = err;
  args[1] = width;
  args[2] = height;
  args[3] = pixel_buffer;
  args[4] = unfiltered_palette_buffer;
  args[5] = filtered_palette_buffer;

  napi_value result;
  napi_call_function(env, cb, cb, 6, args, &result);
  
  
out:
  delete[] baton->pixels;
  napi_delete_async_work(env, baton->work);
  napi_delete_reference(env, baton->callback);
  napi_delete_reference(env, baton->gif_buffer_ref);
  
  delete baton;
}

napi_value slurp(napi_env env, napi_callback_info cbinfo) {
  napi_status status;
  napi_async_work work = nullptr;
  napi_ref callback_ref = nullptr;
  napi_ref buffer_ref = nullptr;
  
  size_t argc = 2;
  napi_value argv[2];
  napi_value cbinfo_this;
  void *cbinfo_data;
  char *gif_bytes;
  size_t gif_byte_count;
  
  slurp_baton *baton = nullptr;

  baton = new slurp_baton();
  
  status = napi_get_cb_info(env, cbinfo, &argc, argv, &cbinfo_this, &cbinfo_data);
  if (status != napi_ok) goto out;
  
  napi_value buffer = argv[0];
  napi_value cb = argv[1];
  
  status = napi_get_buffer_info(env, buffer, &(void *)gif_bytes, &gif_byte_count);
  
  napi_value slurp_description;
  status = napi_create_string_utf8(env, "gif slurp", NAPI_AUTO_LENGTH, &slurp_description);
  if (status != napi_ok) goto out;
  
  status = napi_create_reference(env, cb, 1, &callback_ref);
  if (status != napi_ok) goto out;
  
  status = napi_create_reference(env, buffer, 1, &buffer_ref);
  if (status != napi_ok) goto out;

  status = napi_create_async_work(env, nullptr, slurp_description, slurp_gif_execute, slurp_gif_complete, baton, &work); 
  if (status != napi_ok) goto out;
  
  baton->work = work;
  baton->callback = callback_ref;
  baton->gif_buffer_ref = buffer_ref;
  baton->gif_buffer = gif_bytes;
  baton->gif_length = gif_byte_count;
  baton->spewed_length = 0;
  baton->status = 0;
  baton->width = 0;
  baton->height = 0;
  baton->pixels = nullptr;
  
  status = napi_queue_async_work(env, work);
  if (status != napi_ok) goto out;
    
  baton = nullptr;
  work = nullptr;
  callback_ref = nullptr;
  buffer_ref = nullptr;
out:

  if (callback_ref) napi_delete_reference(env, callback_ref);
  if (buffer_ref) napi_delete_reference(env, buffer_ref);
  if (work) napi_delete_async_work(env, work);
  if (baton) delete baton;
  
  return nullptr;
}
