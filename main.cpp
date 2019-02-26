#include "darmstadt.h"

int fd, primefd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmVBlank vblank;

unsigned int planeid;

int frame;
struct timespec btime, vtime, dtime;

int dw, dh;

pthread_t thr;
std::queue<qFrame> frames;

VADisplay vaInst;
VASurfaceAttribExternalBuffers vaBD;
VAContextID scalerC;
VASurfaceID surface;
VASurfaceID thisFluctuates;
VAConfigID vaConf;
VABufferID scalerBuf;
VASurfaceAttrib vaAttr[2];
VAProcPipelineParameterBuffer scaler;
VARectangle scaleRegion;
VAImage img;
VAImageFormat imgFormat;
VAImageFormat allowedFormats[2048];
int allowedFormatsSize;
int theFormat;
int vaStat;

int fuck, shit;

unsigned char* addr;
unsigned long portedFD;
struct timespec tStart, tEnd;

AVCodecContext* encoder;
AVCodec* encInfo;
AVFrame* hardFrame;
AVBufferRef* ffhardInst;
AVBufferRef* wrappedSource;
AVHWDeviceContext* extractSource;
AVBufferRef* hardFrameDataR;
AVFrame* hardFrameRT;
AVVAAPIFramesContext* massAbbrev;
AVDictionary* encOpt;

AVFormatContext* out;
AVStream* stream;

AVDictionary* badCodingPractice=NULL;

FILE* f;

const char* outname="out.ts";

void* unbuff(void* data) {
  qFrame f;
  
  while (1) {
    while (!frames.empty()) {
      f=frames.back();
      frames.pop();
      printf("popped frame. there are now %lu frames\n",frames.size());
      /*
      
      */
    }
    usleep(1000);
  }
  return NULL;
}

