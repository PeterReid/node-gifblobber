#include <node_api.h>
#include <memory>
#include "macros.h"

static const int UNFILTERED_BLANK_OUT_UNTIL = 6;
static const int FILTERED_BLANK_OUT_UNTIL = 9;

struct stretch_baton {
  double source_left;
  double source_right;
  double source_top;
  double source_bottom;

  int source_width;
  int source_height;
  unsigned char *source_pixels;
  
  int result_width;
  int result_height;
  int *dest_pixels;

  int *filtered_palette;
  int *unfiltered_palette;
  
  bool filtered;

  napi_async_work work; // So we can delete when we are done
  napi_ref callback_ref;
  napi_ref dest_buffer_ref;
  napi_ref source_buffer_ref;
  napi_ref unfiltered_palette_buffer_ref;
  napi_ref filtered_palette_buffer_ref;
};

static inline int clamp(int inclusive_min, int x, int inclusive_max) {
  return x <= inclusive_min ? inclusive_min
       : x >= inclusive_max ? inclusive_max
       : x;
}


#define SHIFT 20

/*
 * ul - upper left corner color, left-shifted by SHIFT
 * ur - upper right corner color, left-shifted by SHIFT
 * bl - bottom left corner color, left-shifted by SHIFT
 * br - bottom right corner color, left-shifted by SHIFT
 * width, height - dimensions of quad to draw
 * output - pixels to draw onto. One int per pixel
 * output_stride - how many ints to step to move to the next row
 * palette - 256 color entries, corresponding to interpolated colors
 */
static void interp_quad(int ul, int ur, int bl, int br, int width, int height, int *output, int output_stride, int *palette) {
    if (width==0 || height==0) return;

    int leftIncr = (bl-ul)/height;
    int rightIncr = (br-ur)/height;

    int horizIncr = (ur-ul)/width;
    int sideDeltaIncr = (rightIncr - leftIncr)/width;

    int left=ul;
    for (int y = 0; y < height; y++) {
        int val = left;
        int *row_ptr = output;
        for (int x = 0; x < width; x++) {
            int result = palette[0xff & ((val + (1<<(SHIFT-1))) >> SHIFT)];
            *row_ptr = result;

            row_ptr++;

            val += horizIncr;
        }
        output += output_stride;
        horizIncr += sideDeltaIncr;
        left += leftIncr;
    }
}


