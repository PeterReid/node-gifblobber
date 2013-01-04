#include <string.h>
#include <algorithm>

#include "macros.h"
#include "blobber.h"

using namespace node_sqlite3;



#define RADAR_COLOR_COUNT 15


Persistent<FunctionTemplate> Foozle::constructor_template;

void Foozle::Init(Handle<Object> target)
{
    HandleScope scope;
    
    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    
    constructor_template = Persistent<FunctionTemplate>::New(t);
    constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
    constructor_template->SetClassName(String::NewSymbol("Foozle"));
    
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "mash", Mash);
    
    target->Set(String::NewSymbol("Foozle"), 
        constructor_template->GetFunction());
        
}
    
Foozle::Foozle()
{
    pixels = NULL;
    gif_width = 0;
    gif_height = 0;
    for (int i = 0; i < 256; i++) {
        palette[i] = 0;
    }
}

Foozle::~Foozle()
{
    delete[] pixels;
}
    
Handle<Value> Foozle::New(const Arguments& args)
{
    HandleScope scope;

    if (!args.IsConstructCall()) {
        return ThrowException(Exception::TypeError(
            String::New("Use the new operator to create new Foozle objects"))
        );
    }

    REQUIRE_ARGUMENT_BUFFER(0, buffer);
    REQUIRE_ARGUMENT_FUNCTION(1, callback);
    
    Foozle* foozle = new Foozle();
    
    foozle->Wrap(args.This());

    args.This()->Set(String::NewSymbol("fooLevel"), String::NewSymbol("bazzle"), ReadOnly);
    //args.This()->Set(String::NewSymbol("mode"), Integer::New(mode), ReadOnly);

    // Start opening the database.
    SlurpBaton *baton = new SlurpBaton(foozle, callback, buffer);
    Work_BeginSlurp(baton);

    return args.This();
}

Handle<Value> Foozle::Mash(const Arguments& args)
{ 
    HandleScope scope;
    
    Foozle* foozle = ObjectWrap::Unwrap<Foozle>(args.This());
    
    REQUIRE_ARGUMENT_DOUBLE(0, source_left);
    REQUIRE_ARGUMENT_DOUBLE(1, source_right);
    REQUIRE_ARGUMENT_DOUBLE(2, source_top);
    REQUIRE_ARGUMENT_DOUBLE(3, source_bottom);
    REQUIRE_ARGUMENT_INTEGER(4, result_width);
    REQUIRE_ARGUMENT_INTEGER(5, result_height);
    REQUIRE_ARGUMENT_BUFFER(6, dest_buffer);
    REQUIRE_ARGUMENT_FUNCTION(7, callback);
    
    if (result_width <= 0 || result_height <= 0 || result_width*result_height*4 != Buffer::Length(dest_buffer)) {
        return ThrowException(Exception::Error( 
            String::New("Buffer length is not consistent with given width and height"))
        );  
    }
    
    
    StretchBaton *baton = new StretchBaton(foozle, callback, dest_buffer, 
        source_left, source_right, source_top, source_bottom,
        result_width, result_height);
    Work_BeginStretch(baton);
    
    return args.This();
}



void Foozle::Work_BeginSlurp(Baton* baton) {
    int status = uv_queue_work(uv_default_loop(),
        &baton->request, Work_Slurp, Work_AfterSlurp);
    assert(status == 0);
}

int Foozle::SlurpBaton::ReadMemoryGif (GifFileType *gif_file, GifByteType *buffer, int size) {
  SlurpBaton *baton = static_cast<SlurpBaton *>(gif_file->UserData);
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

void Foozle::Work_Slurp(uv_work_t* req) {
    SlurpBaton* baton = static_cast<SlurpBaton*>(req->data);
    Foozle* foozle = baton->foozle;

    baton->status = GIF_OK;
    GifFileType *gif_file = DGifOpen(baton, SlurpBaton::ReadMemoryGif, &baton->status);
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
            foozle->palette[i] = color.Red | (color.Green<<8) | (color.Blue<<16) | 0x40000000;
        }
    }

    foozle->gif_width = gif_file->SWidth;
    foozle->gif_height = gif_file->SHeight;
    int pixel_count = gif_file->SWidth * gif_file->SHeight;
    unsigned char *filtered = new unsigned char[pixel_count];
    unsigned char *unfiltered = gif_file->SavedImages[0].RasterBits;
    const int filterThreshold = 7;
    for (int i = 0; i < pixel_count; i++) {
      filtered[i] = unfiltered[i];
    }
    for (int i = 0; i <= filterThreshold; i++) {
      foozle->palette[i] = 0;
    }

    DGifCloseFile(gif_file);
    
    foozle->pixels = filtered;
}