static int encode_write(AVCodecContext *avctx, AVFrame *frame, AVFormatContext *fout, AVStream* str)
{
    int ret = 0;
    struct timespec midTimeS, midTimeE;
    AVPacket enc_pkt;
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    midTimeS=curTime(CLOCK_MONOTONIC);
    if ((ret = avcodec_send_frame(avctx, frame)) < 0) {
        fprintf(stderr, "Error code: %d\n",ret);
        goto end;
    }
    //midTimeE=curTime(CLOCK_MONOTONIC); printf("midTime: %s\n",tstos(midTimeE-midTimeS).c_str());
    while (1) {
        ret = avcodec_receive_packet(avctx, &enc_pkt);
        if (ret)
            break;
        enc_pkt.stream_index = 0;
        str->cur_dts = frame->pts-1;
        enc_pkt.pts = frame->pts;
        if (av_write_frame(fout,&enc_pkt)<0) {
          printf("unable to write frame");
}
        avformat_flush(fout);
        avio_flush(fout->pb);
        av_packet_unref(&enc_pkt);
    }
end:
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

void handleTerm(int data) {
  quit=true;
}

int main(int argc, char** argv) {
  drmVersionPtr ver;
  struct sigaction saTerm;
  saTerm.sa_handler=handleTerm;
  sigemptyset(&saTerm.sa_mask);
  if (argc>1) {
    outname=argv[1];
  }
  // open card
  fd=open(DEVICE_PATH,O_RDWR);
  if (fd<0) {
    perror("couldn't open card");
    return 1;
  }
  ver=drmGetVersion(fd);
  if (ver==NULL) {
    printf("could not get version.\n");
    return 1;
  }
  drmFreeVersion(ver);
  printf("video card opened.\n");
  
  if (drmSetClientCap(fd,DRM_CLIENT_CAP_UNIVERSAL_PLANES,1)<0) {
    printf("i have to tell you... that universal planes can't happen...\n");
    return 1;
  }
  
  planeres=drmModeGetPlaneResources(fd);
  if (planeres==NULL) {
    printf("i have to tell you... that I am unable to get plane resources...\n");
    return 1;
  }
  
  if (planeres->count_planes<0) {
    printf("no planes. there are helicopters instead...\n");
    return 1;
  }
  
  printf("number of planes: %d\n",planeres->count_planes);
  for (unsigned int i=0; i<planeres->count_planes; i++) {
    plane=drmModeGetPlane(fd,planeres->planes[i]);
    if (plane==NULL) {
      printf("skipping plane %d...\n",planeres->planes[i]);
      continue;
    }
    
    if (plane->fb_id==0) {
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }
    
    printf("using plane %d.\n",plane->plane_id);
    planeid=plane->plane_id;
    break;
  }
  
  if (plane==NULL) {
    printf("couldn't find any usable planes...\n");
    return 1;
  }
  
  fb=drmModeGetFB(fd,plane->fb_id);
  if (fb==NULL) {
    printf("could not get framebuffer...\n");
    return 1;
  }
  
  if (!fb->handle) {
    printf("setcap cap_sys_admin=ep\n");
    return 1;
  }
  
  dw=fb->width;
  dh=fb->height;
  
  if (planeres) drmModeFreePlaneResources(planeres);
  if (plane) drmModeFreePlane(plane);
  if (fb) drmModeFreeFB(fb);
  
  vaInst=vaGetDisplayDRM(fd);
  if (!vaDisplayIsValid(vaInst)) {
    printf("could not open VA-API...\n");
    return 1;
  }
  if (vaInitialize(vaInst,&fuck,&shit)!=VA_STATUS_SUCCESS) {
    printf("could not initialize VA-API...\n");
    return 1;
  }
  
  printf("va-api instance opened (%s).\n",vaQueryVendorString(vaInst));
  
  vaQueryImageFormats(vaInst,allowedFormats,&allowedFormatsSize);
  
  for (int i=0; i<allowedFormatsSize; i++) {
    printf("format %d: - %.8x\n",i,allowedFormats[i].fourcc);
    if (allowedFormats[i].fourcc==VA_FOURCC_BGRX) {
      printf("%d is the format!\n",i);
      theFormat=i;
      break;
    }
  }
  
  if ((vaStat=vaCreateSurfaces(vaInst,VA_RT_FORMAT_YUV420,dw,dh,&thisFluctuates,1,NULL,0))!=VA_STATUS_SUCCESS) {
      printf("could not create surface for encoding... %x\n",vaStat);
      return 1;
    }
  
  if (vaCreateConfig(vaInst,VAProfileNone,VAEntrypointVideoProc,NULL,0,&vaConf)!=VA_STATUS_SUCCESS) {
    printf("no videoproc.\n");
    return 1;
  }
  if (vaCreateContext(vaInst,vaConf,dw,dh,VA_PROGRESSIVE,&thisFluctuates,1,&scalerC)!=VA_STATUS_SUCCESS) {
    printf("we can't scale. bullshit\n");
    return 1;
  }
  
  wrappedSource=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
  extractSource=(AVHWDeviceContext*)wrappedSource->data;
  
  printf("creating FFmpeg hardware instance\n");
  if (av_hwdevice_ctx_create_derived(&ffhardInst, AV_HWDEVICE_TYPE_VAAPI, wrappedSource, 0)<0) {
    printf("check me later\n");
  }
  ((AVVAAPIDeviceContext*)(extractSource->hwctx))->display=vaInst;
  printf("pre\n");
  av_hwdevice_ctx_init(ffhardInst);
  printf("post!\n");
  
  printf("creating encoder\n");
  if ((encInfo=avcodec_find_encoder_by_name("hevc_vaapi"))==NULL) {
    printf("could not find encoder...\n");
    return 1;
  }
  encoder=avcodec_alloc_context3(encInfo);
  encoder->width=dw;
  encoder->height=dh;
  encoder->time_base=(AVRational){1,900};
  //encoder->framerate=(AVRational){1000,1};
  encoder->sample_aspect_ratio=(AVRational){1,1};
  encoder->pix_fmt=AV_PIX_FMT_VAAPI;
  printf("setting hwframe ctx\n");
  /*
  if (set_hwframe_ctx(encoder,ffhardInst)<0) {
    printf("could not set hardware to FFmpeg\n");
    return 1;
  }*/
  
  hardFrameDataR=av_hwframe_ctx_alloc(ffhardInst);
  ((AVHWFramesContext*)hardFrameDataR->data)->width=dw;
  ((AVHWFramesContext*)hardFrameDataR->data)->height=dh;
  ((AVHWFramesContext*)hardFrameDataR->data)->initial_pool_size = 32;
  ((AVHWFramesContext*)hardFrameDataR->data)->format=AV_PIX_FMT_VAAPI;
  ((AVHWFramesContext*)hardFrameDataR->data)->sw_format=AV_PIX_FMT_NV12;
  printf("%#lx\n",vaInst);
  av_hwframe_ctx_init(hardFrameDataR);
  encoder->hw_frames_ctx=hardFrameDataR;
  
  printf("%#lx\n",vaInst);
  
  av_dict_set(&encOpt,"g","40",0);
  av_dict_set(&encOpt,"max_b_frames","0",0);  
  av_dict_set(&encOpt,"q","25",0);
  
  /*av_dict_set(&encOpt,"i_qfactor","1",0);
  av_dict_set(&encOpt,"i_qoffset","10",0);*/
  
  //av_dict_set(&encOpt,"compression_level","15",0);
  
  if ((avcodec_open2(encoder,encInfo,&encOpt))<0) {
    printf("could not open encoder :(\n");
    return 1;
  }
  
  // create stream
  avformat_alloc_output_context2(&out,NULL,NULL,outname);
  if (out==NULL) {
    printf("couldn't open output...\n");
    return 1;
  }
  stream=avformat_new_stream(out,encInfo);
  stream->id=0;
  stream->time_base=(AVRational){1,900};

  if (out->oformat->flags&AVFMT_GLOBALHEADER)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  avcodec_parameters_from_context(stream->codecpar,encoder);
  //av_dump_format(out,0,outname,1);

if (!(out->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out->pb, outname, AVIO_FLAG_WRITE)<0) {
          printf("could not open file in avio...\n");
          return 1;
}
    }

  int retv;
  char e[4096];
  if ((retv=avformat_write_header(out,&badCodingPractice))<0) {
    av_make_error_string(e,4095,retv);
    printf("could not write header... %s\n",e);
    return 1;
  }

  if (pthread_create(&thr,NULL,unbuff,NULL)<0) {
    printf("could not create encoding thread...\n");
    return 1;
  }
  
  printf("entering main loop. %dx%d\n",dw,dh);
  
  frame=0;
  
  btime=mkts(0,0);
  
  hardFrame=av_frame_alloc();
    av_hwframe_get_buffer(hardFrameDataR,hardFrame,0);
  
  while (1) {
    vblank.request.sequence=1;
    vblank.request.type=DRM_VBLANK_RELATIVE;
    drmWaitVBlank(fd,&vblank);
    
    //printf("\x1b[2J\x1b[1;1H\n");
    
    dtime=vtime;
    vtime=curTime(CLOCK_MONOTONIC)-btime;
    if (btime==mkts(0,0)) {
      btime=vtime;
      vtime=mkts(0,0);
    }
    long int delta=(vtime-dtime).tv_nsec;
    delta=(int)round((double)1000000000/(double)delta);
    if (delta==0) delta=1000000000;
    printf("\x1b[mframe \x1b[1m% 8d\x1b[m: %.2ld:%.2ld:%.2ld.%.3ld. FPS: %s%ld \x1b[m",frame,vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,(delta>=50)?("\x1b[1;32m"):("\x1b[1;33m"),delta);
    tStart=curTime(CLOCK_MONOTONIC);
    plane=drmModeGetPlane(fd,planeid);
    if (plane==NULL) {
      printf("plane crash\n");
      break;
    }
    if (!plane->fb_id) {
      printf("no fb id\n");
      break;
    }
    fb=drmModeGetFB(fd,plane->fb_id);
    if (fb==NULL) {
      printf("no fb\n");
      break;
    }
    if (!fb->handle) {
      printf("no handle\n");
      break;
    }
    
    if (drmPrimeHandleToFD(fd,fb->handle,O_RDONLY,&primefd)<0) {
      printf("couldn't turn handle to FD :(\n");
      break;
    }
    //printf("prime FD: %d\n",primefd);
    
    // RETRIEVE CODE BEGIN //
    portedFD=primefd;
    vaBD.buffers=&portedFD;
    vaBD.pixel_format=VA_FOURCC_BGRX;
    vaBD.width=dw;
    vaBD.height=dh;
    vaBD.data_size=fb->pitch*fb->height;
    vaBD.num_buffers=1;
    vaBD.flags=0;
    vaBD.pitches[0]=fb->pitch;
    vaBD.offsets[0]=0;
    vaBD.num_planes=1;
    
    vaAttr[0].type=VASurfaceAttribMemoryType;
    vaAttr[0].flags=VA_SURFACE_ATTRIB_SETTABLE;
    vaAttr[0].value.type=VAGenericValueTypeInteger;
    vaAttr[0].value.value.i=VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
    vaAttr[1].type=VASurfaceAttribExternalBufferDescriptor;
    vaAttr[1].flags=VA_SURFACE_ATTRIB_SETTABLE;
    vaAttr[1].value.type=VAGenericValueTypePointer;
    vaAttr[1].value.value.p=&vaBD;
    
    if ((vaStat=vaCreateSurfaces(vaInst,VA_RT_FORMAT_RGB32,dw,dh,&surface,1,vaAttr,2))!=VA_STATUS_SUCCESS) {
      printf("could not create surface... %x\n",vaStat);
    } else {
      if ((vaStat=vaSyncSurface(vaInst,surface))!=VA_STATUS_SUCCESS) {
        printf("no surface sync %x\n",vaStat);
      }
      /*
      if ((vaStat=vaCreateImage(vaInst,&allowedFormats[theFormat],dw,dh,&img))!=VA_STATUS_SUCCESS) {
        printf("could not create image... %x\n",vaStat);
      } else {
        //printf("size: %d\n",img.data_size);
        
        if ((vaStat=vaGetImage(vaInst,surface,0,0,dw,dh,img.image_id))!=VA_STATUS_SUCCESS) {
          printf("bullshit %x\n",vaStat);
          exit(1);
        }
        
        if ((vaStat=vaMapBuffer(vaInst,img.buf,(void**)&addr))!=VA_STATUS_SUCCESS) {
          printf("oh come on! %x\n",vaStat);
        } else {
          // read a pixel
          //printf("addr: %.16lx\n",addr);
          for (int i=0; i<128; i++) {
            printf("%.2x",addr[3840*4*900+800*4+i]);
          }
          printf("\n");
          if ((vaStat=vaUnmapBuffer(vaInst,img.buf))!=VA_STATUS_SUCCESS) {
            printf("could not unmap buffer: %x...\n",vaStat);
          }
          addr=NULL;
        }
        if ((vaStat=vaDestroyImage(vaInst,img.image_id))!=VA_STATUS_SUCCESS) {
          printf("destroy image error %x\n",vaStat);
        }
      }
      */
      
      // ENCODE SINGLE-THREAD CODE BEGIN //
    
    hardFrame->pts=((vtime.tv_sec*100000+vtime.tv_nsec/10000)*9)/10;
    //printf("pts: %ld\n",hardFrame->pts);

    // convert
    massAbbrev=((AVVAAPIFramesContext*)(((AVHWFramesContext*)hardFrame->hw_frames_ctx->data)->hwctx));
    
    // HACK sort of!
    //printf("pointer is %lx and there are a %d\n",massAbbrev->surface_ids,massAbbrev->nb_surfaces);
    /*massAbbrev->surface_ids=&thisFluctuates;
    massAbbrev->nb_surfaces=1;*/
    
    scaleRegion.x=0;
    scaleRegion.y=0;
    scaleRegion.width=dw;
    scaleRegion.height=dh;

    scaler.surface=surface;
    scaler.surface_region=0;
    scaler.surface_color_standard=VAProcColorStandardBT709;
    scaler.output_region=0;
    scaler.output_background_color=0xff000000;
    scaler.output_color_standard=VAProcColorStandardBT709;
    scaler.pipeline_flags=0;
    scaler.filter_flags=VA_FILTER_SCALING_HQ;
    
    if ((vaStat=vaBeginPicture(vaInst,scalerC,massAbbrev->surface_ids[31]))!=VA_STATUS_SUCCESS) {
      printf("vaBeginPicture fail: %x\n",vaStat);
      return 1;
    }
    if ((vaStat=vaCreateBuffer(vaInst,scalerC,VAProcPipelineParameterBufferType,sizeof(scaler),1,&scaler,&scalerBuf))!=VA_STATUS_SUCCESS) {
      printf("param buffer creation fail: %x\n",vaStat);
      return 1;
    }
    if ((vaStat=vaRenderPicture(vaInst,scalerC,&scalerBuf,1))!=VA_STATUS_SUCCESS) {
      printf("vaRenderPicture fail: %x\n",vaStat);
      return 1;
    }
    if ((vaStat=vaEndPicture(vaInst,scalerC))!=VA_STATUS_SUCCESS) {
      printf("vaEndPicture fail: %x\n",vaStat);
      return 1;
    }
    if ((vaStat=vaDestroyBuffer(vaInst,scalerBuf))!=VA_STATUS_SUCCESS) {
      printf("vaDestroyBuffer fail: %x\n",vaStat);
      return 1;
    }
    
    encode_write(encoder,hardFrame,out,stream);

    // ENCODE SINGLE-THREAD CODE END //
    //av_frame_free(&hardFrame);
    

      if ((vaStat=vaDestroySurfaces(vaInst,&surface,1))!=VA_STATUS_SUCCESS) {
        printf("destroy surf error %x\n",vaStat);
      }
      /*if ((vaStat=vaDestroySurfaces(vaInst,&thisFluctuates,1))!=VA_STATUS_SUCCESS) {
        printf("destroy encode surf error %x\n",vaStat);
      }*/
    }
    // RETRIEVE CODE END //
    
    close(primefd);
    drmModeFreeFB(fb);
    drmModeFreePlane(plane);
    //frames.push(qFrame(primefd,fb->height,fb->pitch,vtime,fb,plane));
tEnd=curTime(CLOCK_MONOTONIC);
        printf("%s\n",tstos(tEnd-tStart).c_str());

    
    
    frame++;
    if (false) break;
  }
  
  printf("finished.\n");
  return 0;
}
