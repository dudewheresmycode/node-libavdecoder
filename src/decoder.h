
#include <nan.h>
#include <node.h>
#include <node_buffer.h>
#include "node_pointer.h"

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavformat/avio.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/time.h>
  #include <libavutil/opt.h>
}

using namespace v8;
using namespace node;

namespace libav_decoder {

  struct Emitter: Nan::ObjectWrap {
    static NAN_METHOD(New);
    static NAN_METHOD(Open);
    static NAN_METHOD(Decode);
    static NAN_METHOD(ReadVideo);
    static NAN_METHOD(Destroy);
  };


  typedef struct YUVImage {
    // v8::Uint32 format;
    int w, h;
    int planes;

    uint32_t pitchY;
    uint32_t pitchU;
    uint32_t pitchV;

    uint8_t *avY;
    uint8_t *avU;
    uint8_t *avV;

    size_t size_y;
    size_t size_u;
    size_t size_v;

  };

}
