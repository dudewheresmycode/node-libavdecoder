#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif


#include "decoder.h"
#include "time_value.h"
#include "packet_queue.h"


using namespace v8;
using namespace Nan;
using namespace node;


#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define VIDEO_PICTURE_QUEUE_SIZE 1

namespace libav_decoder {
  int frameIndex, frameDecoded, hasDecodedFrame;
  uint64_t totalFrames;
  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVFrame         *pFrameOut = NULL;
  AVFrame         *pFrameCopy = NULL;
  AVPixelFormat   pix_fmt = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_YUV420P; //AV_PIX_FMT_RGB24;
  int             frameFinished;
  int             shouldQuit;
  uint8_t *buffer;
  AVPacket        packet;

  PacketQueue     videoq;
  PacketQueue     audioq;

  struct SwsContext   *sws_ctx            = NULL;
  YUVImage *yuv;
  double vpts;



  void extractYUV(){

    yuv->pitchY = pFrameOut->linesize[0];
    yuv->pitchU = pFrameOut->linesize[1];
    yuv->pitchV = pFrameOut->linesize[2];

    yuv->avY = pFrameOut->data[0];
    yuv->avU = pFrameOut->data[1];
    yuv->avV = pFrameOut->data[2];

    yuv->size_y = (yuv->pitchY * pCodecCtxOrig->coded_height);
    yuv->size_u = (yuv->pitchU * pCodecCtxOrig->coded_height / 2);
    yuv->size_v = (yuv->pitchV * pCodecCtxOrig->coded_height / 2);

  }

  class DecodeReader : public AsyncWorker {
   public:
    DecodeReader(Callback *callback)
      : AsyncWorker(callback) {}
    ~DecodeReader() {}

    void Execute () {
      hasDecodedFrame=false;
      if(videoq.nb_packets > 0){

        AVPacket pkt1, *packet = &pkt1;
        int frameFinished;

        pFrame = av_frame_alloc();
        yuv = new YUVImage;

        int i=0;
        for(;;) {
          // if(shouldQuit){
          //   SetErrorMessage("Quitting...");
          //   break;
          // }
          if(shouldQuit){
            fprintf(stderr, "quit ivoked!\n");
            break;
          }

          if(packet_queue_get(&videoq, packet, 1) < 0) {
            // means we quit getting packets
            fprintf(stderr, "quit getting packets!\n");
            break;
          }
          //fprintf(stderr, "decode...%d\n", packet);

          vpts=0;

          avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);

          if((vpts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
            vpts = 0;
          }
          vpts *= av_q2d(pFormatCtx->streams[videoStream]->time_base);


          if(frameFinished) {
            sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtxOrig->coded_height, pFrameOut->data, pFrameOut->linesize);
            frameDecoded++;
            hasDecodedFrame=true;
            extractYUV();
            break;
          }
          //no frame found.. free this packet and loop again ...
          av_free_packet(packet);
          i++;
        }
        av_frame_free(&pFrame);

      }else{
        fprintf(stderr, "no more packets...\n");
      }


    }