void Foozle::Work_AfterSlurp(uv_work_t* req) {
    HandleScope scope;
    SlurpBaton* baton = static_cast<SlurpBaton*>(req->data);
    Foozle* foozle = baton->foozle;

    Local<Value> argv[1];
    if (baton->status != GIF_OK) {
        EXCEPTION(String::New("GIF deocode failed"), baton->status, exception);
        argv[0] = exception;
    }
    else {
        argv[0] = Local<Value>::New(Null());
    }

    if (!baton->callback.IsEmpty() && baton->callback->IsFunction()) {
        TRY_CATCH_CALL(foozle->handle_, baton->callback, 1, argv);
    }
    
    delete baton;
}


void Foozle::Work_BeginStretch(Baton* baton) {
    int status = uv_queue_work(uv_default_loop(),
            &baton->request, Work_Stretch, Work_AfterStretch);
    assert(status == 0);
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

void Foozle::Work_Stretch(uv_work_t* req) {
    StretchBaton* baton = static_cast<StretchBaton*>(req->data);
    Foozle* foozle = baton->foozle;
    
    double source_height = baton->source_bottom - baton->source_top;
    double source_width = baton->source_right - baton->source_left;
    bool zoomed_in = source_width * 2 < baton->result_width;

    printf("Stretch: (%f, %f)-(%f, %f)\n",
      baton->source_left, baton->source_top,
      baton->source_right, baton->source_bottom);

    memset(baton->dest_pixels, 0, baton->result_width * baton->result_height * 4);

    if (zoomed_in) {
        // We are zoomed in enough to require fancy interpolation
        int min_in_x = clamp(0, (int)baton->source_left, foozle->gif_width-1);
        int max_in_x = clamp(0, (int)baton->source_right, foozle->gif_width-1);
        int min_in_y = clamp(0, (int)baton->source_top, foozle->gif_height-1);
        int max_in_y = clamp(0, (int)baton->source_bottom, foozle->gif_height-1);
        
        double height_ratio = baton->result_height / source_height;
        double width_ratio = baton->result_width / source_width;

        int out_y1, out_y2;
        
        out_y2 = (int)((min_in_y - baton->source_top) * height_ratio);
          
        for (int in_y = min_in_y; in_y <= max_in_y; in_y++) {
          out_y1 = out_y2;
          out_y2 = (int)(((in_y+1) - baton->source_top) * height_ratio);
          
          int ul, ur, bl, br;
          int out_x1, out_x2;
          
          ur = clamp(7, foozle->pixels[min_in_x + in_y*foozle->gif_width], 22) << SHIFT;
          br = clamp(7, foozle->pixels[min_in_x + (in_y+1)*foozle->gif_width], 22) << SHIFT;
          out_x2 = (int)((min_in_x - baton->source_left) * width_ratio);
            
          for (int in_x = min_in_x; in_x <= max_in_x; in_x++) {
            ul = ur; // This quad's left is the old quad's right
            bl = br;
            ur = clamp(7, foozle->pixels[(in_x+1) + in_y*foozle->gif_width], 22) << SHIFT;
            br = clamp(7, foozle->pixels[(in_x+1) + (in_y+1)*foozle->gif_width], 22) << SHIFT;
            
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
                foozle->palette);
          }
        }
    } else {
        int i=0;
#define ZOOM_OUT_SHIFT 15
        //int min_out_x = clamp(0, (0-baton->source_left)*width_ratio, baton->result_width);
        //int max_out_x = clamp(0, (foozle->gif_width-baton->source_left)*width_ratio, baton->result_width);
        
        int x_step_shifted = (int)(source_width/baton->result_width*(1<<ZOOM_OUT_SHIFT) + .5);
        int in_x_shifted_initial = (int)(baton->source_left*(1<<ZOOM_OUT_SHIFT) + 0.5);
        for (int out_y = 0; out_y < baton->result_height; out_y++) {
          int in_y = (int)(out_y*source_height/baton->result_height + baton->source_top);
          if (in_y < 0) {
            i += baton->result_width;
            continue;
          }
          if (in_y >= foozle->gif_height) break;

          unsigned char *scan_line = foozle->pixels + foozle->gif_width*in_y;
          
          // todo: find bounds first, then loop
          int in_x_shifted = in_x_shifted_initial;
          for (int out_x=0; out_x < baton->result_width; out_x++, in_x_shifted += x_step_shifted) {
            int in_x = in_x_shifted >> ZOOM_OUT_SHIFT;
            
            if (in_x >=0 && in_x < foozle->gif_width) {
              baton->dest_pixels[i] = foozle->palette[scan_line[in_x]];
            }
            
            i++;
          }
        }
    }
}

void Foozle::Work_AfterStretch(uv_work_t* req) {
    HandleScope scope;
    StretchBaton *baton = static_cast<StretchBaton *>(req->data);
    Foozle *foozle = baton->foozle;

    Local<Value> argv[1];
    argv[0] = Local<Value>::New(baton->dest_buffer);
    
    if (!baton->callback.IsEmpty() && baton->callback->IsFunction()) {
        TRY_CATCH_CALL(foozle->handle_, baton->callback, 1, argv);
    }
    
    delete baton;
}


