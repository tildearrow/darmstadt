#include "darmstadt.h"

bool quit, newSize;

int fd, primefd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmVBlank vblank;

unsigned int planeid;

int frame;
struct timespec btime, vtime, dtime;

struct timespec wtStart, wtEnd;

int dw, dh;

pthread_t thr;
std::queue<qFrame> frames;

VADisplay vaInst;
VASurfaceAttribExternalBuffers vaBD;
VAContextID scalerC;
VAContextID copierC;
VASurfaceID surface;
VASurfaceID portFrame[16];
VAConfigID vaConf;
VABufferID scalerBuf;
VABufferID copierBuf;
VASurfaceAttrib vaAttr[2];
VASurfaceAttrib coAttr[2];
VAProcPipelineParameterBuffer scaler;
VARectangle scaleRegion;
VAProcPipelineParameterBuffer copier;
VARectangle copyRegion;
VAImage img;
VAImageFormat imgFormat;
VAImageFormat allowedFormats[2048];
int allowedFormatsSize;
int theFormat;
int vaStat;

int discvar1, discvar2;

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

#define DARM_WRITEBUF_SIZE (1L<<24)
#define DARM_AVIO_BUFSIZE 131072
char* writeBuf;
int wbReadPos, wbWritePos;
long int totalWritten, bitRate, bitRatePre;
struct timespec brTime;

AVDictionary* badCodingPractice=NULL;
void* badCodingPractice1;

FILE* f;

int speeds[120];

const char* outname="out.ts";

SyncMethod syncMethod;

struct winsize winSize;

//std::vector<Device> devices;
drmDevice* devices[128];
int deviceCount;
std::vector<Param> params;

string encName;
int autoDevice;

// datasets for graphs
float dsScanOff[1024]; size_t dsScanOffPos;

void* cacheThread(void* data) {
  int safeWritePos;
  while (1) {
    safeWritePos=wbWritePos;
    if (wbReadPos!=safeWritePos) {
      if (safeWritePos<wbReadPos) {
        // ring buffer crossed
        fwrite(writeBuf+wbReadPos,1,DARM_WRITEBUF_SIZE-wbReadPos,f);
        fwrite(writeBuf,1,safeWritePos,f);
      } else {
        fwrite(writeBuf+wbReadPos,1,safeWritePos-wbReadPos,f);
      }
    }
    wbReadPos=safeWritePos;
    usleep(50000);
    if (quit) break;
  }
  // write remaining
  safeWritePos=wbWritePos;
  if (wbReadPos!=safeWritePos) {
    if (safeWritePos<wbReadPos) {
      // ring buffer crossed
      printf("WARNING: writebuf crossed\n");
      fwrite(writeBuf+wbReadPos,1,DARM_WRITEBUF_SIZE-wbReadPos,f);
      fwrite(writeBuf,1,safeWritePos,f);
    } else {
      fwrite(writeBuf+wbReadPos,1,safeWritePos-wbReadPos,f);
    }
  }
  wbReadPos=safeWritePos;
  
  return NULL;
}

int writeToCache(void* data, unsigned char* buf, int size) {
  if ((wbWritePos+size)>(DARM_WRITEBUF_SIZE-1)) {
    // ring buffer
    memcpy(writeBuf+wbWritePos,buf,(DARM_WRITEBUF_SIZE)-wbWritePos);
    memcpy(writeBuf,buf+((DARM_WRITEBUF_SIZE)-wbWritePos),(wbWritePos+size)-(DARM_WRITEBUF_SIZE));
  } else {
    memcpy(writeBuf+wbWritePos,buf,size);
  }
  wbWritePos+=size;
  wbWritePos&=DARM_WRITEBUF_SIZE-1;
  totalWritten+=size;
  bitRatePre+=size;
  if ((curTime(CLOCK_MONOTONIC)-brTime)>mkts(0,250000000)) {
    brTime=curTime(CLOCK_MONOTONIC);
    bitRate=bitRatePre*4*8;
    bitRatePre=0;
  }
  return size;
}