    void HandleOKCallback(){

      Nan::HandleScope scope;
      Local<Object> obj = Nan::New<Object>();
      if(hasDecodedFrame){
        obj->Set(Nan::New<String>("avY").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked());
        obj->Set(Nan::New<String>("avU").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked());
        obj->Set(Nan::New<String>("avV").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked());

        obj->Set(Nan::New<String>("pitchY").ToLocalChecked(), Nan::New<Integer>(yuv->pitchY));
        obj->Set(Nan::New<String>("pitchU").ToLocalChecked(), Nan::New<Integer>(yuv->pitchU));
        obj->Set(Nan::New<String>("pitchV").ToLocalChecked(), Nan::New<Integer>(yuv->pitchV));

        obj->Set(Nan::New<String>("frame").ToLocalChecked(), Nan::New<Number>(frameDecoded));
        obj->Set(Nan::New<String>("pts").ToLocalChecked(), Nan::New<Number>(vpts));
        obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(1));
        if(yuv){
          delete yuv;
        }
      }else{
        obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(0));

      }

      v8::Local<v8::Value> argv[] = {obj};
      callback->Call(1, argv);
      // if(shouldQuit){
      //   v8::Local<v8::Value> argv[] = {
      //     Nan::New<Integer>(-1)
      //   };
      //   callback->Call(2, argv);
      // }else{
      //   v8::Local<v8::Value> argv[] = {
      //     Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
      //     Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
      //     Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
      //     Nan::New<Integer>(yuv->pitchY),
      //     Nan::New<Integer>(yuv->pitchU),
      //     Nan::New<Integer>(yuv->pitchV),
      //     Nan::New<Integer>(videoq.nb_packets),
      //     Nan::New<Number>(vpts)
      //   };
      //   callback->Call(8, argv);
      //   delete yuv;
      // }
    }
    void Destroy(){

    }


  };

  class DecodeWorker : public AsyncProgressWorker {
   public:
    DecodeWorker(Callback *callback, Callback *progress)
      : AsyncProgressWorker(callback), progress(progress) {}
    ~DecodeWorker() {}

    void Execute (const AsyncProgressWorker::ExecutionProgress& progress) {

      fprintf(stderr, "\n----\n------- START DECODING --- \n\n" );

      //Find the decoder for the video stream
      pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
      if(pCodec==NULL) {
        fprintf(stderr, "Unsupported codec!\n");
        exit(-1);
      }

      // Copy context
      pCodecCtx = avcodec_alloc_context3(pCodec);
      if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        exit(-1);
      }

      // Open codec
      if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        fprintf(stderr, "find videoStream failed\n");
      }


      AVPacket pkt1, *packet = &pkt1;
      frameIndex=0;
      frameDecoded=0;
      totalFrames=pFormatCtx->streams[videoStream]->nb_frames;

      pFrameCopy = av_frame_alloc();
      pFrameOut = av_frame_alloc();

      int size = avpicture_get_size(pix_fmt, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);
      uint8_t* buffer = (uint8_t*)av_malloc(size);

      int sizeout = avpicture_get_size(pix_fmt, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);
      uint8_t* bufferout = (uint8_t*)av_malloc(sizeout);

      avpicture_fill((AVPicture *)pFrameCopy, buffer, pix_fmt, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);
      avpicture_fill((AVPicture *)pFrameOut, bufferout, pix_fmt, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);

      sws_ctx = sws_getContext(
           pCodecCtxOrig->coded_width,
           pCodecCtxOrig->coded_height,
           pCodecCtx->pix_fmt,
           pCodecCtxOrig->coded_width,
           pCodecCtxOrig->coded_height,
           pix_fmt,
           SWS_BILINEAR,
           NULL,
           NULL,
           NULL
      );

      int readFrame;

      for(;;) {


        // if(videoq.size > MAX_VIDEOQ_SIZE || audioq.size > MAX_AUDIOQ_SIZE) {
        if(videoq.size > MAX_VIDEOQ_SIZE) {
          // fprintf(stderr, "max video queue size (%d).. waiting 100ms\n", videoq.size);
          progress.Signal();
          Sleep(100); //sleep for a second?
          continue;
        }

        if(readFrame = av_read_frame(pFormatCtx, packet) < 0) {
          if(pFormatCtx->pb->error == 0) {
            //fprintf(stderr, "no error.. sleeping\n");
            continue;
    	       //Sleep(100); /* no error; wait for user input */
             //continue;
          } else {
    	       break;
          }
        }

        if(packet->stream_index == videoStream) {
          frameIndex++;
          packet_queue_put(&videoq, packet);
        } else if(packet->stream_index == audioStream) {
          // packet_queue_put(&audioq, packet);
          // av_free_packet(packet);
        } else {
          fprintf(stderr, "free: %d\n", i);
          // av_free_packet(packet);
        }
        // fprintf(stderr, "read packet: %d\n", readFrame);
        // if(!packet){
          // break;
        // }
      }

    }

    void Destroy(){
      // avformat_close_input(&pFormatCtx);
    }
    void HandleProgressCallback(const char *data, size_t size) {
      Nan::HandleScope scope;
      Local<Object> obj = Nan::New<Object>();
      obj->Set(Nan::New<String>("current").ToLocalChecked(), Nan::New<Integer>(frameIndex));
      obj->Set(Nan::New<String>("total").ToLocalChecked(), Nan::New<Number>(totalFrames));
      obj->Set(Nan::New<String>("queue_size").ToLocalChecked(), Nan::New<Integer>(videoq.nb_packets));
      v8::Local<v8::Value> argv[] = {obj};
      progress->Call(1, argv);
    }
    void HandleOKCallback(){

      fprintf(stderr, "\n----\n------- FINISHED DECODING --- \n\n" );

      Local<Object> obj = Nan::New<Object>();
      obj->Set(Nan::New<String>("current").ToLocalChecked(), Nan::New<Integer>(frameIndex));
      obj->Set(Nan::New<String>("total").ToLocalChecked(), Nan::New<Number>(totalFrames));

      Nan::HandleScope scope;
      v8::Local<v8::Value> argv[] = {
        obj
        // Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked(),
        // Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked(),
        // Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked(),
        // Nan::New<Integer>(yuv->pitchY),
        // Nan::New<Integer>(yuv->pitchU),
        // Nan::New<Integer>(yuv->pitchV),
        // Nan::New<Number>(vpts)
      };
      callback->Call(1, argv);
    }
    // void HandleProgressCallback(const char *data, size_t size) {
    //   Nan::HandleScope scope;
    //   fprintf(stderr, "\n----\n------- PROGRESS: %d of %d \n\n",  frameIndex, pFormatCtx->streams[videoStream]->nb_frames);
    //   v8::Local<v8::Value> argv[] = {
    //     Nan::New<Integer>(frameIndex),
    //     Nan::New<Integer>((int)pFormatCtx->streams[videoStream]->nb_frames)
    //   };
    //   progress->Call(1, argv);
    // }

   private:
    Callback *progress;
  };

  Emitter* self;

  NAN_METHOD(Emitter::New) {
    assert(info.IsConstructCall());
    self = new Emitter();
    self->Wrap(info.This());

    // VideoState      *is;
    // is = av_mallocz(sizeof(VideoState));


    info.GetReturnValue().Set(info.This());
  }
  NAN_METHOD(Emitter::Open) {
    char val_str[128];

    String::Utf8Value cmd(info[0]);
    char *in = (*cmd);

    Callback *callback = new Callback(info[1].As<Function>());
    fprintf(stderr, "Open: %s\n", in);

    if(avformat_open_input(&pFormatCtx, in, NULL, NULL)!=0){
      fprintf(stderr, "Could not open %s\n", in);
      exit(-1);
    }

    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
      fprintf(stderr, "avformat_find_stream_info failed\n");
      avformat_close_input(&pFormatCtx);
      exit(-1);
    }

    videoStream=-1;
    audioStream=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++) {
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0) {
        videoStream=i;
      }
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream < 0) {
        audioStream=i;
      }
    }
    if(videoStream==-1){
      fprintf(stderr, "find videoStream failed\n");
    }
    if(audioStream==-1){
      fprintf(stderr, "find audioStream failed\n");
    }

    //dump input stream infos
    // av_dump_format(pFormatCtx, 0, in, 0);

    pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

    float aspect_ratio;
    if(pCodecCtxOrig->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(pCodecCtxOrig->sample_aspect_ratio) * pCodecCtxOrig->width / pCodecCtxOrig->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)pCodecCtxOrig->width / (float)pCodecCtxOrig->height;
    }

    packet_queue_init(&videoq);

    // fprintf(stderr, "codec: %dx%d %dx%d\n", pCodecCtxOrig->width, pCodecCtxOrig->height, pCodecCtxOrig->coded_width, pCodecCtxOrig->coded_height);



    Local<Object> obj = Nan::New<Object>();
    obj->Set(Nan::New<String>("filename").ToLocalChecked(), Nan::New<String>(in).ToLocalChecked());
    obj->Set(Nan::New<String>("duration").ToLocalChecked(), Nan::New<Number>(atof(time_value_string(val_str, sizeof(val_str), pFormatCtx->duration))));
    obj->Set(Nan::New<String>("width").ToLocalChecked(), Nan::New<Integer>(pCodecCtxOrig->width));
    obj->Set(Nan::New<String>("height").ToLocalChecked(), Nan::New<Integer>(pCodecCtxOrig->height));
    obj->Set(Nan::New<String>("coded_width").ToLocalChecked(), Nan::New<Integer>(pCodecCtxOrig->coded_width));
    obj->Set(Nan::New<String>("coded_height").ToLocalChecked(), Nan::New<Integer>(pCodecCtxOrig->coded_height));
    obj->Set(Nan::New<String>("aspect_ratio").ToLocalChecked(), Nan::New<Number>(aspect_ratio));
    obj->Set(Nan::New<String>("frame_rate").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->r_frame_rate.num/pFormatCtx->streams[videoStream]->r_frame_rate.den));

    Nan:: HandleScope scope;
    Local<Value> argv[] = {obj};
    callback->Call(1, argv);
  }
  NAN_METHOD(Emitter::Decode) {
    Callback *progress = new Callback(info[0].As<v8::Function>());
    Callback *callback = new Callback(info[1].As<v8::Function>());


    //!!uv_queue_work(uv_default_loop(), &request->req, ec_decode_buffer_async, (uv_after_work_cb)ec_decode_buffer_after);

    AsyncQueueWorker(new DecodeWorker(callback, progress));
  }
  NAN_METHOD(Emitter::Destroy) {
    // shouldQuit=true;
    info.GetReturnValue().Set(Nan::New<String>("OK").ToLocalChecked());
  }

  NAN_METHOD(Emitter::ReadVideo) {
    Callback *callback = new Callback(info[0].As<v8::Function>());
    AsyncQueueWorker(new DecodeReader(callback));
  }


  void decoder_init(Handle<Object> target){
    Nan::HandleScope scope;

    av_register_all();

    Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(Emitter::New);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    t->SetClassName(Nan::New("Emitter").ToLocalChecked());
    Nan::SetPrototypeMethod(t, "open", Emitter::Open);
    Nan::SetPrototypeMethod(t, "decode", Emitter::Decode);
    Nan::SetPrototypeMethod(t, "readVideo", Emitter::ReadVideo);
    Nan::SetPrototypeMethod(t, "destroy", Emitter::Destroy);

    Nan::Set(target, Nan::New("Emitter").ToLocalChecked(), t->GetFunction());
  }


}