void stretch_execute(napi_env env, void* data)
{
  stretch_baton *baton = (stretch_baton *)data;
    
  double source_height = baton->source_bottom - baton->source_top;
  double source_width = baton->source_right - baton->source_left;
  bool zoomed_in = source_width * 2 < baton->result_width;
  int clamp_min = baton->filtered ? FILTERED_BLANK_OUT_UNTIL : UNFILTERED_BLANK_OUT_UNTIL;
  int clamp_max = 22;
  int *palette = baton->filtered ? baton->filtered_palette : baton->unfiltered_palette;
  
  //memset(baton->dest_pixels, 0, baton->result_width * baton->result_height * 4);

  if (zoomed_in) {
      // We are zoomed in enough to require fancy interpolation
      int min_in_x = clamp(0, (int)baton->source_left, baton->source_width-1);
      int max_in_x = clamp(0, (int)baton->source_right, baton->source_width-1);
      int min_in_y = clamp(0, (int)baton->source_top, baton->source_height-1);
      int max_in_y = clamp(0, (int)baton->source_bottom, baton->source_height-1);

      double height_ratio = baton->result_height / source_height;
      double width_ratio = baton->result_width / source_width;

      int out_y1, out_y2;

      out_y2 = (int)((min_in_y - baton->source_top) * height_ratio);

      for (int in_y = min_in_y; in_y <= max_in_y; in_y++) {
        out_y1 = out_y2;
        out_y2 = (int)(((in_y+1) - baton->source_top) * height_ratio);

        int ul, ur, bl, br;
        int out_x1, out_x2;

        ur = clamp(clamp_min, baton->source_pixels[min_in_x + in_y*baton->source_width], clamp_max) << SHIFT;
        br = clamp(clamp_min, baton->source_pixels[min_in_x + (in_y+1)*baton->source_width], clamp_max) << SHIFT;
        out_x2 = (int)((min_in_x - baton->source_left) * width_ratio);

        for (int in_x = min_in_x; in_x <= max_in_x; in_x++) {
          ul = ur; // This quad's left is the old quad's right
          bl = br;
          ur = clamp(clamp_min, baton->source_pixels[(in_x+1) + in_y*baton->source_width], clamp_max) << SHIFT;
          br = clamp(clamp_min, baton->source_pixels[(in_x+1) + (in_y+1)*baton->source_width], clamp_max) << SHIFT;

          out_x1 = out_x2;
          out_x2 = (int)(((in_x+1) - baton->source_left) * width_ratio);

          int this_out_y1 = out_y1, this_out_y2 = out_y2;
          if (this_out_y1 < 0) {
            ul = ul + (bl - ul)*(0-out_y1)/(out_y2-out_y1);
            ur = ur + (br - ur)*(0-out_y1)/(out_y2-out_y1);
            this_out_y1 = 0;
          }
          if (this_out_y2 > baton->result_height) {
            bl = (ul-bl)*(out_y2-baton->result_height)/(out_y2-out_y1) + bl;
            br = (ur-br)*(out_y2-baton->result_height)/(out_y2-out_y1) + br;
            this_out_y2=baton->result_height;
          }
          if (out_x1 < 0) {
            ul = ul + (ur - ul)*(0-out_x1)/(out_x2-out_x1);
            bl = bl + (br - bl)*(0-out_x1)/(out_x2-out_x1);
            out_x1 = 0;
          }
          if (out_x2 > baton->result_width) {
            ur = (ul-ur)*(out_x2-baton->result_width)/(out_x2-out_x1) + ur;
            br = (bl-br)*(out_x2-baton->result_width)/(out_x2-out_x1) + br;
            out_x2=baton->result_width;
          }
          interp_quad(ul, ur, bl, br, out_x2-out_x1, this_out_y2-this_out_y1,
              baton->dest_pixels + out_x1 + (baton->result_width*this_out_y1),
              baton->result_width,
              palette);
        }
      }
  } else {
      int i=0;
#define ZOOM_OUT_SHIFT 15
      //int min_out_x = clamp(0, (0-baton->source_left)*width_ratio, baton->result_width);
      //int max_out_x = clamp(0, (baton->source_width-baton->source_left)*width_ratio, baton->result_width);

      int x_step_shifted = (int)(source_width/baton->result_width*(1<<ZOOM_OUT_SHIFT) + .5);
      int in_x_shifted_initial = (int)(baton->source_left*(1<<ZOOM_OUT_SHIFT) + 0.5);
      for (int out_y = 0; out_y < baton->result_height; out_y++) {
        int in_y = (int)(out_y*source_height/baton->result_height + baton->source_top);
        if (in_y < 0) {
          i += baton->result_width;
          continue;
        }
        if (in_y >= baton->source_height) break;

        unsigned char *scan_line = baton->source_pixels + baton->source_width*in_y;

        // todo: find bounds first, then loop
        int in_x_shifted = in_x_shifted_initial;
        for (int out_x=0; out_x < baton->result_width; out_x++, in_x_shifted += x_step_shifted) {
          int in_x = in_x_shifted >> ZOOM_OUT_SHIFT;

          if (in_x >=0 && in_x < baton->source_width) {
            baton->dest_pixels[i] = palette[scan_line[in_x]];
          }

          i++;
        }
      }
  }
}

void stretch_complete(napi_env env, napi_status status, void* data)
{
  stretch_baton *baton = (stretch_baton *)data;
  
  napi_value cb;
  status = napi_get_reference_value(env, baton->callback_ref, &cb);
  if (status != napi_ok) goto out;
  
  napi_value err;
  
  status = napi_get_null(env, &err);
  if (status != napi_ok) goto out;

  napi_value args[1];
  args[0] = err;

  napi_value result;
  napi_call_function(env, cb, cb, 1, args, &result);
  
  
out:
  napi_delete_async_work(env, baton->work);
  napi_delete_reference(env, baton->callback_ref);
  napi_delete_reference(env, baton->dest_buffer_ref);
  napi_delete_reference(env, baton->source_buffer_ref);
  napi_delete_reference(env, baton->unfiltered_palette_buffer_ref);
  napi_delete_reference(env, baton->filtered_palette_buffer_ref);
  
  delete baton;
}