int64_t seekCache(void* data, int64_t off, int whence) {
  // flush cache, then seek on file.
  printf("\x1b[1;35mSEEKING, %ld\x1b[m\n\n",off);
  if (whence == AVSEEK_SIZE) {
    return -1;
  }
  return off;
}

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
        wtStart=curTime(CLOCK_MONOTONIC);
        if (av_write_frame(fout,&enc_pkt)<0) {
          printf("unable to write frame");
}
        avformat_flush(fout);
        avio_flush(fout->pb);
        wtEnd=curTime(CLOCK_MONOTONIC);
        if ((wtEnd-wtStart)>mkts(0,16666667)) {
          printf("\x1b[1;32mWARNING! write took too long :( (%dµs)\n",(wtEnd-wtStart).tv_nsec/1000);
        }
        av_packet_unref(&enc_pkt);
    }
end:
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

bool needsValue(string param) {
  for (int i=0; i<params.size(); i++) {
    if (params[i].name==param) {
      return params[i].value;
    }
  }
  return false;
}

void makeGraph(int x, int y, int w, int h, bool center, float* dataset, size_t dsize, int astart) {
  float rMin, rMax;
  int start=astart;
  if (start<0) start+=dsize;
  
  rMin=-1; rMax=1;
  
  // Calculate Min/Max
  for (int i=start; i!=((start+w)%dsize); i=(i+1)%dsize) {
    if (dataset[i]<rMin) rMin=dataset[i];
    if (dataset[i]>rMax) rMax=dataset[i];
  }
  
  // If NOT Centered, Min Shall be Zero
  if (!center) rMin=0;
  
  int dpos=start;
  for (int i=0; i<w; i++) {
    dpos=(dpos+1)%dsize;
    printf("\x1b[%d;%dH+",1+y+(int)(((dataset[dpos]-rMin)/(rMax-rMin))*h),1+x+i);
  }
}

void handleTerm(int data) {
  quit=true;
}

void handleWinCh(int data) {
  newSize=true;
}

bool pSetH264(string u) {
  encName="h264_vaapi";
  return true;
}

bool pSetHEVC(string u) {
  encName="hevc_vaapi";
  return true;
}

bool pListDevices(string u) {
  return true;
}

bool pSetDevice(string u) {
  return true;
}

bool pSetVendor(string v) {
  if (v=="Intel") {
    
  } else if (v=="AMD") {

  } else if (v=="NVIDIA") {
    logE("seriously? did you think that would work?\n");
    return false;
  } else if (v=="Other") {

  } else {
    logE("vendor: Intel, AMD or Other.\n");
    return false;
  }
  return true;
}

bool pSetBusID(string u) {
  return true;
}

bool pSetDisplay(string u) {
  return true;
}

void initParams() {
  params.push_back(Param("h264",false,pSetH264));
  params.push_back(Param("hevc",false,pSetHEVC));
  params.push_back(Param("listdevices",false,pListDevices));
  params.push_back(Param("device",true,pSetDevice));
  params.push_back(Param("vendor",true,pSetVendor));
  params.push_back(Param("busid",true,pSetBusID));
  params.push_back(Param("display",true,pSetDisplay));
}

// Intel IDs: 8086
// AMD IDs: 1002 1022
// NVIDIA IDs: 10de 12d2
// 
// devices are selected in the following order:
// AMD > Intel > OtherVendor > NVIDIA (though it won't work)
bool initDevices() {
  deviceCount=drmGetDevices2(0,devices,128);
  
  for (int i=0; i<deviceCount; i++) {
    logD("- device %d (%.2x:%.2x.%x): vendor %4x\n",i,
           devices[i]->businfo.pci->bus,
           devices[i]->businfo.pci->dev,
           devices[i]->businfo.pci->func,
           devices[i]->deviceinfo.pci->vendor_id);
    logD("%s\n",devices[i]->nodes[DRM_NODE_PRIMARY]);
  }
  return true;
}

