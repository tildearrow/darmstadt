#include "darmstadt.h"

int fd, primefd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmVBlank vblank;

unsigned int planeid;

int frame;
struct timespec btime, vtime;

int dw, dh;

pthread_t thr;
std::queue<qFrame> frames;

VADisplay vaInst;
VASurfaceAttribExternalBuffers vaBD;
VASurfaceID surface;
VASurfaceAttrib vaAttr[2];
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

int main(int argc, char** argv) {
  drmVersionPtr ver;
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
  if ((encInfo=avcodec_find_encoder_by_name("h264_vaapi"))==NULL) {
    printf("could not find encoder...\n");
    return 1;
  }
  encoder=avcodec_alloc_context3(encInfo);
  encoder->width=dw;
  encoder->height=dh;
  encoder->time_base=(AVRational){1,1000};
  encoder->framerate=(AVRational){60,1};
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
  //((AVHWFramesContext*)hardFrameDataR->data)->initial_pool_size = 200;
  ((AVHWFramesContext*)hardFrameDataR->data)->format=AV_PIX_FMT_VAAPI;
  ((AVHWFramesContext*)hardFrameDataR->data)->sw_format=AV_PIX_FMT_NV12;
  printf("%#lx\n",vaInst);
  av_hwframe_ctx_init(hardFrameDataR);
  encoder->hw_frames_ctx=hardFrameDataR;
  
  printf("%#lx\n",vaInst);
  
  av_dict_set(&encOpt,"qp","25",0);
  av_dict_set(&encOpt,"compression_level","15",0);
  
  if ((avcodec_open2(encoder,encInfo,&encOpt))<0) {
    printf("could not open encoder :(\n");
    return 1;
  }
  
  // create stream
  avformat_alloc_output_context2(&out,NULL,NULL,"out.mkv");
  if (out==NULL) {
    printf("couldn't open output...\n");
    return 1;
  }
  stream=avformat_new_stream(out,NULL);
  stream->id=0;
  stream->time_base=(AVRational){1,1000};

  if (out->oformat->flags&AVFMT_GLOBALHEADER)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  avcodec_parameters_from_context(stream->codecpar,encoder);

if (!(out->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out->pb, "out.mkv", AVIO_FLAG_WRITE)<0) {
          printf("could not open file in avio...\n");
          return 1;
}
    }

  int retv;
  char e[4096];
  if (retv=avformat_write_header(out,&badCodingPractice)<0) {
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
  
  while (1) {
    vblank.request.sequence=1;
    vblank.request.type=DRM_VBLANK_RELATIVE;
    drmWaitVBlank(fd,&vblank);
    
    //printf("\x1b[2J\x1b[1;1H\n");
    
    vtime=curTime(CLOCK_MONOTONIC)-btime;
    if (btime==mkts(0,0)) {
      btime=vtime;
      vtime=mkts(0,0);
    }
    printf("frame % 8d: %.2ld:%.2ld:%.2ld.%.3ld\n",frame,vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000);
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
    hardFrame=av_frame_alloc();
    av_hwframe_get_buffer(hardFrameDataR,hardFrame,0);
    hardFrame->pts=vtime.tv_sec*1000+vtime.tv_nsec/1000000;
    printf("pts: %ld\n",hardFrame->pts);

    massAbbrev=((AVVAAPIFramesContext*)(((AVHWFramesContext*)hardFrame->hw_frames_ctx->data)->hwctx));
    
    // HACK!
    encode_write(encoder,hardFrame,out);

    // ENCODE SINGLE-THREAD CODE END //
    av_frame_free(&hardFrame);

      if ((vaStat=vaDestroySurfaces(vaInst,&surface,1))!=VA_STATUS_SUCCESS) {
        printf("destroy surf error %x\n",vaStat);
      }
    }
    // RETRIEVE CODE END //
    
    close(primefd);
    drmModeFreeFB(fb);
    drmModeFreePlane(plane);
    //frames.push(qFrame(primefd,fb->height,fb->pitch,vtime,fb,plane));
tEnd=curTime(CLOCK_MONOTONIC);
        printf("get time: %s\n",tstos(tEnd-tStart).c_str());

    
    
    frame++;
    if (false) break;
  }
  
  printf("finished.\n");
  return 0;
}
