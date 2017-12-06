#include <nan.h>

using namespace v8;
using namespace node;

namespace libav_decoder {

  void decoder_init(Handle<Object>);

  void Initialize(Handle<Object> target) {
    decoder_init(target);
  }

  NODE_MODULE(addon, libav_decoder::Initialize)

}