napi_value stretch(napi_env env, napi_callback_info cbinfo) {
  napi_status status;
  napi_async_work work = nullptr;
  napi_ref callback_ref = nullptr;
  napi_ref source_buffer_ref = nullptr;
  napi_ref dest_buffer_ref = nullptr;
  napi_ref unfiltered_palette_buffer_ref = nullptr;
  napi_ref filtered_palette_buffer_ref = nullptr;
  
  const char *error = nullptr;
  const char *invalid_arguments_error = "invalid argument types";
  napi_value cbinfo_this;
  void *cbinfo_data;
  size_t argc = 14;
  napi_value argv[14];

  stretch_baton *baton = nullptr;

  status = napi_get_cb_info(env, cbinfo, &argc, argv, &cbinfo_this, &cbinfo_data);
  if (status != napi_ok) goto out;
  if (argc < 10) {
    error = "Wrong number of arguments.";
    goto out;
  }

  
  baton = new stretch_baton();
  
  

  REQUIRE_ARGUMENT_BUFFER_REF(0, source_buffer, source_buffer_length, source_buffer_ref);
  REQUIRE_ARGUMENT_INTEGER(1, source_width);
  REQUIRE_ARGUMENT_INTEGER(2, source_height);
  REQUIRE_ARGUMENT_BUFFER_REF(3, unfiltered_palette, unfiltered_palette_length, unfiltered_palette_buffer_ref);
  REQUIRE_ARGUMENT_BUFFER_REF(4, filtered_palette, filtered_palette_length, filtered_palette_buffer_ref);
  
  REQUIRE_ARGUMENT_DOUBLE(5, source_left);
  REQUIRE_ARGUMENT_DOUBLE(6, source_right);
  REQUIRE_ARGUMENT_DOUBLE(7, source_top);
  REQUIRE_ARGUMENT_DOUBLE(8, source_bottom);
  REQUIRE_ARGUMENT_INTEGER(9, result_width);
  REQUIRE_ARGUMENT_INTEGER(10, result_height);
  REQUIRE_ARGUMENT_BOOLEAN(11, filtered);
  REQUIRE_ARGUMENT_BUFFER_REF(12, dest_buffer, dest_buffer_length, dest_buffer_ref);
  
  status = napi_create_reference(env, argv[13], 1, &callback_ref); \
  if (status != napi_ok) goto out;
  
  if (filtered_palette_length != 256*4 || unfiltered_palette_length != 256*4) {
      error = "Palette buffers must be of length 256";
      goto out;
  }
  if (result_width <= 0 || result_height <= 0 || result_width*result_height*4 != (int32_t)dest_buffer_length) {
      error = "Buffer length is not consistent with given width and height";
      goto out;
  }
  if (source_width <= 0 || source_height <= 0 || source_width*source_height != (int32_t)source_buffer_length) {
      error = "Buffer length is not consistent with given width and height";
      goto out;
  }

  napi_value stretch_description;
  status = napi_create_string_utf8(env, "gif stretch", NAPI_AUTO_LENGTH, &stretch_description);
  if (status != napi_ok) goto out;
  
  status = napi_create_async_work(env, nullptr, stretch_description, stretch_execute, stretch_complete, baton, &work); 
  if (status != napi_ok) goto out;
  
  
  baton->source_left = source_left;
  baton->source_right = source_right;
  baton->source_top = source_top;
  baton->source_bottom = source_bottom;
  baton->source_width = source_width;
  baton->source_height = source_height;
  baton->source_pixels = (unsigned char *)source_buffer;
  baton->result_width = result_width;
  baton->result_height = result_height;
  baton->dest_pixels = (int *)dest_buffer;
  baton->filtered = filtered;
  baton->filtered_palette = (int *)filtered_palette;
  baton->unfiltered_palette = (int *)unfiltered_palette;
  baton->callback_ref = callback_ref;
  baton->dest_buffer_ref = dest_buffer_ref;
  baton->source_buffer_ref = source_buffer_ref;
  baton->unfiltered_palette_buffer_ref = unfiltered_palette_buffer_ref;
  baton->filtered_palette_buffer_ref = filtered_palette_buffer_ref;
  baton->work = work;
  
  status = napi_queue_async_work(env, work);
  if (status != napi_ok) goto out;
    
  baton = nullptr;
  work = nullptr;
  callback_ref = nullptr;
  source_buffer_ref = nullptr;
  dest_buffer_ref = nullptr;
  unfiltered_palette_buffer_ref = nullptr;
  filtered_palette_buffer_ref = nullptr;
  
out:
  if (callback_ref) napi_delete_reference(env, callback_ref);
  if (source_buffer_ref) napi_delete_reference(env, source_buffer_ref);
  if (dest_buffer_ref) napi_delete_reference(env, dest_buffer_ref);
  if (unfiltered_palette_buffer_ref) napi_delete_reference(env, unfiltered_palette_buffer_ref);
  if (filtered_palette_buffer_ref) napi_delete_reference(env, filtered_palette_buffer_ref);
  if (work) napi_delete_async_work(env, work);
  if (baton) delete baton;
  
  if (error) {
    napi_throw_error(env, NULL, error);
  }
  return nullptr;
}
