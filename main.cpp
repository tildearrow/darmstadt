#include "darmstadt.h"

bool quit, newSize;

int fd, primefd, renderfd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmVBlank vblank;

unsigned int planeid;

int frame;
struct timespec btime, vtime, dtime;

struct timespec wtStart, wtEnd;

int dw, dh, ow, oh;

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
int fskip;
bool paused, noSignal;

int discvar1, discvar2;

unsigned char* addr;
unsigned long portedFD;
struct timespec tStart, tEnd;

// VIDEO //
AVStream* stream;
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

// AUDIO //
AVStream* audStream;
AVCodecContext* audEncoder;
AVCodec* audEncInfo;
AVFrame* audFrame;
AVDictionary* audEncOpt;
AVPacket audPacket;

AVFormatContext* out;

#define DARM_WRITEBUF_SIZE (1L<<24)
#define DARM_AVIO_BUFSIZE 131072
char* writeBuf;
int wbReadPos, wbWritePos;
long int totalWritten, bitRate, bitRatePre;
bool cacheEmpty, cacheRun;
struct timespec brTime;

AVDictionary* badCodingPractice=NULL;
void* badCodingPractice1;

FILE* f;

int speeds[120];

const char* outname="out.ts";

SyncMethod syncMethod;

AudioType audioType;
AudioEngine* ae;

struct winsize winSize;

//std::vector<Device> devices;
drmDevice* devices[128];
int deviceCount;
std::vector<Param> params;

string encName;
int autoDevice;

// datasets for graphs
float dsScanOff[1024]; size_t dsScanOffPos;

AVRational tb;

// hesse: capture on AMD/Intel, encode on NVIDIA
bool hesse, absolPerf;

void* cacheThread(void* data) {
  int safeWritePos;
  cacheRun=true;
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
    cacheEmpty=true;
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
  cacheEmpty=true;
  cacheRun=false;
  
  return NULL;
}

int writeToCache(void* data, unsigned char* buf, int size) {
  if (!cacheRun) {
    fwrite(buf,1,size,f);
    return size;
  }
  cacheEmpty=false;
  if ((wbWritePos+size)>(DARM_WRITEBUF_SIZE-1)) {
    // ring buffer
    memcpy(writeBuf+wbWritePos,buf,(DARM_WRITEBUF_SIZE)-wbWritePos);
    memcpy(writeBuf,buf+((DARM_WRITEBUF_SIZE)-wbWritePos),(wbWritePos+size)-(DARM_WRITEBUF_SIZE));
  } else {
    memcpy(writeBuf+wbWritePos,buf,size);
  }
  wbWritePos+=size;
  wbWritePos&=DARM_WRITEBUF_SIZE-1;
  printf("writing to cache\n");
  totalWritten+=size;
  bitRatePre+=size;
  if ((curTime(CLOCK_MONOTONIC)-brTime)>mkts(0,250000000)) {
    brTime=curTime(CLOCK_MONOTONIC);
    bitRate=bitRatePre*4*8;
    bitRatePre=0;
  }
  return size;
}

void waitForCache() {
  //printf("waiting for cache!\n");
  if (!cacheRun) {
    return;
  }
  while (!cacheEmpty) {
    usleep(1000);
  }
}