int devImportance(int id) {
  switch (devices[id]->deviceinfo.pci->vendor_id) {
    case 0x1002: case 0x1022: // AMD
      return 2;
    case 0x10de: case 0x12d2: // NVIDIA
      return -9001;
    case 0x8086: // Intel
      return 1;
  }
  return 0; // OtherVendor
}

int main(int argc, char** argv) {
  drmVersionPtr ver;
  struct sigaction saTerm;
  struct sigaction saWinCh;
  saTerm.sa_handler=handleTerm;
  saWinCh.sa_handler=handleWinCh;
  sigemptyset(&saTerm.sa_mask);
  sigemptyset(&saWinCh.sa_mask);
  saWinCh.sa_flags=0;
  quit=false; newSize=false;
  syncMethod=syncVBlank;
  totalWritten=0;
  bitRate=0;
  bitRatePre=0;
  autoDevice=-1;
  brTime=curTime(CLOCK_MONOTONIC);
  
  encName="hevc_vaapi";

  initParams();

  // parse arguments
  string arg, val;
  size_t eqSplit, argStart;
  for (int i=1; i<argc; i++) {
    arg=""; val="";
    if (argv[i][0]=='-') {
      if (argv[i][1]=='-') {
        argStart=2;
      } else {
        argStart=1;
      }
      arg=&argv[i][argStart];
      eqSplit=arg.find_first_of('=');
      if (eqSplit==string::npos) {
        if (needsValue(arg)) {
          if ((i+1)<argc) {
            val=argv[i+1];
            i++;
          } else {
            logE("incomplete param %s.\n",arg.c_str());
            return 1;
          }
        }
      } else {
        val=arg.substr(eqSplit+1);
        arg=arg.substr(0,eqSplit);
      }
      //printf("arg %s. val %s\n",arg.c_str(),val.c_str());
      for (int j=0; j<params.size(); j++) {
        if (params[j].name==arg) {
          if (!params[j].func(val)) return 1;
          break;
        }
      }
    } else {
      outname=argv[i];
    }
  }
  
  // init devices
  initDevices();
  
  // automatic device selection
  for (int i=0; i<deviceCount; i++) {
    if (devImportance(i)==-9001) continue;
    if (autoDevice==-1) {
      autoDevice=i;
      continue;
    }
    if (devImportance(i)>devImportance(autoDevice)) {
      autoDevice=i;
    }
  }
  
  if (autoDevice==-1) {
    logE("no viable devices found.\n");
    return 1;
  }

  // open card
  logI("selected device %s.\n",devices[autoDevice]->nodes[DRM_NODE_PRIMARY]);
  
  // set environment (workaround for AMD card)
  if (devImportance(autoDevice)==2) {
    setenv("LIBVA_DRIVER_NAME","radeonsi",0);
  }
  
  fd=open(devices[autoDevice]->nodes[DRM_NODE_PRIMARY],O_RDWR);
  if (fd<0) {
    perror("couldn't open card");
    return 1;
  }
  
  ver=drmGetVersion(fd);
  if (ver==NULL) {
    logE("could not get version.\n");
    return 1;
  }
  drmFreeVersion(ver);
  logD("video card opened.\n");
  
  if (drmSetClientCap(fd,DRM_CLIENT_CAP_UNIVERSAL_PLANES,1)<0) {
    logE("i have to tell you... that universal planes can't happen...\n");
    return 1;
  }
  
  planeres=drmModeGetPlaneResources(fd);
  if (planeres==NULL) {
    logE("i have to tell you... that I am unable to get plane resources...\n");
    return 1;
  }
  
  if (planeres->count_planes<0) {
    logE("no planes. there are helicopters instead...\n");
    return 1;
  }
  
  logD("number of planes: %d\n",planeres->count_planes);
  for (unsigned int i=0; i<planeres->count_planes; i++) {
    plane=drmModeGetPlane(fd,planeres->planes[i]);
    if (plane==NULL) {
      logD("skipping plane %d...\n",planeres->planes[i]);
      continue;
    }
    
    if (plane->fb_id==0) {
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }
    
    logD("using plane %d.\n",plane->plane_id);
    planeid=plane->plane_id;
    break;
  }
  
  if (plane==NULL) {
    logE("no displays available. can't record.\n");
    return 1;
  }
  
  fb=drmModeGetFB(fd,plane->fb_id);
  if (fb==NULL) {
    logE("could not access display...\n");
    return 1;
  }
  
  if (!fb->handle) {
    logE("error: this program needs special permissions to capture the screen.\n");
    logE("please grant permissions by running this under root:\n");
    logE("setcap cap_sys_admin=ep %s\n",argv[0]);
    return 2;
  }
  
  dw=fb->width;
  dh=fb->height;
  
  if (planeres) drmModeFreePlaneResources(planeres);
  if (plane) drmModeFreePlane(plane);
  if (fb) drmModeFreeFB(fb);
  
  vaInst=vaGetDisplayDRM(fd);
  if (!vaDisplayIsValid(vaInst)) {
    logE("could not open VA-API...\n");
    return 1;
  }
  if (vaInitialize(vaInst,&discvar1,&discvar2)!=VA_STATUS_SUCCESS) {
    logE("could not initialize VA-API...\n");
    return 1;
  }
  
  logD("va-api instance opened (%s).\n",vaQueryVendorString(vaInst));
  
  vaQueryImageFormats(vaInst,allowedFormats,&allowedFormatsSize);
  
  for (int i=0; i<allowedFormatsSize; i++) {
    logD("format %d: - %.8x\n",i,allowedFormats[i].fourcc);
    if (allowedFormats[i].fourcc==VA_FOURCC_BGRX) {
      logD("%d is the format!\n",i);
      theFormat=i;
      break;
    }
  }
  
  coAttr[0].type=VASurfaceAttribUsageHint;
  coAttr[0].flags=VA_SURFACE_ATTRIB_SETTABLE;
  coAttr[0].value.type=VAGenericValueTypeInteger;
  coAttr[0].value.value.i=VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ;
  
  if ((vaStat=vaCreateSurfaces(vaInst,VA_RT_FORMAT_YUV420,dw,dh,portFrame,1,coAttr,1))!=VA_STATUS_SUCCESS) {
    logE("could not create surface for encoding... %x\n",vaStat);
    return 1;
  }
  
  if (vaCreateConfig(vaInst,VAProfileNone,VAEntrypointVideoProc,NULL,0,&vaConf)!=VA_STATUS_SUCCESS) {
    logE("no videoproc.\n");
    return 1;
  }
  if (vaCreateContext(vaInst,vaConf,dw,dh,VA_PROGRESSIVE,NULL,0,&scalerC)!=VA_STATUS_SUCCESS) {
    logE("we can't scale...\n");
    return 1;
  }
  /*
  if (vaCreateContext(vaInst,vaConf,dw,dh,VA_PROGRESSIVE,NULL,0,&copierC)!=VA_STATUS_SUCCESS) {
    printf("we can't copy, i think...\n");
    return 1;
  }
  */
  wrappedSource=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
  extractSource=(AVHWDeviceContext*)wrappedSource->data;
  
  logD("creating FFmpeg hardware instance\n");
  if (av_hwdevice_ctx_create_derived(&ffhardInst, AV_HWDEVICE_TYPE_VAAPI, wrappedSource, 0)<0) {
    logD("check me later\n");
  }
  ((AVVAAPIDeviceContext*)(extractSource->hwctx))->display=vaInst;
  av_hwdevice_ctx_init(ffhardInst);
  
  logD("creating encoder\n");
  if ((encInfo=avcodec_find_encoder_by_name(encName.c_str()))==NULL) {
    logE("your card does not support %s.\n",encName.c_str());
    return 1;
  }
  encoder=avcodec_alloc_context3(encInfo);
  encoder->width=dw;
  encoder->height=dh;
  encoder->time_base=(AVRational){1,900};
  //encoder->framerate=(AVRational){1000,1};
  encoder->sample_aspect_ratio=(AVRational){1,1};
  encoder->pix_fmt=AV_PIX_FMT_VAAPI;
  logD("setting hwframe ctx\n");
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
  av_hwframe_ctx_init(hardFrameDataR);
  encoder->hw_frames_ctx=hardFrameDataR;
  
  av_dict_set(&encOpt,"g","40",0);
  av_dict_set(&encOpt,"max_b_frames","0",0);
  av_dict_set(&encOpt,"qp","25",0);
  
  /*av_dict_set(&encOpt,"i_qfactor","1",0);
  av_dict_set(&encOpt,"i_qoffset","10",0);*/
  
  //av_dict_set(&encOpt,"compression_level","15",0);
  
  if ((avcodec_open2(encoder,encInfo,&encOpt))<0) {
    logE("could not open encoder :(\n");
    return 1;
  }
  
  // create stream
  avformat_alloc_output_context2(&out,NULL,NULL,outname);
  if (out==NULL) {
    logE("couldn't open output...\n");
    return 1;
  }
  stream=avformat_new_stream(out,encInfo);
  stream->id=0;
  stream->time_base=(AVRational){1,900};

  if (out->oformat->flags&AVFMT_GLOBALHEADER)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  avcodec_parameters_from_context(stream->codecpar,encoder);
  //av_dump_format(out,0,outname,1);
  
  if ((f=fopen(outname,"wb"))==NULL) {
    perror("could not open output file");
    return 1;
  }

  writeBuf=new char[DARM_WRITEBUF_SIZE];
  if (!(out->oformat->flags & AVFMT_NOFILE)) {
    unsigned char* ioBuffer=(unsigned char*)av_malloc(DARM_AVIO_BUFSIZE+AV_INPUT_BUFFER_PADDING_SIZE);
    AVIOContext* avioContext=avio_alloc_context(ioBuffer,DARM_AVIO_BUFSIZE,1,(void*)(f),NULL, &writeToCache,&seekCache);
    out->pb=avioContext;
  }

  int retv;
  char e[4096];
  if ((retv=avformat_write_header(out,&badCodingPractice))<0) {
    av_make_error_string(e,4095,retv);
    logE("could not write header... %s\n",e);
    return 1;
  }

  sigaction(SIGINT,&saTerm,NULL);
  sigaction(SIGTERM,&saTerm,NULL);
  sigaction(SIGHUP,&saTerm,NULL);
  
  sigaction(SIGWINCH,&saWinCh,NULL);

  memset(speeds,0,sizeof(int)*120);
  
  wbReadPos=0;
  wbWritePos=0;

  if (pthread_create(&thr,NULL,cacheThread,NULL)<0) {
    logE("could not create encoding thread...\n");
    return 1;
  }
  
  logI("recording!\n");
  
  frame=0;
  
  struct timespec nextMilestone=mkts(0,0);
  
  btime=mkts(0,0);
  
  hardFrame=av_frame_alloc();
  av_hwframe_get_buffer(hardFrameDataR,hardFrame,0);
  
  drmVBlankReply vreply;
  int startSeq;
  
  vblank.request.sequence=1;
  vblank.request.type=DRM_VBLANK_RELATIVE;
  drmWaitVBlank(fd,&vblank);
  vreply=vblank.reply;
  startSeq=vreply.sequence;
  int recal=0;
  
  printf("\x1b[?1049h\x1b[2J\n");
  
  ioctl(1,TIOCGWINSZ,&winSize);
  while (1) {
    vblank.request.sequence=startSeq+1;
    vblank.request.type=DRM_VBLANK_ABSOLUTE;
    
    drmWaitVBlank(fd,&vblank);
    vreply=vblank.reply;
    startSeq++;
    if ((vblank.reply.sequence-startSeq)>0) {
      printf("\x1b[2K\x1b[1;33m%d: dropped frame! (%d)\x1b[m\n",frame,recal+1);
      startSeq=vreply.sequence;
      recal++;
    }
    
    //printf("PRE: % 5ldµs. \n",(curTime(CLOCK_MONOTONIC)-mkts(vreply.tval_sec,vreply.tval_usec*1000)).tv_nsec/1000);
    
    while ((curTime(CLOCK_MONOTONIC))<mkts(vreply.tval_sec,1000000+vreply.tval_usec*1000)) {
      usleep(2000);
    }

    //printf("POST: % 5ldµs. \n",(curTime(CLOCK_MONOTONIC)-mkts(vreply.tval_sec,vreply.tval_usec*1000)).tv_nsec/1000);
   
    //printf("\x1b[2J\x1b[1;1H\n");
    
    dtime=vtime;
    vtime=mkts(vreply.tval_sec,vreply.tval_usec*1000)-btime;
    if (btime==mkts(0,0)) {
      btime=vtime;
      vtime=mkts(0,0);
    }
    // --HACK OF SORTS--
    // because VBlank isn't working out well for me,
    // and I seem to be unable to copy a surface to
    // another, I devised this method as a workaround.
    // please note this is temporary, and the whole
    // cache/copy thing still remains in the plan.
    // I just want Jane to come and make me happy...
    //
    // OK, SO, PLEASE, REITERATE THE PREVIOUS COMMENT
    // AS MUCH AS YOU CAN!!!
    //
    // OK, so, please, ignore the above comment.
    // the brightness is somewhere else.
    //
    // WAIT! why is this method still here? the
    // superior "absolute" approach is in place!
    if (newSize) {
      newSize=false;
      ioctl(1,TIOCGWINSZ,&winSize);
      printf("\x1b[2J\n");
    }
    
    int darmStringSize=strlen("~> DARMSTADT " DARM_VERSION "<~");
    printf("\x1b[1;%dH\x1b[2K\x1b[1;33m~> \x1b[1;36mDARMSTADT \x1b[1;32m" DARM_VERSION "\x1b[1;33m <~\x1b[m\n",(winSize.ws_col-darmStringSize)/2);
    if (syncMethod==syncTimer) {
      if (vtime<nextMilestone) continue;
      nextMilestone=nextMilestone+mkts(0,16666667);
      while ((vtime-mkts(0,16666667))>nextMilestone) nextMilestone=nextMilestone+mkts(0,16666667);
    }
    long int delta=(vtime-dtime).tv_nsec;
    if (delta!=0) delta=(int)round((double)1000000000/(double)delta);
    //if (delta==0) delta=1000000000;
    
    // >>> STATUS LINE <<<
    printf(
      // begin
      "\x1b[%d;1H\x1b[2K\x1b[m"
      // frame
      "frame \x1b[1m% 8d\x1b[m: "
      // time
      "%.2ld:%.2ld:%.2ld.%.3ld. "
      // FPS
      "FPS:%s%3ld "
      // queue
      "\x1b[mqueue: \x1b[1m%5dK "
      // bitrate
      "\x1b[mrate: %s%6dKbit "
      // size
      "\x1b[msize: \x1b[1m%dM"
      // end
      "\x1b[m\x1b[%d;1H\n",
      
      // ARGUMENTS
      winSize.ws_row,
      // frame
      frame,
      // time
      vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,
      // FPS
      (delta>=50)?("\x1b[1;32m"):("\x1b[1;33m"),delta,
      // queue
      (wbWritePos-wbReadPos)>>10,
      // bitrate
      (bitRate>=30000000)?("\x1b[1;33m"):("\x1b[1;32m"),bitRate>>10,
      // size
      totalWritten>>20,
      // end
      winSize.ws_row-1);
    
    if (frame>1 && delta>=0 && delta<120) {
      speeds[delta]++;
    }
    tStart=curTime(CLOCK_MONOTONIC);
    plane=drmModeGetPlane(fd,planeid);
    if (plane==NULL) {
      printf("\x1b[2K\x1b[1;31m%d: the plane no longer exists!\x1b[m\n",frame);
      break;
    }
    if (!plane->fb_id) {
      printf("\x1b[2K\x1b[1;31m%d: the plane doesn't have a framebuffer!\x1b[m\n",frame);
      break;
    }
    fb=drmModeGetFB(fd,plane->fb_id);
    if (fb==NULL) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer no longer exists!\x1b[m\n",frame);
      break;
    }
    if (!fb->handle) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer has invalid handle!\x1b[m\n",frame);
      break;
    }
    
    if (drmPrimeHandleToFD(fd,fb->handle,O_RDONLY,&primefd)<0) {
      printf("\x1b[2K\x1b[1;31m%d: unable to prepare frame for the encoder!\x1b[m\n",frame);
      break;
    }
    //printf("prime FD: %d\n",primefd);
    
    dsScanOff[dsScanOffPos++]=(wtEnd-wtStart).tv_nsec;
    dsScanOffPos&=1023;
    //makeGraph(0,2,40,12,true,dsScanOff,1024,dsScanOffPos-40);
    
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
      
      // ENCODE SINGLE-THREAD CODE BEGIN //
    
    hardFrame->pts=((vtime.tv_sec*100000+vtime.tv_nsec/10000)*9)/10;
    //printf("pts: %ld\n",hardFrame->pts);

    // convert
    massAbbrev=((AVVAAPIFramesContext*)(((AVHWFramesContext*)hardFrame->hw_frames_ctx->data)->hwctx));
    
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
    }
    // RETRIEVE CODE END //
    
    close(primefd);
    drmModeFreeFB(fb);
    drmModeFreePlane(plane);
    //frames.push(qFrame(primefd,fb->height,fb->pitch,vtime,fb,plane));
tEnd=curTime(CLOCK_MONOTONIC);
        //printf("%s\n",tstos(tEnd-tStart).c_str());

    
    
    frame++;
    if (quit) break;
  }
  
  printf("\x1b[?1049l\n");

  if (av_write_trailer(out)<0) {
    logW("could not finish file...\n");
  }
  
  logD("finishing output...\n");
  pthread_join(thr,&badCodingPractice1);

  av_free(out->pb->buffer);
  fclose(f);
  avformat_free_context(out);

  avcodec_free_context(&encoder);
  vaDestroyContext(vaInst,scalerC);
  vaDestroyContext(vaInst,copierC);
  if ((vaStat=vaDestroySurfaces(vaInst,portFrame,1))!=VA_STATUS_SUCCESS) {
    logE("destroy portframes error %x\n",vaStat);
  }
  vaTerminate(vaInst);

  close(fd);
  
  logI("finished.\n");

  printf("--FRAME REPORT--\n");
  if (frame<=1) {
    logW("too short! can't estimate.\n");
  } else {
    for (int i=0; i<120; i++) {
      if (speeds[i]>0) {
        printf("%d FPS: %d (%.2f%)\n",i,speeds[i],100.0*((double)speeds[i]/(double)(frame-2)));
      }
    }
  }
  printf("dropped frames: %d\n",recal);
  logI("finished recording %.2ld:%.2ld:%.2ld.%.3ld (%d).\n",vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,frame);
  return 0;
}