int64_t seekCache(void* data, int64_t off, int whence) {
  // flush cache, then seek on file.
  waitForCache();
  logD("\x1b[1;35mSEEKING, %ld\x1b[m\n",off);
  fseek(f,off,whence);
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
        logW("couldn't write frame! %s\n",strerror(-ret));
        goto end;
    }
    //midTimeE=curTime(CLOCK_MONOTONIC); printf("midTime: %s\n",tstos(midTimeE-midTimeS).c_str());
    while (1) {
        ret = avcodec_receive_packet(avctx, &enc_pkt);
        if (ret)
            break;
        enc_pkt.stream_index = str->index;
        str->cur_dts = frame->pts-1;
        //enc_pkt.pts = frame->pts;
        av_packet_rescale_ts(&enc_pkt,tb,str->time_base);
        str->cur_dts = enc_pkt.pts-1;
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

const char* getVendor(int id) {
  switch (devImportance(id)) {
    case 1:
      return "Intel";
    case 2:
      return "AMD";
    case -9001:
      return "NVIDIA, hesse-only";
  }
  return "Other";
}

bool pSetH264(string u) {
  if (hesse) {
    encName="h264_nvenc";
  } else {
    encName="h264_vaapi";
  }
  return true;
}

bool pSetHEVC(string u) {
  if (hesse) {
    encName="hevc_nvenc";
  } else {
    encName="hevc_vaapi";
  }
  return true;
}

bool pSetMJPEG(string u) {
  encName="mjpeg_vaapi";
  return true;
}

bool pSetMPEG2(string u) {
  encName="mpeg2_vaapi";
  return true;
}

bool pSetVP8(string u) {
  encName="vp8_vaapi";
  return true;
}

bool pSetVP9(string u) {
  encName="vp9_vaapi";
  return true;
}

bool pListDevices(string u) {
  for (int i=0; i<deviceCount; i++) {
    printf("- device %d (%s) (%.2x:%.2x.%x): %s\n",i,getVendor(i),
           devices[i]->businfo.pci->bus,
           devices[i]->businfo.pci->dev,
           devices[i]->businfo.pci->func,
           devices[i]->nodes[DRM_NODE_PRIMARY]);
  }
  return false;
}

bool pSetDevice(string u) {
  try {
    autoDevice=stoi(u);
  } catch (std::exception& err) {
    logE("invalid device ID. use -listdevices to see available devices.\n");
    return false;
  }
  if (autoDevice>=deviceCount || autoDevice<0) {
    logE("invalid device ID. use -listdevices to see available devices.\n");
    return false;
  }
  if (devImportance(autoDevice)==-9001) {
    logE("seriously? did you think that would work?\n");
    logE("NVIDIA is supported in encode-only mode (-hesse).\n");
    return false;
  }
  return true;
}

bool pSetVendor(string v) {
  int findID;
  findID=0;
  if (v=="Intel") {
    findID=1;
  } else if (v=="AMD") {
    findID=2;
  } else if (v=="NVIDIA") {
    logE("seriously? did you think that would work?\n");
    logE("NVIDIA is supported in encode-only mode (-hesse).\n");
    return false;
  } else if (v=="Other") {
    findID=0;
  } else {
    logE("vendor: Intel, AMD or Other.\n");
    return false;
  }

  for (int i=0; i<deviceCount; i++) {
    if (devImportance(i)==findID) {
      autoDevice=i;
      return true;
    }
  }
  logE("no devices from this vendor found.\n");
  return false;
}

bool pSetBusID(string u) {
  return true;
}

bool pSetDisplay(string u) {
  return true;
}

bool pSetSkip(string u) {
  try {
    fskip=stoi(u);
  } catch (std::exception& err) {
    logE("invalid frameskip value.\n");
    return false;
  }
  if (fskip<1) {
    logE("frameskip must be at least 1.\n");
    return false;
  }
  return true;
}

bool pSet10Bit(string u) {
  return true;
}

bool pSet444(string u) {
  if (!hesse) {
    logE("4:4:4 chroma subsampling only available for NVIDIA Maxwell (and newer) cards on hesse mode.\n");
    return false;
  }
  absolPerf=true;
  return true;
}

bool pSetAudio(string v) {
  if (v=="none") {
    audioType=audioTypeNone;
  } else if (v=="jack") {
    audioType=audioTypeJACK;
  } else if (v=="pulse") {
    audioType=audioTypePulse;
  } else if (v=="alsa") {
    audioType=audioTypeALSA;
  } else {
    logE("audio: none, jack, pulse or alsa.\n");
    return false;
  }
  return true;
}

bool pSetAudioDev(string u) {
  return true;
}

bool pSetAudioCodec(string u) {
  return true;
}

bool pHesse(string u) {
  hesse=true;
  encName="h264_nvenc";
  return true;
}

void initParams() {
  params.push_back(Param("h264",false,pSetH264));
  params.push_back(Param("hevc",false,pSetHEVC));
  params.push_back(Param("mjpeg",false,pSetMJPEG));
  params.push_back(Param("mpeg2",false,pSetMPEG2));
  params.push_back(Param("vp8",false,pSetVP8));
  params.push_back(Param("vp9",false,pSetVP9));
  params.push_back(Param("10bit",false,pSet10Bit));
  params.push_back(Param("absoluteperfection",false,pSet444));
  params.push_back(Param("listdevices",false,pListDevices));
  params.push_back(Param("device",true,pSetDevice));
  params.push_back(Param("vendor",true,pSetVendor));
  params.push_back(Param("busid",true,pSetBusID));
  params.push_back(Param("display",true,pSetDisplay));
  params.push_back(Param("skip",true,pSetSkip));
  params.push_back(Param("audio",true,pSetAudio));
  params.push_back(Param("audiodev",true,pSetAudioDev));
  params.push_back(Param("audiocodec",true,pSetAudioCodec));

  // VA-API -> NVENC codepath
  params.push_back(Param("hesse",false,pHesse));
}

// Intel IDs: 8086
// AMD IDs: 1002 1022
// NVIDIA IDs: 10de 12d2
// 
// devices are selected in the following order:
// AMD > Intel > OtherVendor > NVIDIA (though it won't work)
bool initDevices() {
  deviceCount=drmGetDevices2(0,devices,128);
  return true;
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
  fskip=1;
  cacheEmpty=false;
  cacheRun=false;
  noSignal=false;
  paused=false;
  hesse=false;
  absolPerf=false;
  audioType=audioTypeJACK;
  autoDevice=-1;
  brTime=curTime(CLOCK_MONOTONIC);
  tb=(AVRational){1,100000};
  
  encName="hevc_vaapi";

  initParams();

  // init devices
  initDevices();  

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
  
  // automatic device selection
  if (autoDevice==-1) {
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
  }
  
  if (autoDevice==-1) {
    logE("no viable devices found.\n");
    return 1;
  }

  // DRM INIT BEGIN //
  logI("selected device %s.\n",devices[autoDevice]->nodes[DRM_NODE_PRIMARY]);
  
  // set environment (workaround for X11)
  if (devImportance(autoDevice)==2) {
    setenv("LIBVA_DRIVER_NAME","radeonsi",0);
  }

  // TODO: test with iHD
  if (devImportance(autoDevice)==1) {
    setenv("LIBVA_DRIVER_NAME","i965",0);
  }
  
  fd=open(devices[autoDevice]->nodes[DRM_NODE_PRIMARY],O_RDWR);
  if (fd<0) {
    perror("couldn't open card");
    return 1;
  }

  // Intel devices require special treatment
  if (devImportance(autoDevice)==1) {
    renderfd=open(devices[autoDevice]->nodes[DRM_NODE_RENDER],O_RDWR);
    if (renderfd<0) {
      perror("couldn't open render");
      return 1;
    }
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
  
  ow=dw;
  oh=dh;
  
  if (planeres) drmModeFreePlaneResources(planeres);
  if (plane) drmModeFreePlane(plane);
  if (fb) drmModeFreeFB(fb);
  // DRM INIT END //
  
  // VA-API INIT BEGIN //
  if (devImportance(autoDevice)==1) {
    vaInst=vaGetDisplayDRM(renderfd);
  } else {
    vaInst=vaGetDisplayDRM(fd);
  }
  if (!vaDisplayIsValid(vaInst)) {
    logE("could not open VA-API...\n");
    return 1;
  }
  if (vaInitialize(vaInst,&discvar1,&discvar2)!=VA_STATUS_SUCCESS) {
    logE("could not initialize VA-API...\n");
    return 1;
  }
  
  logD("va-api instance opened. %s.\n",vaQueryVendorString(vaInst));
  
  vaQueryImageFormats(vaInst,allowedFormats,&allowedFormatsSize);
  
  for (int i=0; i<allowedFormatsSize; i++) {
    if (allowedFormats[i].fourcc==VA_FOURCC_BGRX) {
      theFormat=i;
      break;
    }
  }
  
  coAttr[0].type=VASurfaceAttribUsageHint;
  coAttr[0].flags=VA_SURFACE_ATTRIB_SETTABLE;
  coAttr[0].value.type=VAGenericValueTypeInteger;
  coAttr[0].value.value.i=VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ;
  
  if ((vaStat=vaCreateSurfaces(vaInst,VA_RT_FORMAT_YUV420,ow,oh,portFrame,1,coAttr,1))!=VA_STATUS_SUCCESS) {
    logE("could not create surface for encoding... %x\n",vaStat);
    return 1;
  }
  
  if (vaCreateConfig(vaInst,VAProfileNone,VAEntrypointVideoProc,NULL,0,&vaConf)!=VA_STATUS_SUCCESS) {
    logE("no videoproc.\n");
    return 1;
  }
  if (vaCreateContext(vaInst,vaConf,ow,oh,VA_PROGRESSIVE,NULL,0,&scalerC)!=VA_STATUS_SUCCESS) {
    logE("we can't scale...\n");
    return 1;
  }
  wrappedSource=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
  extractSource=(AVHWDeviceContext*)wrappedSource->data;
  
  // only do so if not in hesse mode
  if (!hesse) {
    logD("creating FFmpeg hardware instance\n");
    if (av_hwdevice_ctx_create_derived(&ffhardInst, AV_HWDEVICE_TYPE_VAAPI, wrappedSource, 0)<0) {
      logD("check me later\n");
    }
    ((AVVAAPIDeviceContext*)(extractSource->hwctx))->display=vaInst;
    av_hwdevice_ctx_init(ffhardInst);
  } else {
    logD("creating image in memory\n");
  }
  // VA-API INIT END //
  
  // FFMPEG INIT BEGIN //
  // open file
  avformat_alloc_output_context2(&out,NULL,NULL,outname);
  if (out==NULL) {
    logE("couldn't open output...\n");
    return 1;
  }
  
  /// Video
  logD("creating encoder\n");
  // create stream
  if ((encInfo=avcodec_find_encoder_by_name(encName.c_str()))==NULL) {
    logE("your card does not support %s.\n",encName.c_str());
    return 1;
  }
  
  stream=avformat_new_stream(out,NULL);
  stream->id=out->nb_streams-1;
  // TODO: possibly change this to 2400
  //       (for 50, 60, 100, 120 and 144)
  stream->time_base=(AVRational){1,900};
  
  encoder=avcodec_alloc_context3(encInfo);
  
  encoder->codec_id=encInfo->id;
  encoder->width=ow;
  encoder->height=oh;
  encoder->time_base=tb;
  //encoder->framerate=(AVRational){1000,1};
  encoder->sample_aspect_ratio=(AVRational){1,1};
  if (hesse) {
    encoder->gop_size=90;
    encoder->pix_fmt=AV_PIX_FMT_BGR0;
  } else {
    encoder->gop_size=40;
    encoder->pix_fmt=AV_PIX_FMT_VAAPI;
  }
  encoder->max_b_frames=0;
  
  // only do so if not in hesse mode
  if (!hesse) {
    logD("setting hwframe ctx\n");
    hardFrameDataR=av_hwframe_ctx_alloc(ffhardInst);
    ((AVHWFramesContext*)hardFrameDataR->data)->width=ow;
    ((AVHWFramesContext*)hardFrameDataR->data)->height=oh;
    // this was 32 but let's see if 2 fixes hang
    ((AVHWFramesContext*)hardFrameDataR->data)->initial_pool_size = 2;
    ((AVHWFramesContext*)hardFrameDataR->data)->format=AV_PIX_FMT_VAAPI;
    ((AVHWFramesContext*)hardFrameDataR->data)->sw_format=AV_PIX_FMT_NV12;
    av_hwframe_ctx_init(hardFrameDataR);
    encoder->hw_frames_ctx=hardFrameDataR;
  }
  
  av_dict_set(&encOpt,"qp","26",0);
  
  if (hesse) {
    if (absolPerf) {
      av_dict_set(&encOpt,"profile","high444p",0);
    }
  }
  
  if (out->oformat->flags&AVFMT_GLOBALHEADER)
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  /// Audio
  ae=new JACKAudioEngine;
  if ((audioType==audioTypeNone) || !ae->init("")) {
    logW("couldn't init audio.\n");
    audioType=audioTypeNone;
  } else {
    if ((audEncInfo=avcodec_find_encoder_by_name("pcm_f32le"))==NULL) {
      logE("audio codec not found D:\n");
      return 1;
    }
    
    audStream=avformat_new_stream(out,NULL);
    audStream->id=out->nb_streams-1;
    
    audEncoder=avcodec_alloc_context3(audEncInfo);
    audEncoder->sample_fmt=audEncInfo->sample_fmts?audEncInfo->sample_fmts[0]:AV_SAMPLE_FMT_FLTP;
    audEncoder->sample_rate=ae->sampleRate();
    audEncoder->channels=ae->channels();
    audEncoder->time_base=(AVRational){1,audEncoder->sample_rate};
    audStream->time_base=(AVRational){1,audEncoder->sample_rate};
    switch (audEncoder->channels) {
      case 1:
        audEncoder->channel_layout=AV_CH_LAYOUT_MONO;
        break;
      case 2:
        audEncoder->channel_layout=AV_CH_LAYOUT_STEREO;
        break;
      case 3:
        audEncoder->channel_layout=AV_CH_LAYOUT_2POINT1;
        break;
      case 4:
        audEncoder->channel_layout=AV_CH_LAYOUT_QUAD;
        break;
      case 5:
        audEncoder->channel_layout=AV_CH_LAYOUT_5POINT0;
        break;
      case 6:
        audEncoder->channel_layout=AV_CH_LAYOUT_5POINT1;
        break;
      case 7:
        audEncoder->channel_layout=AV_CH_LAYOUT_7POINT0;
        break;
      case 8:
        audEncoder->channel_layout=AV_CH_LAYOUT_7POINT1;
        break;
    }
    
    if (out->oformat->flags&AVFMT_GLOBALHEADER) {
      printf("global h\n");
      audEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
  }
  
  // Open Video
  if ((avcodec_open2(encoder,encInfo,&encOpt))<0) {
    logE("could not open encoder :(\n");
    return 1;
  }
  avcodec_parameters_from_context(stream->codecpar,encoder);
  
  // Open Audio
  if (audioType!=audioTypeNone) {
    if ((avcodec_open2(audEncoder,audEncInfo,&audEncOpt))<0) {
      logE("could not open encoder :(\n");
      return 1;
    }
    avcodec_parameters_from_context(audStream->codecpar,audEncoder);
    
    audFrame=av_frame_alloc();
    if (audFrame==NULL) {
      logE("couldn't allocate audio frame!\n");
    }
    audFrame->format=audEncoder->sample_fmt;
    audFrame->channel_layout=audEncoder->channel_layout;
    if (audEncInfo->capabilities&AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
      audFrame->nb_samples=1024;
    } else {
      audFrame->nb_samples=audEncoder->frame_size;
    }
    printf("format: %d\n",audFrame->format);
    audFrame->pts=0;
    if (av_frame_get_buffer(audFrame,0)<0) {
      logE("are you being serious?\n");
      return 1;
    }
  }

  
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
  // FFMPEG INIT END //

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
  
  printf("\x1b[1m|\x1b[m\n");
  if (hesse) {
    printf("\x1b[1m|\x1b[34m ~~~~> \x1b[1;35mDARMSTADT (hesse-mode) \x1b[1;36m" DARM_VERSION "\x1b[m\n");
  } else {
    printf("\x1b[1m|\x1b[1;33m ~~~~> \x1b[1;36mDARMSTADT \x1b[1;32m" DARM_VERSION "\x1b[m\n");
  }
  printf("\x1b[1m|\x1b[m\n");
  printf("\x1b[1m- screen %dx%d, output %dx%d, %s\x1b[m\n",dw,dh,ow,oh,absolPerf?("4:4:4"):("4:2:0"));
  
  logI("recording!\n");
  
  frame=0;
  
  struct timespec nextMilestone=mkts(0,0);
  
  btime=mkts(0,0);
  
  hardFrame=av_frame_alloc();
  if (!hardFrame) {
    logE("couldn't allocate frame!\n");
    return 1;
  }
  
  if (!hesse) {
    av_hwframe_get_buffer(hardFrameDataR,hardFrame,0);
  } else {
    // initialize the frame ourselves
    hardFrame->format=AV_PIX_FMT_BGR0;
    hardFrame->width=ow;
    hardFrame->height=oh;
    retv=av_frame_get_buffer(hardFrame,0);
    if (retv<0) {
      logE("couldn't allocate frame data!\n");
      return 1;
    }
  }
  
  drmVBlankReply vreply;
  int startSeq;
  
  vblank.request.sequence=1;
  vblank.request.type=DRM_VBLANK_RELATIVE;
  drmWaitVBlank(fd,&vblank);
  vreply=vblank.reply;
  startSeq=vreply.sequence;
  int recal=0;
  
  ioctl(1,TIOCGWINSZ,&winSize);
  if (audioType!=audioTypeNone) ae->start();
  while (1) {
    vblank.request.sequence=startSeq+fskip;
    vblank.request.type=DRM_VBLANK_ABSOLUTE;
    
    if (drmWaitVBlank(fd,&vblank)<0) {
      noSignal=true;
      usleep(62500);
    } else {
      noSignal=false;
    }
    vreply=vblank.reply;
    startSeq+=fskip;
    if ((vblank.reply.sequence-startSeq)>0) {
      printf("\x1b[2K\x1b[1;33m%d: dropped frame! (%d)\x1b[m\n",frame,++recal);
      startSeq=vreply.sequence;
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
    }
    
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
      "%s"
      // time
      "%.2ld:%.2ld:%.2ld.%.3ld. "
      // FPS
      "FPS:%s%3ld "
      // queue
      "\x1b[mqueue: \x1b[1m%5dK "
      // bitrate
      "\x1b[mrate: %s%6ldKbit "
      // size
      "\x1b[msize: \x1b[1m%ldM"
      // end
      "\x1b[m\x1b[%d;1H\n\n",
      
      // ARGUMENTS
      winSize.ws_row,
      // frame
      (noSignal)?("\x1b[1;31m-> NO SIGNAL <- \x1b[m"):(strFormat("frame \x1b[1m% 8d\x1b[m: ",frame).c_str()),
      // time
      vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,
      // FPS
      (delta>=(50/fskip))?("\x1b[1;32m"):("\x1b[1;33m"),delta,
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
      quit=true;
      break;
    }
    if (!plane->fb_id) {
      printf("\x1b[2K\x1b[1;31m%d: the plane doesn't have a framebuffer!\x1b[m\n",frame);
      quit=true;
      break;
    }
    fb=drmModeGetFB(fd,plane->fb_id);
    if (fb==NULL) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer no longer exists!\x1b[m\n",frame);
      quit=true;
      break;
    }
    if (!fb->handle) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer has invalid handle!\x1b[m\n",frame);
      quit=true;
      break;
    }
    
    if (drmPrimeHandleToFD(fd,fb->handle,O_RDONLY,&primefd)<0) {
      printf("\x1b[2K\x1b[1;31m%d: unable to prepare frame for the encoder!\x1b[m\n",frame);
      quit=true;
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

    if (hesse) {
      // HESSE NVENC CODE BEGIN //
      hardFrame->pts=(vtime.tv_sec*100000+vtime.tv_nsec/10000);
      if ((vaStat=vaCreateImage(vaInst,&allowedFormats[theFormat],dw,dh,&img))!=VA_STATUS_SUCCESS) {
        logE("could not create image... %x\n",vaStat);
        break;
      }
      if ((vaStat=vaGetImage(vaInst,surface,0,0,dw,dh,img.image_id))!=VA_STATUS_SUCCESS) {
        logW("couldn't get image!\n");
      }
      if ((vaStat=vaMapBuffer(vaInst,img.buf,(void**)&addr))!=VA_STATUS_SUCCESS) {
        logW("oh come on! %x\n",vaStat);
      }
      hardFrame->data[0]=addr;
      encode_write(encoder,hardFrame,out,stream);
      if ((vaStat=vaUnmapBuffer(vaInst,img.buf))!=VA_STATUS_SUCCESS) {
        logE("could not unmap buffer: %x...\n",vaStat);
      }
      if ((vaStat=vaDestroyImage(vaInst,img.image_id))!=VA_STATUS_SUCCESS) {
        logE("could not destroy image %x\n",vaStat);
      }
      // HESSE NVENC CODE END //
    } else {
      // VA-API ENCODE SINGLE-THREAD CODE BEGIN //
      hardFrame->pts=(vtime.tv_sec*100000+vtime.tv_nsec/10000);
  
      // convert
      massAbbrev=((AVVAAPIFramesContext*)(((AVHWFramesContext*)hardFrame->hw_frames_ctx->data)->hwctx));
      
      scaleRegion.x=0;
      scaleRegion.y=0;
      scaleRegion.width=ow;
      scaleRegion.height=oh;
  
      scaler.surface=surface;
      scaler.surface_region=0;
      scaler.surface_color_standard=VAProcColorStandardBT709;
      scaler.output_region=0;
      scaler.output_background_color=0xff000000;
      scaler.output_color_standard=VAProcColorStandardBT709;
      scaler.pipeline_flags=0;
      scaler.filter_flags=VA_FILTER_SCALING_HQ;
  
      if ((vaStat=vaBeginPicture(vaInst,scalerC,massAbbrev->surface_ids[1]))!=VA_STATUS_SUCCESS) {
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
      // VA-API ENCODE SINGLE-THREAD CODE END //
    }
    //av_frame_free(&hardFrame);
    

      if ((vaStat=vaDestroySurfaces(vaInst,&surface,1))!=VA_STATUS_SUCCESS) {
        printf("destroy surf error %x\n",vaStat);
      }
    }
    // RETRIEVE CODE END //
    
    close(primefd);
    drmModeFreeFB(fb);
    drmModeFreePlane(plane);
    
    // AUDIO CODE BEGIN //
    if (audioType!=audioTypeNone) {
      AudioPacket* audioPack;
      while ((audioPack=ae->read())!=NULL) {
        logD("read one packet. write out.\n");
        memcpy(audFrame->data[0],audioPack->data,1024/**ae->channels()*sizeof(float)*/);
        logD("after.\n");
        audPacket.data=NULL;
        audPacket.size=0;
        av_init_packet(&audPacket);
        if (avcodec_send_frame(audEncoder,audFrame)<0) {
          logW("couldn't write audio frame!\n");
          break;
        }
        while (1) {
          if (avcodec_receive_packet(audEncoder,&audPacket)) break;
          audPacket.stream_index=audStream->index;
          audStream->cur_dts=audFrame->pts;
          //av_packet_rescale_ts(&enc_pkt,tb,str->time_base);
          audStream->cur_dts=audPacket.pts-1;
          audFrame->pts+=1024;
          if (av_write_frame(out,&audPacket)<0) {
            printf("unable to write frame");
          }
          avformat_flush(out);
          avio_flush(out->pb);
          if ((wtEnd-wtStart)>mkts(0,16666667)) {
            printf("\x1b[1;32mWARNING! audio write took too long :( (%dµs)\n",(wtEnd-wtStart).tv_nsec/1000);
          }
          av_packet_unref(&audPacket);
        }
      }
    }
    // AUDIO CODE END //
    
    // HACK: force flush
    //av_interleaved_write_frame(out,NULL);
    
    //frames.push(qFrame(primefd,fb->height,fb->pitch,vtime,fb,plane));
    tEnd=curTime(CLOCK_MONOTONIC);
    //printf("%s\n",tstos(tEnd-tStart).c_str());

    if (!noSignal) {
      frame++;
    }
    if (quit) break;
  }

  printf("it crashes here\n");
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
