#include "darmstadt.h"
#include "eglcommon.h"
#include <GLES2/gl2.h>
#include <va/va.h>
#include <xf86drmMode.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <atomic>

PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;

bool quit, newSize;

// HARDWARE //
int fd, primefd, renderfd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmModeCrtc* disp;
drmVBlank vblank;
unsigned int oldFBID=0;
int repeatCount=0;

gbm_device* gbmDevice;

// STATS //
int frame;
int framerate;
struct timespec btime, vtime, dtime, lastATime, sATime;

int videoStalls=0;

struct timespec wtStart, wtEnd;

// CONFIG //
int dw, dh, ow, oh;
int cx, cy, cw, ch;
bool doCrop=false;
bool captureCursor=true;
ScaleMethod sm;

// VA-API //
VADisplay vaInst;
VASurfaceAttribExternalBuffers vaBD;
VAContextID scalerC;
VASurfaceID surface;
VAConfigID vaConfS;
VABufferID scalerBuf;
VASurfaceAttrib vaAttr[2];
VAProcPipelineParameterBuffer scaler;
VARectangle scaleRegion, cropRegion;
VAImage img;
VAImageFormat imgFormat;
VAImageFormat allowedFormats[2048];
int allowedFormatsSize;
int theFormat;
int vaStat;
int fskip;
bool paused, noSignal;
bool huff=false;
int qp, br, gopSize, encSpeed;

int discvar1, discvar2;

unsigned char* addr;
unsigned long portedFD;
struct timespec tStart, tEnd;

// COMPOSITOR - VA //
VASurfaceAttribExternalBuffers vaCompoBD;
VAContextID copierC;
VABufferID copierBuf;
VASurfaceID portFrame;
VABlendState compoBS;
VASurfaceAttrib coAttr[3];
VAProcPipelineParameterBuffer copier;
VARectangle copyRegion;

// COMPOSITOR - EGL //
EGLDisplay eglInst;
EGLConfig eglConf;
EGLContext eglContext;
EGLImageKHR eglOut;
GLuint compoTex;
GLuint compoFB;
gbm_bo* compoBo;
const char* eglExtsBase=NULL;
const char* eglExtsInst=NULL;
const char* glExts=NULL;
GLuint renderVertexS, renderFragmentS, renderProgram;
GLfloat planeTri[16][12];
GLfloat planeUV[16][8];
GLuint planeTriBO;
GLint renderTexture;

// X11 CURSOR INFORMATION (workaround until I find a way to get cursor position using DRM) //
pthread_t x11Thread;
Display* x11Inst;
Window x11Screen;
// (X, Y) 16.16
std::atomic<unsigned int> cursorPos;

// VIDEO //
AVStream* stream;
AVCodecContext* encoder;
const AVCodec* encInfo;
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
const AVCodec* audEncInfo;
AVFrame* audFrame;
AVDictionary* audEncOpt;
AVPacket* audPacket;
string audName;

// OUTPUT //
AVFormatContext* out;

#define DARM_AVIO_BUFSIZE 131072
char* writeBuf;
int wbReadPos, wbWritePos;
long int totalWritten, bitRate, bitRatePre;
bool cacheEmpty, cacheRun;
WriteCache cache;
struct timespec brTime;

int f;

int speeds[120];

const char* outname="out.nut";

SyncMethod syncMethod;

// AUDIO SETTINGS //
AudioType audioType;
AudioEngine* ae;
float audGain, audLimAmt;
bool audLimiter;

struct winsize winSize;

drmDevice* devices[128];
int deviceCount;
std::vector<Param> params;
std::vector<Category> categories;

string encName, audEncName;
int autoDevice;

// datasets for graphs
float dsScanOff[1024]; size_t dsScanOffPos;

AVRational tb;
AVRational audiotb;

// hesse: capture on AMD/Intel, encode on NVIDIA
// meitner: capture on AMD/Intel, encode using x264/x265
bool hesse, meitner, absolPerf, tenBit;

// constants
const char* renderVertex=
  "attribute vec4 in_position;\n"
  "attribute vec2 in_TexCoord;\n"
  "\n"
  "varying vec2 vTexCoord;\n"
  "\n"
  "void main() {\n"
  "  gl_Position=in_position;\n"
  "  vTexCoord=in_TexCoord;\n"
  "}\n";

const char* renderFragment=
  "#extension GL_OES_EGL_image_external : enable\n"
  "precision mediump float;\n"
  "uniform samplerExternalOES uTexture;\n"
  "varying vec2 vTexCoord;\n"
  "\n"
  "void main() {\n"
  "  gl_FragColor=texture2D(uTexture,vTexCoord);\n"
  "}\n";

/*
const char* renderFragment=
  "precision mediump float;\n"
  "uniform sampler2D uTexture;\n"
  "varying vec2 vTexCoord;\n"
  "\n"
  "void main() {\n"
  "  gl_FragColor=vec4(vTexCoord.x,vTexCoord.y,0.0,1.0);\n"
  "}\n";
  */

// stuff
int writeToCache(void*, unsigned char* buf, int size) {
  bitRatePre+=size;
  totalWritten+=size;
  if ((curTime(CLOCK_MONOTONIC)-brTime)>mkts(2,0)) {
    brTime=curTime(CLOCK_MONOTONIC);
    bitRate=bitRatePre*8/2;
    bitRatePre=0;
  }
  return cache.write(buf,size);
}

int64_t seekCache(void*, int64_t off, int whence) {
  if (whence==AVSEEK_SIZE) {
    cache.seek(0,SEEK_END);
    return -1;
  }
  cache.seek(off,whence);
  return 0;
}

static int writeFrame(AVCodecContext *avctx, AVFrame *frame, AVFormatContext *fout, AVStream* str)
{
    int ret = 0;
    AVPacket* enc_pkt=av_packet_alloc();
    if (enc_pkt==NULL) {
      logW("couldn't allocate packet!\n");
      return -1;
    }
    if ((ret = avcodec_send_frame(avctx, frame)) < 0) {
        logW("couldn't write frame! %s\n",strerror(-ret));
        goto end;
    }
    while (1) {
        ret = avcodec_receive_packet(avctx, enc_pkt);
        if (ret)
            break;
        enc_pkt->stream_index = str->index;
        //enc_pkt->duration=0;
        //enc_pkt->dts = frame->pts;
        av_packet_rescale_ts(enc_pkt,tb,str->time_base);
        // what was this again?
        //str->cur_dts = enc_pkt->pts-1;
        wtStart=curTime(CLOCK_MONOTONIC);
        if (av_write_frame(fout,enc_pkt)<0) {
          logE("unable to write frame!!!!\n");
        }
        avio_flush(fout->pb);
        wtEnd=curTime(CLOCK_MONOTONIC);
        if ((wtEnd-wtStart)>mkts(0,16666667)) {
          printf("\x1b[1;32mWARNING! write took too long :( (%ldµs)\n",(wtEnd-wtStart).tv_nsec/1000);
          videoStalls++;
        }
    }
end:
    av_packet_free(&enc_pkt);
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

void* x11CursorThread(void*) {
  while (true) {
    if (quit) break;
    x11Inst=XOpenDisplay(NULL);
    if (x11Inst==NULL) {
      logD("no X11 display found! retrying in 10 seconds...\n");
      sleep(10);
      continue;
    }
    x11Screen=XRootWindow(x11Inst,0);

    while (true) {
      if (quit) break;
      XFixesCursorImage* cursor=XFixesGetCursorImage(x11Inst);

      short x=cursor->x-cursor->xhot;
      short y=cursor->y-cursor->yhot;
      cursorPos=((unsigned short)x<<16)|((unsigned short)y);

      XFree(cursor);
      usleep(10000);
    }
    XCloseDisplay(x11Inst);
  }

  return NULL;
}

void* initGLProc(const char* extList, const char* extName, const char* procName) {
  size_t len=strlen(extName);
  const char* name=extList;

  if (name==NULL) {
    logW("extension list is null\n");
    return NULL;
  }
  if (name[0]==0) {
    logW("extension list is empty\n");
    return NULL;
  }

  // find extension
  while (true) {
    name=strstr(name,extName);
    if (name==NULL) {
      logW("extension %s not found.\n",extName);
      return NULL;
    }
    if (name[len]==0 || name[len]==' ') break;
    name+=len;
  }

  // get proc address
  return (void*)eglGetProcAddress(procName);
}

#define INIT_GL_PROC(_e,_n,_p) { \
  void* temp=initGLProc(_e,_n,#_p); \
  memcpy(&_p,&temp,sizeof(void*)); \
}

#define REQUIRE_GL_PROC(_e,_n,_p) \
  INIT_GL_PROC(_e,_n,_p); \
  if (_p==NULL)

#define REQUIRE_GL_PROC_DEFAULT(_e,_n,_p) \
  REQUIRE_GL_PROC(_e,_n,_p) { \
    logE("procedure " #_p " of extension " _n " not found.\n"); \
    return false; \
  }

bool initEGL() {
  // init output buffer
  unsigned int boFormat=GBM_FORMAT_ARGB8888;

  logD("creating GBM buffer...\n");
  compoBo=gbm_bo_create(gbmDevice,ow,oh,boFormat,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
  if (compoBo==NULL) {
    logE("couldn't make GBM buffer.\n");
    return false;
  }

  // check availability of GBM platform
  logD("query base extensions...\n");
  eglExtsBase=eglQueryString(EGL_NO_DISPLAY,EGL_EXTENSIONS);
#ifdef EGL_MESA_platform_gbm
  if (eglExtsBase==NULL) {
    logE("no extensions available!\n");
    return false;
  }

  logD("init GL proc\n");
  REQUIRE_GL_PROC(eglExtsBase,"EGL_EXT_platform_base",eglGetPlatformDisplayEXT) {
    logE("your setup does not support EGL_MESA_platform_gbm.\n");
    return false;
  }
#endif

  // open device
  logD("open EGL device...\n");
#ifdef EGL_MESA_platform_gbm
  eglInst=eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA,gbmDevice,NULL);
#else
  eglInst=eglGetDisplay(gbmDevice);
#endif

  if (eglInst==EGL_NO_DISPLAY) {
    logE("no display...\n");
    return false;
  }

  logD("initialize EGL...\n");
  EGLint eglVerMajor, eglVerMinor;
  if (!eglInitialize(eglInst,&eglVerMajor,&eglVerMinor)) {
    logE("EGL init failed...\n");
    return false;
  }
  logD("EGL version %d.%d.\n",eglVerMajor,eglVerMinor);

  eglExtsInst=eglQueryString(eglInst,EGL_EXTENSIONS);

  // prepare EGL extensions
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_image_base",eglCreateImageKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_image_base",eglDestroyImageKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_fence_sync",eglCreateSyncKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_fence_sync",eglDestroySyncKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_fence_sync",eglWaitSyncKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_KHR_fence_sync",eglClientWaitSyncKHR);
  REQUIRE_GL_PROC_DEFAULT(eglExtsInst,"EGL_ANDROID_native_fence_sync",eglDupNativeFenceFDANDROID);

  // prepare OpenGL
  logD("bind API...\n");
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    logE("could not bind OpenGL ES!\n");
    return false;
  }

  static const EGLint contextAttr[]={
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  const EGLint configAttr[]={
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 1,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SAMPLES, 1,
    EGL_NONE
  };

  EGLint count=0;
  EGLint matched=0;
  EGLConfig *configs;
  int configIndex=-1;

  logD("choose config...\n");
  if (!eglGetConfigs(eglInst,NULL,0,&count)) {
    logE("no EGL configs available!\n");
    return false;
  }
  if (count<1) {
    logE("no EGL configs available!\n");
    return false;
  }
  configs=new EGLConfig[count];

  if (!eglChooseConfig(eglInst,configAttr,configs,count,&matched)) {
    logE("no EGL configs with needed attributes!\n");
    delete[] configs;
    return false;
  }
  if (!matched) {
    logE("no EGL configs with needed attributes!\n");
    delete[] configs;
    return false;
  }

  if (!boFormat) {
    configIndex=0;
    logW("choosing the first config.\n");
  }

  if (configIndex==-1) {
    for (int i=0; i<count; i++) {
      EGLint id;
      
      if (!eglGetConfigAttrib(eglInst,configs[i],EGL_NATIVE_VISUAL_ID,&id))
        continue;
      
      if (id==(int)boFormat) {
        configIndex=i;
        break;
      }
    }
  }

  if (configIndex!=-1) {
    eglConf=configs[configIndex];
    delete[] configs;
  } else {
    logE("no EGL config chosen!\n");
    delete[] configs;
    return false;
  }

  logD("create context...\n");
  eglContext=eglCreateContext(eglInst,eglConf,EGL_NO_CONTEXT,contextAttr);
  if (eglContext==EGL_NO_CONTEXT) {
    logE("couldn't create OpenGL context!\n");
    return false;
  }

  // make current
  logD("make current...\n");
  if (!eglMakeCurrent(eglInst,EGL_NO_SURFACE,EGL_NO_SURFACE,eglContext)) {
    logE("couldn't make current!\n");
    return false;
  }

  glExts=(const char*)glGetString(GL_EXTENSIONS);

  logD("OpenGL ES info:\n");
  logD("- version %s (GLSL %s)\n",glGetString(GL_VERSION),glGetString(GL_SHADING_LANGUAGE_VERSION));
  logD("- vendor: %s\n",glGetString(GL_VENDOR));
  logD("- renderer: %s\n",glGetString(GL_RENDERER));

  // get GL extensions
  REQUIRE_GL_PROC_DEFAULT(glExts,"GL_OES_EGL_image",glEGLImageTargetTexture2DOES);

  // create output buffer
  logD("create output buffer...\n");
  int boFD=gbm_bo_get_fd(compoBo);
  if (boFD<0) {
    logE("couldn't get FD of compositor BO.\n");
    return false;
  }

  EGLint imageAttr[]={
    EGL_WIDTH, (int)gbm_bo_get_width(compoBo),
    EGL_HEIGHT, (int)gbm_bo_get_height(compoBo),
    EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_bo_get_format(compoBo),
    EGL_DMA_BUF_PLANE0_FD_EXT, boFD,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)gbm_bo_get_stride(compoBo),
    EGL_NONE,
  };

  eglOut=eglCreateImageKHR(eglInst,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,imageAttr);
  if (eglOut==EGL_NO_IMAGE_KHR) {
    logE("couldn't create image.\n");
    return false;
  }
  // I wonder why. we WILL need this later.
  close(boFD);

  logD("create output texture...\n");
  glGenTextures(1,&compoTex);
  glBindTexture(GL_TEXTURE_2D,compoTex);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D,eglOut);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D,0);

  glGenFramebuffers(1,&compoFB);
  glBindFramebuffer(GL_FRAMEBUFFER,compoFB);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,compoTex,0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
    logE("framebuffer isn't complete.\n");
    glDeleteFramebuffers(1,&compoFB);
    glDeleteTextures(1,&compoTex);
    return false;
  }

  // initialize OpenGL (TODO: error checks)
  int shStatus;
  logD("initialize shader...\n");
  renderVertexS=glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(renderVertexS,1,&renderVertex,NULL);
  glCompileShader(renderVertexS);
  glGetShaderiv(renderVertexS,GL_COMPILE_STATUS,&shStatus);
  if (!shStatus) {
    logE("failed to compile vertex shader!\n");
    return false;
  }

  renderFragmentS=glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(renderFragmentS,1,&renderFragment,NULL);
  glCompileShader(renderFragmentS);
  glGetShaderiv(renderFragmentS,GL_COMPILE_STATUS,&shStatus);
  if (!shStatus) {
    logE("failed to compile fragment shader!\n");
    return false;
  }

  renderProgram=glCreateProgram();
  glAttachShader(renderProgram,renderVertexS);
  glAttachShader(renderProgram,renderFragmentS);
  glBindAttribLocation(renderProgram,1,"in_TexCoord");
  glLinkProgram(renderProgram);
  glGetProgramiv(renderProgram,GL_LINK_STATUS,&shStatus);
  if (!shStatus) {
    logE("failed to link shader!\n");
    return false;
  }

  logD("initialize the rest...\n");
  glViewport(0,0,ow,oh);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  renderTexture=glGetUniformLocation(renderProgram,"uTexture");

  glGenBuffers(1,&planeTriBO);

  logD("complete!\n");
  return true;
}

struct DarmPlane {
  drmModePlanePtr plane;
  drmModeCrtcPtr crtc;
  drmModeFBPtr fb;
  int buf;
  EGLImageKHR image;
  GLuint texture;
  DarmPlane(drmModePlanePtr p, drmModeCrtcPtr c, drmModeFBPtr f, int b):
    plane(p),
    crtc(c),
    fb(f),
    buf(b),
    image(EGL_NO_IMAGE_KHR),
    texture(0) {}
  DarmPlane():
    plane(NULL),
    crtc(NULL),
    fb(NULL),
    buf(-1),
    image(EGL_NO_IMAGE_KHR),
    texture(0) {}
};

bool composeFrameEGL() {
  std::vector<DarmPlane> availPlanes;

  planeres=drmModeGetPlaneResources(fd);
  if (planeres==NULL) {
    logE("\x1b[2K\x1b[1;31m%d: plane resources unavailable!\x1b[m\n");
    return false;
  }

  for (unsigned int i=0; i<planeres->count_planes; i++) {
    plane=drmModeGetPlane(fd,planeres->planes[i]);
    if (plane==NULL) {
      //logD("skipping plane %d...\n",planeres->planes[i]);
      continue;
    }

    if (plane->fb_id==0) {
      //printf("\x1b[2K\x1b[1;31m%d: the plane doesn't have a framebuffer!\x1b[m\n",frame);
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }

    drmModeCrtcPtr crtc=drmModeGetCrtc(fd,plane->crtc_id);
    if (crtc==NULL) {
      printf("\x1b[2K\x1b[1;31m%d: no CRTC!\x1b[m\n",frame);
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }

    //printf("\x1b[2K\x1b[1;32m%d: composing FB %d...\x1b[m\n",frame,plane->fb_id);
    
    fb=drmModeGetFB(fd,plane->fb_id);
    if (fb==NULL) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer no longer exists!\x1b[m\n",frame);
      drmModeFreeCrtc(crtc);
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }
    if (!fb->handle) {
      printf("\x1b[2K\x1b[1;31m%d: the framebuffer has invalid handle!\x1b[m\n",frame);
      drmModeFreeFB(fb);
      drmModeFreeCrtc(crtc);
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }

    if (drmPrimeHandleToFD(fd,fb->handle,O_RDONLY,&primefd)<0) {
      printf("\x1b[2K\x1b[1;31m%d: unable to prepare framebuffer for the compositor!\x1b[m\n",frame);
      drmModeFreeFB(fb);
      drmModeFreeCrtc(crtc);
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }

    availPlanes.push_back(DarmPlane(plane,crtc,fb,primefd));

    plane=NULL;
  }

  // create textures
  for (DarmPlane& i: availPlanes) {
    //logD("- %d\n",i.fb->fb_id);
    EGLint imageAttr[]={
      EGL_WIDTH, (int)i.fb->width,
      EGL_HEIGHT, (int)i.fb->height,
      EGL_LINUX_DRM_FOURCC_EXT, GBM_FORMAT_ARGB8888,
      EGL_DMA_BUF_PLANE0_FD_EXT, i.buf,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
      EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)i.fb->pitch,
      EGL_NONE,
    };

    i.image=eglCreateImageKHR(eglInst,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,imageAttr);
    if (i.image==EGL_NO_IMAGE_KHR) {
      logE("couldn't create image.\n");
      continue;
    }
    // supposedly close goes here, but no thanks.

    //logD("create output texture...\n");
    glGenTextures(1,&i.texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES,i.texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,i.image);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  }

  unsigned int curPos=cursorPos;
  short cursorX=curPos>>16;
  short cursorY=curPos&0xffff;
  //logD("cursor pos: %d, %d\n",cursorX,cursorY);

  // prepare positions
  int index=0;
  for (DarmPlane& i: availPlanes) {
    if (index && !captureCursor) break;
    int px=index?cursorX:i.plane->x;
    int py=index?cursorY:i.plane->y;
    double x=((double)px/((double)ow*0.5))-1.0;
    double y=((double)py/((double)oh*0.5))-1.0;
    double w=(i.fb->width/((double)ow*0.5));
    double h=(i.fb->height/((double)oh*0.5));
    planeTri[index][0]=x;
    planeTri[index][1]=y;
    planeTri[index][2]=0.0f;
    planeTri[index][3]=x+w;
    planeTri[index][4]=y;
    planeTri[index][5]=0.0f;
    planeTri[index][6]=x;
    planeTri[index][7]=y+h;
    planeTri[index][8]=0.0f;
    planeTri[index][9]=x+w;
    planeTri[index][10]=y+h;
    planeTri[index][11]=0.0f;

    planeUV[index][0]=0.0f;
    planeUV[index][1]=0.0f;
    planeUV[index][2]=1.0f;
    planeUV[index][3]=0.0f;
    planeUV[index][4]=0.0f;
    planeUV[index][5]=1.0f;
    planeUV[index][6]=1.0f;
    planeUV[index][7]=1.0f;

    index++;
  }

  // draw
  glBindFramebuffer(GL_FRAMEBUFFER,compoFB);

  glClearColor(0.35, 0.5, 0.75, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindBuffer(GL_ARRAY_BUFFER,planeTriBO);
  glBufferData(GL_ARRAY_BUFFER,sizeof(planeTri)+sizeof(planeUV),planeTri,GL_STATIC_DRAW);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,NULL);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,(void*)sizeof(planeTri));
  glEnableVertexAttribArray(1);

  // ???
  int first=0;
  for (DarmPlane& i: availPlanes) {
    if (first && !captureCursor) break;
    glUseProgram(renderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES,i.texture);
    glUniform1i(renderTexture,0);
    glDrawArrays(GL_TRIANGLE_STRIP,first,4);
    first+=4;
  }
  
  glFlush();

  // cleanup
  for (DarmPlane& i: availPlanes) {
    if (i.texture) {
      glDeleteTextures(1,&i.texture);
    }
    if (i.image!=EGL_NO_IMAGE_KHR) {
      eglDestroyImageKHR(eglInst,i.image);
    }
    close(i.buf);
    drmModeFreeFB(i.fb);
    drmModeFreeCrtc(i.crtc);
    drmModeFreePlane(i.plane);
  }
  drmModeFreePlaneResources(planeres);

  return true;
}

bool needsValue(string param) {
  for (size_t i=0; i<params.size(); i++) {
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
  for (size_t i=start; i!=((start+w)%dsize); i=(i+1)%dsize) {
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

void handleTerm(int) {
  quit=true;
}

void handleWinCh(int) {
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

bool pSetH264(string) {
  if (meitner) {
    encName="libx264rgb";
  } else if (hesse) {
    encName="h264_nvenc";
  } else {
    encName="h264_vaapi";
  }
  return true;
}

bool pSetHEVC(string) {
  if (meitner) {
    encName="libx265";
  } else if (hesse) {
    encName="hevc_nvenc";
  } else {
    encName="hevc_vaapi";
  }
  return true;
}

bool pSetMJPEG(string) {
  if (hesse) {
    logE("NVIDIA cards do not support MJPEG encoding.\n");
    return false;
  } else {
    encName="mjpeg_vaapi";
  }
  return true;
}

bool pSetMPEG2(string) {
  if (hesse) {
    logE("NVIDIA cards do not support MPEG-2 encoding.\n");
    return false;
  } else {
    encName="mpeg2_vaapi";
  }
  return true;
}

bool pSetVP8(string) {
  if (hesse) {
    logE("NVIDIA cards do not support VP8 encoding.\n");
    return false;
  } else {
    encName="vp8_vaapi";
  }
  return true;
}

bool pSetVP9(string) {
  if (hesse) {
    logE("NVIDIA cards do not support VP9 encoding.\n");
    return false;
  } else {
    encName="vp9_vaapi";
  }
  return true;
}

bool pSetFFVHuff(string) {
  if (meitner) {
    encName="ffvhuff";
    logD("setting ffvhuff as encoder.\n");
    huff=true;
  } else {
    logE("only available in Meitner mode.\n");
    return false;
  }
  return true;
}

bool pSetHuffYUV(string) {
  if (meitner) {
    encName="huffyuv";
    logD("setting huffyuv as encoder.\n");
    huff=true;
  } else {
    logE("only available in Meitner mode.\n");
    return false;
  }
  return true;
}

bool pSetRawVideo(string) {
  if (meitner) {
    encName="rawvideo";
    logD("setting rawvideo as encoder.\n");
    huff=true;
  } else {
    logE("only available in Meitner mode.\n");
    return false;
  }
  return true;
}

bool pListDevices(string) {
  for (int i=0; i<deviceCount; i++) {
    printf("- device %d (%s) (%.2x:%.2x.%x): %s\n",i,getVendor(i),
           devices[i]->businfo.pci->bus,
           devices[i]->businfo.pci->dev,
           devices[i]->businfo.pci->func,
           devices[i]->nodes[DRM_NODE_PRIMARY]);
  }
  return false;
}

bool pListDisplays(string) {
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

bool pSetBusID(string val) {
  for (int i=0; i<deviceCount; i++) {
    if (strFormat("%.2x:%.2x.%x",devices[i]->businfo.pci->bus,devices[i]->businfo.pci->dev,devices[i]->businfo.pci->func)==val) {
      autoDevice=i;
      break;
    }
  }
  
  if (autoDevice==-1) {
    logE("no devices under this bus found.\n");
    return false;
  }
  
  if (devImportance(autoDevice)==-9001) {
    logE("seriously? did you think that would work?\n");
    logE("NVIDIA is supported in encode-only mode (-hesse).\n");
    return false;
  }

  return true;
}

bool pSetDisplay(string) {
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

bool pSet10Bit(string) {
  tenBit=true;
  return true;
}

bool pSet444(string) {
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

bool pSetAudioDev(string val) {
  audName=val;
  return true;
}

bool pSetAudioCodec(string val) {
  audEncName=val;
  return true;
}

bool pSetAudioLim(string) {
  audLimiter=true;
  return true;
}

bool pSetAudioVol(string val) {
  try {
    audGain=stof(val);
  } catch (std::exception& e) {
    logE("invalid volume.\n");
    return false;
  }
  
  if (audGain<0 || audGain>1) {
    logE("it must be between 0.0 and 1.0.\n");
    return false;
  }
  return true;
}

bool pMeitner(string) {
  meitner=true;
  hesse=true;
  encName="libx264rgb";
  return true;
}

bool pHesse(string) {
  hesse=true;
  encName="h264_nvenc";
  return true;
}

bool pHelp(string) {
  printf("usage: darmstadt [params] [filename]\n\n"
         "by default darmstadt outputs to %s.\n",outname);
  for (auto& i: params) {
    for (auto& j: categories) {
      if (j.cmdName==i.name) {
        printf("\n%s:\n",j.name.c_str());
        break;
      }
    }
    if (i.value) {
      printf("  -%s %s: %s\n",i.name.c_str(),i.valName.c_str(),i.desc.c_str());
    } else {
      printf("  -%s: %s\n",i.name.c_str(),i.desc.c_str());
    }
  }
  return false;
}

bool hesseBench(string u) {
  char* image[3];
  int size;
  struct timespec bBegin, bEnd, bDelta;
  double delta;

  logI("running hesse overhead benchmark!\n");

  // 1080p
  logI("1080p...\n");
  size=1920*1080*4;
  for (int i=0; i<3; i++) {
    image[i]=new char[size];
  }
  bBegin=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<1920; i++) {
    memcpy(image[1],image[0],size);
    memcpy(image[2],image[1],size);
    memcpy(image[1],image[0],size);
  }
  bEnd=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<3; i++) {
    delete[] image[i];
  }

  bDelta=bEnd-bBegin;
  delta=bDelta.tv_sec+bDelta.tv_nsec/1000000000.0;
  logI("%.2f%% overhead (%.0f max FPS).\n",(60.0/(1920.0/delta))*100.0,(1920.0/delta));

  // 4K
  logI("2160p...\n");
  size=3840*2160*4;
  for (int i=0; i<3; i++) {
    image[i]=new char[size];
  }
  bBegin=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<480; i++) {
    memcpy(image[1],image[0],size);
    memcpy(image[2],image[1],size);
    memcpy(image[1],image[0],size);
  }
  bEnd=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<3; i++) {
    delete[] image[i];
  }

  bDelta=bEnd-bBegin;
  delta=bDelta.tv_sec+bDelta.tv_nsec/1000000000.0;
  logI("%.2f%% overhead (%.0f max FPS).\n",(60.0/(480.0/delta))*100.0,(480.0/delta));

  // 8K
  logI("4320p...\n");
  size=7680*4320*4;
  for (int i=0; i<3; i++) {
    image[i]=new char[size];
  }
  bBegin=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<120; i++) {
    memcpy(image[1],image[0],size);
    memcpy(image[2],image[1],size);
    memcpy(image[1],image[0],size);
  }
  bEnd=curTime(CLOCK_MONOTONIC);
  for (int i=0; i<3; i++) {
    delete[] image[i];
  }

  bDelta=bEnd-bBegin;
  delta=bDelta.tv_sec+bDelta.tv_nsec/1000000000.0;
  logI("%.2f%% overhead (%.0f max FPS).\n",(60.0/(120.0/delta))*100.0,(120.0/delta));

  logI("done. exiting.\n");
  return false;
}

bool pSetVideoSize(string val) {
  string num;
  size_t xpos;

  xpos=val.find_first_of("xX*,");
  if (xpos==string::npos) {
    logE("invalid size.\n");
    return false;
  }

  try {
    num=val.substr(0,xpos);
    ow=stoi(num);
    num=val.substr(xpos+1);
    oh=stoi(num);
  } catch (std::exception& e) {
    logE("invalid size.\n");
    return false;
  }

  if (ow<1 || oh<1) {
    logE("invalid size. it must be at least 1x1.\n");
    return false;
  }
  return true;
}

bool pSetVideoCrop(string val) {
  string num;
  size_t xpos, ppos1, ppos2;

  xpos=val.find_first_of("xX*");
  if (xpos==string::npos) {
    logE("invalid size.\n");
    return false;
  }

  ppos1=val.find_first_of("+");
  if (ppos1==string::npos) {
    logE("invalid position.\n");
    return false;
  }

  ppos2=val.find_first_of("+",ppos1+1);
  if (ppos2==string::npos) {
    logE("invalid position.\n");
    return false;
  }

  try {
    num=val.substr(0,xpos);
    cw=stoi(num);
    num=val.substr(xpos+1,ppos1-xpos);
    ch=stoi(num);
    num=val.substr(ppos1+1,ppos2-ppos1);
    cx=stoi(num);
    num=val.substr(ppos2+1);
    cy=stoi(num);
  } catch (std::exception& e) {
    logE("invalid size/position.\n");
    return false;
  }

  if (cx<0 || cy<0 || cw<1 || ch<1) {
    logE("invalid crop. it must fit on the screen.\n");
    return false;
  }

  logD("crop: %d, %d, %dx%d\n",cx,cy,cw,ch);
  
  doCrop=true;
  return true;
}

bool pSetVideoScale(string val) {
  if (val=="fill") {
    sm=scaleFill;
  } else if (val=="fit") {
    sm=scaleFit;
  } else if (val=="orig") {
    sm=scaleOrig;
  } else {
    logE("it must be fill, fit or orig.\n");
    return false;
  }
  return true;
}

bool pSetCursor(string val) {
  if (val=="on") {
    captureCursor=true;
  } else if (val=="off") {
    captureCursor=false;
  } else {
    logE("it must be on or off.\n");
    return false;
  }
  return true;
}

bool pSetQuality(string val) {
  try {
    qp=stoi(val);
  } catch (std::exception& e) {
    logE("invalid quality value. it must be between 0 and 50.\n");
    return false;
  }
  if (qp<0 || qp>50) {
    logE("invalid quality value. it must be between 0 and 50.\n");
    return false;
  }
  return true;
}

bool pSetBitRate(string val) {
  try {
    br=stoi(val);
  } catch (std::exception& e) {
    logE("invalid bitrate. ex\n");
    return false;
  }
  if (br<=0) {
    logE("invalid bitrate.\n");
    return false;
  }
  return true;
}

bool pSetEncSpeed(string val) {
  if (val=="speed") {
    encSpeed=encPerformance;
  } else if (val=="balanced") {
    encSpeed=encBalanced;
  } else if (val=="quality") {
    encSpeed=encQuality;
  } else {
    logE("it must be speed, balanced or quality.\n");
    return false;
  }
  return true;
}

bool pSetGOP(string val) {
  try {
    gopSize=stoi(val);
  } catch (std::exception& e) {
    logE("invalid value.\n");
    return false;
  }
  
  if (gopSize<1 || gopSize>250) {
    logE("must be between 1 and 250.\n");
    return false;
  }
  
  if (gopSize<8) {
    logW("keyframe interval is too small. this may result in a file that is unplayable.\n");
  }
  return true;
}

void initParams() {
  // information
  categories.push_back(Category("Information","help"));
  params.push_back(Param("h","help",false,pHelp,"","display this help"));
  params.push_back(Param("l","listdevices",false,pListDevices,"","list available devices"));
  params.push_back(Param("L","listdisplays",false,pListDisplays,"","list available displays")); // TODO
  
  // device selection
  categories.push_back(Category("Device selection","device"));
  params.push_back(Param("d","device",true,pSetDevice,"index","select device"));
  params.push_back(Param("V","vendor",true,pSetVendor,"AMD|Intel","select device by vendor"));
  params.push_back(Param("B","busid",true,pSetBusID,"xx:yy.z","select device by PCI bus ID"));
  params.push_back(Param("D","display",true,pSetDisplay,"name","select display to record")); // TODO
  
  // video options
  categories.push_back(Category("Video options","size"));
  params.push_back(Param("s","size",true,pSetVideoSize,"WIDTHxHEIGHT","set output size"));
  params.push_back(Param("c","crop",true,pSetVideoCrop,"WIDTHxHEIGHT+X+Y","crop screen contents"));
  params.push_back(Param("sc","scale",true,pSetVideoScale,"fit|fill|orig","set scaling method")); // TODO, methods not done
  params.push_back(Param("C","cursor",true,pSetCursor,"on|off","enable/disable X11 cursor overlay"));
  params.push_back(Param("S","skip",true,pSetSkip,"value","set frameskip value"));
  params.push_back(Param("10","10bit",false,pSet10Bit,"","use 10-bit pixel format"));
  params.push_back(Param("4","absoluteperfection",false,pSet444,"","use YUV 4:4:4 mode (NVIDIA-only)"));

  // codec selection
  categories.push_back(Category("Codec selection","h264"));
  params.push_back(Param("","h264",false,pSetH264,"","set video codec to H.264"));
  params.push_back(Param("","hevc",false,pSetHEVC,"","set video codec to H.265/HEVC"));
  params.push_back(Param("","mjpeg",false,pSetMJPEG,"","set video codec to MJPEG (not useful)"));
  params.push_back(Param("","mpeg2",false,pSetMPEG2,"","set video codec to MPEG-2"));
  params.push_back(Param("","vp8",false,pSetVP8,"","set video codec to VP8"));
  params.push_back(Param("","vp9",false,pSetVP9,"","set video codec to VP9"));
  params.push_back(Param("","ffvhuff",false,pSetFFVHuff,"","set video codec to FFVHuff (Meitner only)"));
  params.push_back(Param("","huffyuv",false,pSetHuffYUV,"","set video codec to HuffYUV (Meitner only)"));
  params.push_back(Param("","rawvideo",false,pSetRawVideo,"","set video codec to raw (Meitner only)"));
  
  // codec options
  categories.push_back(Category("Codec options","quality"));
  params.push_back(Param("q","quality",true,pSetQuality,"0-50","set quality (less is better)"));
  params.push_back(Param("b","bitrate",true,pSetBitRate,"value","set bitrate (in Kbps)"));
  params.push_back(Param("sp","speed",true,pSetEncSpeed,"quality|balanced|speed","set encoder quality/speed tradeoff (if supported)"));
  params.push_back(Param("g","keyinterval",true,pSetGOP,"frames","set interval between intra-frames (keyframes)"));
  
  // audio options
  categories.push_back(Category("Audio options","audio"));
  params.push_back(Param("a","audio",true,pSetAudio,"jack|pulse|alsa|none","set audio engine (none disables audio recording)"));
  params.push_back(Param("A","audiodev",true,pSetAudioDev,"name","set audio capture device"));
  params.push_back(Param("ac","audiocodec",true,pSetAudioCodec,"codec","set audio codec"));
  params.push_back(Param("al","limiter",false,pSetAudioLim,"","enable built-in audio limiter"));
  params.push_back(Param("av","volume",true,pSetAudioVol,"vol","set audio volume, between 0.0 and 1.0"));
  
  // VA-API -> NVENC codepath
  categories.push_back(Category("Hesse mode selection","hesse"));
  params.push_back(Param("H","hesse",false,pHesse,"","enable hesse mode (use AMD/Intel card for capture and NVIDIA card for encoding)"));
  params.push_back(Param("B","hessebench",false,hesseBench,"","benchmark hesse overhead"));

  // VA-API -> soft codepath
  categories.push_back(Category("Meitner mode selection","meitner"));
  params.push_back(Param("M","meitner",false,pMeitner,"","enable meitner mode (use AMD/Intel card for capture and software encoder)"));
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
  memset(&saTerm,0,sizeof(struct sigaction));
  memset(&saWinCh,0,sizeof(struct sigaction));
  saTerm.sa_handler=handleTerm;
  saWinCh.sa_handler=handleWinCh;
  sigemptyset(&saTerm.sa_mask);
  sigemptyset(&saWinCh.sa_mask);
  saWinCh.sa_flags=0;
  quit=false; newSize=false;
  syncMethod=syncVBlank;
  sm=scaleFit;
  totalWritten=0;
  bitRate=0;
  bitRatePre=0;
  framerate=0;
  fskip=1;
  qp=-1;
  br=-1;
  gopSize=-1;
  encSpeed=-1;
  ow=-1;
  oh=-1;
  cacheEmpty=false;
  cacheRun=false;
  noSignal=false;
  paused=false;
  hesse=false;
  meitner=false;
  absolPerf=false;
  tenBit=false;
  audLimiter=false;
  audLimAmt=1;
  audGain=1;
  audioType=audioTypeJACK;
  audName="";
  autoDevice=-1;
  brTime=curTime(CLOCK_MONOTONIC);
  tb=(AVRational){1,100000};
  
  encName="";
  audEncName="pcm_f32le";

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
      for (size_t j=0; j<params.size(); j++) {
        if (params[j].name==arg || params[j].shortName==arg) {
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

  logD("creating GBM device...\n");
  gbmDevice=gbm_create_device(fd);
  if (gbmDevice==NULL) {
    logE("couldn't open GBM device.\n");
    return 1;
  }
  
  planeres=drmModeGetPlaneResources(fd);
  if (planeres==NULL) {
    logE("i have to tell you... that I am unable to get plane resources...\n");
    return 1;
  }
  
  logD("number of planes: %d\n",planeres->count_planes);
  for (unsigned int i=0; i<planeres->count_planes; i++) {
    plane=drmModeGetPlane(fd,planeres->planes[i]);
    if (plane==NULL) {
      logD("skipping plane %d...\n",planeres->planes[i]);
      continue;
    }

    logD("trying plane %d (%d) with fb_id %d...\n",plane->plane_id,i,plane->fb_id);
    
    if (plane->fb_id==0) {
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }
    
    logD("using plane %d (%d in list) %d, %d.\n",plane->plane_id,i,plane->x,plane->y);
    break;
  }
  
  if (plane==NULL) {
    logE("no displays available. can't record.\n");
    return 1;
  }

  disp=drmModeGetCrtc(fd,plane->crtc_id);
  if (disp==NULL) {
    logE("could not retrieve display info...\n");
    return 1;
  }

  framerate=disp->mode.vrefresh;
  logD("refresh rate: %d\n",framerate);
  
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
 
  if (doCrop) {
    if ((cx+cw)>=dw || (cy+ch)>=dh) {
      logE("invalid crop. it must fit on the screen.\n");
      return 1;
    }
  }

  if (ow<1 || oh<1) {
    if (doCrop) {
      ow=cw;
      oh=ch;
    } else {
      ow=dw;
      oh=dh;
    }
  }

  logD("initializing EGL.\n");
  if (!initEGL()) return 1;

  if (encName=="") {
    // 2880x1800 = 5.1 megapixels
    if (ow*oh>=5184000) {
      encName="hevc_vaapi";
    } else {
      encName="h264_vaapi";
    }
  }
  
  // warn user about 1920x1088 bug
  if (devImportance(autoDevice)==2 && encName=="hevc_vaapi") {
    if (ow%16!=0 || oh%16!=0) {
      logW("warning: due to a bug in the AMD H.265 encoder, your video will have a wrong resolution.\n");
    }
  }
  
  if (fb) drmModeFreeFB(fb);
  if (plane) drmModeFreePlane(plane);
  if (planeres) drmModeFreePlaneResources(planeres);
  
  // DRM INIT END //
  
  // VA-API INIT BEGIN //
  logD("getting VA display\n");
  if (devImportance(autoDevice)==1) {
    vaInst=vaGetDisplayDRM(renderfd);
  } else {
    vaInst=vaGetDisplayDRM(fd);
  }
  if (!vaDisplayIsValid(vaInst)) {
    logE("could not open VA-API...\n");
    return 1;
  }
  logD("initializing VA-API\n");
  if (vaInitialize(vaInst,&discvar1,&discvar2)!=VA_STATUS_SUCCESS) {
    logE("could not initialize VA-API...\n");
    return 1;
  }
  
  string vendorName=vaQueryVendorString(vaInst);
  logD("va-api instance opened. %s.\n",vendorName.c_str());
  
  // warn the user about newer Mesa releases
  if (vendorName.find("Mesa")!=std::string::npos) {
    if (vendorName.find("19.0")==std::string::npos) {
      if (devImportance(autoDevice)==2) {
        logW("Mesa versions after 19.0 may hang on certain AMD cards while recording! be careful!\n");
      }
    }
  }
  
  logD("querying image formats\n");
  vaQueryImageFormats(vaInst,allowedFormats,&allowedFormatsSize);
  
  theFormat=-1;
  for (int i=0; i<allowedFormatsSize; i++) {
    if (allowedFormats[i].fourcc==VA_FOURCC_BGRX) {
      theFormat=i;
      logD("the format is %d\n",theFormat);
      break;
    }
  }
  if (theFormat==-1) {
    logE("no compatible pixel format found!\n");
    return 1;
  }
  
  logD("creating VA config\n");
  if (vaCreateConfig(vaInst,VAProfileNone,VAEntrypointVideoProc,NULL,0,&vaConfS)!=VA_STATUS_SUCCESS) {
    logE("no videoproc.\n");
    return 1;
  }

  logD("creating VA format converter context\n");
  if (vaCreateContext(vaInst,vaConfS,ow,oh,VA_PROGRESSIVE,NULL,0,&scalerC)!=VA_STATUS_SUCCESS) {
    logE("we can't scale...\n");
    return 1;
  }

  logD("allocating context for FFmpeg\n");
  wrappedSource=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);

  if (wrappedSource==NULL) {
    logE("failure to\n");
    return 1;
  }

  extractSource=(AVHWDeviceContext*)wrappedSource->data;
  
  // only do so if not in hesse mode
  if (!hesse) {
    logD("PRE: %p\n",extractSource);
    logD("creating FFmpeg hardware instance\n");
    if (av_hwdevice_ctx_create_derived(&ffhardInst, AV_HWDEVICE_TYPE_VAAPI, wrappedSource, 0)<0) {
      logE("check me NOW\n");
      return 1;
    }
    extractSource=(AVHWDeviceContext*)ffhardInst->data;
    logD("POST: %p\n",extractSource);
    ((AVVAAPIDeviceContext*)(extractSource->hwctx))->display=vaInst;
    ((AVVAAPIDeviceContext*)(extractSource->hwctx))->driver_quirks=0;
    logD("initializing the context\n");
    if (av_hwdevice_ctx_init(ffhardInst)<0) {
      logE("aaaaaand it failed.\n");
      return 1;
    }

    logD("quirks: %x\n",((AVVAAPIDeviceContext*)(extractSource->hwctx))->driver_quirks);
  } else {
    logD("creating image in memory\n");
  }
  // VA-API INIT END //
  
  // FFMPEG INIT BEGIN //
  // open file
  logD("creating output context\n");
  unlink(outname);
  avformat_alloc_output_context2(&out,NULL,"nut",outname);
  if (out==NULL) {
    logE("couldn't open output...\n");
    return 1;
  }
  
  /// Video
  logD("creating encoder\n");
  // create stream
  if (meitner && absolPerf && !huff) {
    logD("setting libx264rgb as encoder.\n");
    encName="libx264rgb";
  }
  if ((encInfo=avcodec_find_encoder_by_name(encName.c_str()))==NULL) {
    logE("your card does not support %s.\n",encName.c_str());
    return 1;
  }
  
  stream=avformat_new_stream(out,NULL);
  // TODO: possibly change this to 2400
  //       (for 50, 60, 100, 120 and 144)
  stream->time_base=(AVRational){1,900};
  stream->start_time=0;
  
  encoder=avcodec_alloc_context3(encInfo);
  
  encoder->codec_id=encInfo->id;
  encoder->width=ow;
  encoder->height=oh;
  encoder->time_base=tb;
  //encoder->thread_count=4;
  if (framerate>0) {
    encoder->framerate=(AVRational){framerate,fskip};
  }
  encoder->sample_aspect_ratio=(AVRational){1,1};
  if (gopSize>=1) {
    encoder->gop_size=gopSize;
  } else {
    if (hesse) {
      encoder->gop_size=90;
    } else {
      encoder->gop_size=40;
    }
  }
  if (hesse) {
    encoder->pix_fmt=AV_PIX_FMT_BGR0;
  } else {
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
    // what hang? the Mesa 19.1 one?
    ((AVHWFramesContext*)hardFrameDataR->data)->initial_pool_size = 2;
    ((AVHWFramesContext*)hardFrameDataR->data)->format=AV_PIX_FMT_VAAPI;
    ((AVHWFramesContext*)hardFrameDataR->data)->sw_format=AV_PIX_FMT_NV12;
    av_hwframe_ctx_init(hardFrameDataR);
    encoder->hw_frames_ctx=hardFrameDataR;
  }
  
  if (br>0) {
    logD("using constant bitrate mode.\n");
    // warn user about AMD H.265 CBR bug
    if (devImportance(autoDevice)==2 && encName=="hevc_vaapi") {
      logW("constant bitrate mode may be broken under AMD cards for the H.265 codec! it is likely that the output will be corrupted.\n");
    }
    av_dict_set(&encOpt,"b",strFormat("%dK",br).c_str(),0);
    av_dict_set(&encOpt,"maxrate",strFormat("%dK",br).c_str(),0);
  } else {
    logD("using constant quality mode.\n");
    if (qp>=0) {
      av_dict_set(&encOpt,"qp",strFormat("%d",qp).c_str(),0);
    } else {
      av_dict_set(&encOpt,"qp","26",0);
    }
  }
  
  if (hesse) {
    if (absolPerf) {
      if (!meitner) {
        av_dict_set(&encOpt,"profile","high444p",0);
      }
    }
  }

  if (encName=="huffyuv" || encName=="ffvhuff") {
    //av_dict_set(&encOpt,"non_deterministic","1",0);
    //av_dict_set(&encOpt,"context","1",0);
  }

  if (hesse) {
    if (meitner) {
      switch (encSpeed) {
        case encPerformance:
          av_dict_set(&encOpt,"preset","ultrafast",0);
          break;
        case encBalanced:
          av_dict_set(&encOpt,"preset","faster",0);
          break;
        case encQuality:
          av_dict_set(&encOpt,"preset","fast",0);
          break;
        default:
          break;
      }
    } else {
      switch (encSpeed) {
        case encPerformance:
          av_dict_set(&encOpt,"preset","fast",0);
          break;
        case encBalanced:
          av_dict_set(&encOpt,"preset","medium",0);
          break;
        case encQuality:
          av_dict_set(&encOpt,"preset","slow",0);
          break;
        default:
          break;
      }
    }
  } else {
    switch (encSpeed) {
      case encPerformance:
        av_dict_set(&encOpt,"compression_level","7",0);
        break;
      case encBalanced:
        av_dict_set(&encOpt,"compression_level","4",0);
        break;
      case encQuality:
        av_dict_set(&encOpt,"compression_level","0",0);
        break;
      default:
        break;
    }
  }
  
  if (out->oformat->flags&AVFMT_GLOBALHEADER) {
    logD("video encoder should have global header...\n");
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  
  /// Audio
  switch (audioType) {
#ifdef HAVE_JACK
    case audioTypeJACK:
      ae=new JACKAudioEngine;
      break;
#endif
    case audioTypePulse:
      ae=new PulseAudioEngine;
      break;
    case audioTypeNone:
      break;
    default:
      logE("this audio type is not available!\n");
      return 1;
      break;
  }
  if (audioType!=audioTypeNone) {
    logD("initializing audio...\n");
  }
  if ((audioType==audioTypeNone) || !ae->init(audName)) {
    if (audioType!=audioTypeNone) {
      logW("couldn't init audio.\n");
    }
    audioType=audioTypeNone;
  } else {
    logD("finding audio codec (%s)...\n",audEncName.c_str());
    if ((audEncInfo=avcodec_find_encoder_by_name(audEncName.c_str()))==NULL) {
      logE("audio codec not found D:\n");
      return 1;
    }
    
    logD("creating audio stream...\n");
    audStream=avformat_new_stream(out,NULL);
    audStream->start_time=0;
    
    logD("creating audio codec context...\n");
    audEncoder=avcodec_alloc_context3(audEncInfo);
    audEncoder->sample_fmt=audEncInfo->sample_fmts?audEncInfo->sample_fmts[0]:AV_SAMPLE_FMT_FLTP;
    audEncoder->sample_rate=ae->sampleRate();
    audEncoder->ch_layout.nb_channels=ae->channels();
    audEncoder->ch_layout.order=AV_CHANNEL_ORDER_NATIVE;
    audEncoder->time_base=(AVRational){1,audEncoder->sample_rate};
    audStream->time_base=(AVRational){1,audEncoder->sample_rate};
    switch (audEncoder->ch_layout.nb_channels) {
      case 1:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_MONO;
        break;
      case 2:
        audEncoder->ch_layout=AV_CHANNEL_LAYOUT_STEREO;
        break;
      case 3:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_2POINT1;
        break;
      case 4:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_QUAD;
        break;
      case 5:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_5POINT0;
        break;
      case 6:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_5POINT1;
        break;
      case 7:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_7POINT0;
        break;
      case 8:
        audEncoder->ch_layout.u.mask=AV_CH_LAYOUT_7POINT1;
        break;
    }
    
    if (out->oformat->flags&AVFMT_GLOBALHEADER) {
      logD("audio encoder should have global header...\n");
      audEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
  }
  
  AVDictionary* outOpt=NULL;
  av_dict_set(&outOpt,"global_header","1",0);

  logD("initializing output...\n");
  /*if (avformat_init_output(out,&outOpt)<0) {
    logE("error while initializing the output!\n");
    return 1;
  }*/
  
  // Open Video
  logD("opening video encoder\n");
  if ((avcodec_open2(encoder,encInfo,&encOpt))<0) {
    logE("could not open encoder :(\n");
    return 1;
  }
  logD("video parameters from context...\n");
  avcodec_parameters_from_context(stream->codecpar,encoder);
  
  // Open Audio
  if (audioType!=audioTypeNone) {
    logD("opening audio encoder\n");
    if ((avcodec_open2(audEncoder,audEncInfo,&audEncOpt))<0) {
      logE("could not open encoder :(\n");
      return 1;
    }
    logD("audio parameters from context...\n");
    avcodec_parameters_from_context(audStream->codecpar,audEncoder);
    
    logD("allocating audio frame\n");
    audFrame=av_frame_alloc();
    if (audFrame==NULL) {
      logE("couldn't allocate audio frame!\n");
    }
    audFrame->pts=1024;
    audFrame->pkt_dts=1024;
    audFrame->format=audEncoder->sample_fmt;
    audFrame->ch_layout=audEncoder->ch_layout;
    if (audEncInfo->capabilities&AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
      audFrame->nb_samples=1024;
    } else {
      audFrame->nb_samples=audEncoder->frame_size;
    }
    printf("format: %d\n",audFrame->format);
    logD("getting audio buffer...\n");
    int audErr=av_frame_get_buffer(audFrame,0);
    if (audErr<0) {
      logE("are you being serious? %s\n",strerror(-audErr));
      return 1;
    }
  }
  
  logD("opening file...\n");
  if ((f=open(outname,O_CREAT|O_RDWR,0666))<0) {
    perror("could not open output file");
    return 1;
  }
  
  if (!(out->oformat->flags & AVFMT_NOFILE)) {
    logD("creating I/O buffer...\n");
    unsigned char* ioBuffer=(unsigned char*)av_malloc(DARM_AVIO_BUFSIZE+AV_INPUT_BUFFER_PADDING_SIZE);
    cache.setFile(f);
    logD("allocating the I/O context...\n");
    AVIOContext* avioContext=avio_alloc_context(ioBuffer,DARM_AVIO_BUFSIZE,1,(void*)(&f),NULL,&writeToCache,&seekCache);
    out->pb=avioContext;
    cache.enable(DARM_RINGBUF_SIZE);
  }

  int retv;
  char e[4096];
  logD("writing header!\n");
  if ((retv=avformat_write_header(out,&outOpt))<0) {
    av_make_error_string(e,4095,retv);
    logE("could not write header... %s\n",e);
    return 1;
  }
  // FFMPEG INIT END //

  // CURSOR INIT BEGIN //
  if (captureCursor) {
    if (pthread_create(&x11Thread,NULL,x11CursorThread,NULL)!=0) {
      logW("couldn't create cursor thread!\n");
    }
  }
  // CURSOR INIT END //

  sigaction(SIGINT,&saTerm,NULL);
  sigaction(SIGTERM,&saTerm,NULL);
  sigaction(SIGHUP,&saTerm,NULL);
  
  sigaction(SIGWINCH,&saWinCh,NULL);

  memset(speeds,0,sizeof(int)*120);
  
  wbReadPos=0;
  wbWritePos=0;
  
  printf("\x1b[1m|\x1b[m\n");
  if (meitner) {
    printf("\x1b[1m|\x1b[0;34m ~~~~> \x1b[1;33mDARMSTADT => EGL <= (meitner mode) \x1b[0;32m" DARM_VERSION "\x1b[m\n");
  } else if (hesse) {
    printf("\x1b[1m|\x1b[0;32m ~~~~> \x1b[1;32mDARMSTADT => EGL <= (hesse mode) \x1b[0;32m" DARM_VERSION "\x1b[m\n");
  } else {
    printf("\x1b[1m|\x1b[1;33m ~~~~> \x1b[1;36mDARMSTADT => EGL <= \x1b[1;32m" DARM_VERSION "\x1b[m\n");
  }
  if (qp==0) printf("\x1b[1;31m> !!! -> -> LOSSLESS <- <- !!! <\x1b[m\n");
  printf("\x1b[1m|\x1b[m\n");
  if (meitner) {
    printf("\x1b[1m- screen %dx%d, output %dx%d, %s, %s\x1b[m\n",dw,dh,ow,oh,"RGB 4:4:4",encName.c_str());
  } else {
    printf("\x1b[1m- screen %dx%d, output %dx%d, YUV %s, %s\x1b[m\n",dw,dh,ow,oh,absolPerf?("4:4:4"):("4:2:0"),encName.c_str());
  }
  if (audioType!=audioTypeNone) {
    printf("\x1b[1m- audio from %s, %dHz (%d channels)\n",ae->engineName(),ae->sampleRate(),ae->channels());
  }
  
  logI("recording!\n");
  
  frame=0;
  
  struct timespec nextMilestone=mkts(0,0);
  lastATime=curTime(CLOCK_MONOTONIC);
  sATime=lastATime;
  
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
    // initialize the image
    if ((vaStat=vaCreateImage(vaInst,&allowedFormats[theFormat],dw,dh,&img))!=VA_STATUS_SUCCESS) {
      logE("could not create image... %x\n",vaStat);
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
  int arecal=0;
  int audioStalls=0;
  
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
    
    /*
    while ((curTime(CLOCK_MONOTONIC))<mkts(vreply.tval_sec,1000000+vreply.tval_usec*1000)) {
      usleep(2000);
    }*/

    //printf("POST: % 5ldµs. \n",(curTime(CLOCK_MONOTONIC)-mkts(vreply.tval_sec,vreply.tval_usec*1000)).tv_nsec/1000);
   
    //printf("\x1b[2J\x1b[1;1H\n");
    
    dtime=vtime;
    vtime=mkts(vreply.tval_sec,vreply.tval_usec*1000)-btime;
    if (btime==mkts(0,0)) {
      btime=vtime;
      vtime=mkts(0,0);
    }
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
      "\x1b[mqueue: \x1b[1m%6ldK "
      // bitrate
      "\x1b[mrate: %s%6ldKbit "
      // size
      "\x1b[msize: \x1b[1m%ldM "
      // A/V sync
      "\x1b[mA-V: \x1b[1m%ldms"
      // end
      "\x1b[m\x1b[%d;1H\n",
      
      // ARGUMENTS
      winSize.ws_row,
      // frame
      (noSignal)?("\x1b[1;31m-> NO SIGNAL <- \x1b[m"):(strFormat("frame \x1b[1m% 8d\x1b[m: ",frame).c_str()),
      // time
      vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,
      // FPS
      (delta>=(50/fskip))?("\x1b[1;32m"):("\x1b[1;33m"),delta,
      // queue
      cache.queueSize()>>10,
      // bitrate
      (bitRate>=30000000)?("\x1b[1;33m"):("\x1b[1;32m"),bitRate>>10,
      // size
      totalWritten>>20,
      // A/V sync
      (lastATime-sATime).tv_nsec/1000000,
      // end
      winSize.ws_row-1);
    
    if (frame>1 && delta>=0 && delta<120) {
      speeds[delta]++;
    }

    //printf("\x1b[2K\x1b[0;33m%d: time: %ldµs\x1b[m\n",frame,(tEnd-tStart).tv_nsec/1000);

    // RETRIEVE CODE BEGIN //
    tStart=curTime(CLOCK_MONOTONIC);

    intptr_t boFD=gbm_bo_get_fd(compoBo);
    if (boFD<0) {
      logE("could not fuck the BO\n");
      return 1;
    }

    vaCompoBD.buffers=(uintptr_t*)&boFD;
    vaCompoBD.pixel_format=VA_FOURCC_BGRX;
    vaCompoBD.width=ow;
    vaCompoBD.height=oh;
    vaCompoBD.data_size=ow*oh*4;
    vaCompoBD.num_buffers=1;
    vaCompoBD.flags=VA_SURFACE_EXTBUF_DESC_PROTECTED;
    vaCompoBD.pitches[0]=ow*4;
    vaCompoBD.offsets[0]=0;
    vaCompoBD.num_planes=1;
    
    coAttr[0].type=VASurfaceAttribMemoryType;
    coAttr[0].flags=VA_SURFACE_ATTRIB_SETTABLE;
    coAttr[0].value.type=VAGenericValueTypeInteger;
    coAttr[0].value.value.i=VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
    coAttr[1].type=VASurfaceAttribExternalBufferDescriptor;
    coAttr[1].flags=VA_SURFACE_ATTRIB_SETTABLE;
    coAttr[1].value.type=VAGenericValueTypePointer;
    coAttr[1].value.value.p=&vaCompoBD;

    if ((vaStat=vaCreateSurfaces(vaInst,VA_RT_FORMAT_RGB32,dw,dh,&portFrame,1,coAttr,3))!=VA_STATUS_SUCCESS) {
      logE("could not create surface for compositing... %x\n",vaStat);
      return 1;
    }

    if ((vaStat=vaSyncSurface(vaInst,portFrame))!=VA_STATUS_SUCCESS) {
      logE("no surface sync %x\n",vaStat);
    }

    if (!composeFrameEGL()) {
      logE("couldn't compose frame.\n");
      return 1;
    }

    //printf("\x1b[2K\x1b[1;32m%d: preparing frame...\x1b[m\n",frame);
    
    dsScanOff[dsScanOffPos++]=(wtEnd-wtStart).tv_nsec;
    dsScanOffPos&=1023;
    //makeGraph(0,2,40,12,true,dsScanOff,1024,dsScanOffPos-40);

    scaleRegion.x=0;
    scaleRegion.y=0;
    scaleRegion.width=ow;
    scaleRegion.height=oh;

    scaler.surface=portFrame;
    scaler.surface_region=NULL;
    scaler.surface_color_standard=VAProcColorStandardBT709;
    scaler.output_region=&scaleRegion;
    scaler.output_background_color=0xff000000;
    scaler.output_color_standard=VAProcColorStandardBT709;
    scaler.pipeline_flags=0;
    scaler.filter_flags=VA_FILTER_SCALING_HQ;

    if (hesse) {
      // HESSE NVENC CODE BEGIN //
      hardFrame->pts=(vtime.tv_sec*100000+vtime.tv_nsec/10000);
      hardFrame->pkt_dts=hardFrame->pts;

      // how come this fails? we need to rescale!
      if ((vaStat=vaGetImage(vaInst,portFrame,0,0,dw,dh,img.image_id))!=VA_STATUS_SUCCESS) {
        logW("couldn't get image! %x\n",vaStat);
      }
      if ((vaStat=vaMapBuffer(vaInst,img.buf,(void**)&addr))!=VA_STATUS_SUCCESS) {
        logW("oh come on! %x\n",vaStat);
      }
      hardFrame->data[0]=addr;
      writeFrame(encoder,hardFrame,out,stream);
      if ((vaStat=vaUnmapBuffer(vaInst,img.buf))!=VA_STATUS_SUCCESS) {
        logE("could not unmap buffer: %x...\n",vaStat);
      }
      // HESSE NVENC CODE END //
    } else {
      // VA-API ENCODE SINGLE-THREAD CODE BEGIN //
      hardFrame->pts=(vtime.tv_sec*100000+vtime.tv_nsec/10000);
      hardFrame->pkt_dts=hardFrame->pts;
      //printf("PTS: %d\n",hardFrame->pts);
  
      // convert
      massAbbrev=((AVVAAPIFramesContext*)(((AVHWFramesContext*)hardFrame->hw_frames_ctx->data)->hwctx));
  
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
      
      writeFrame(encoder,hardFrame,out,stream);
      // VA-API ENCODE SINGLE-THREAD CODE END //
    }
    //av_frame_free(&hardFrame);

    if ((vaStat=vaSyncSurface(vaInst,portFrame))!=VA_STATUS_SUCCESS) {
      printf("no surface sync %x\n",vaStat);
    }
    if ((vaStat=vaDestroySurfaces(vaInst,&portFrame,1))!=VA_STATUS_SUCCESS) {
      logE("destroy portframes error %x\n",vaStat);
      return 1;
    }
    close(boFD);
    // RETRIEVE CODE END //
    
    // AUDIO CODE BEGIN //
    if (audioType!=audioTypeNone) {
      AudioPacket* audioPack;
      while ((audioPack=ae->read())!=NULL) {
        // post-process
        for (int i=0; i<1024*ae->channels(); i++) {
          audioPack->data[i]*=audGain;
          if (audLimiter) {
            if (fabs(audioPack->data[i])>audLimAmt) {
              audLimAmt=fabs(audioPack->data[i]);
            }
            audioPack->data[i]/=audLimAmt;
            audLimAmt-=audLimAmt*0.00002;
            if (audLimAmt<1) audLimAmt=1;
          }
        }
        
        // copy audio to packet
        if (audFrame->format==AV_SAMPLE_FMT_FLT) {
          memcpy(audFrame->data[0],audioPack->data,1024*ae->channels()*sizeof(float));
        } else {
          switch (audFrame->format) {
            case AV_SAMPLE_FMT_U8:
              for (int i=0; i<1024*ae->channels(); i++) {
                if (audioPack->data[i]>1) {
                  ((unsigned char*)audFrame->data[0])[i]=255;
                } else if (audioPack->data[i]<-1) {
                  ((unsigned char*)audFrame->data[0])[i]=0;
                } else {
                  ((unsigned char*)audFrame->data[0])[i]=0x80+audioPack->data[i]*127;
                }
              }
              break;
            case AV_SAMPLE_FMT_S16:
              for (int i=0; i<1024*ae->channels(); i++) {
                if (audioPack->data[i]>1) {
                  ((short*)audFrame->data[0])[i]=32767;
                } else if (audioPack->data[i]<-1) {
                  ((short*)audFrame->data[0])[i]=-32767;
                } else {
                  ((short*)audFrame->data[0])[i]=audioPack->data[i]*32767;
                }
              }
              break;
            case AV_SAMPLE_FMT_S32:
              // floating point limitations
              for (int i=0; i<1024*ae->channels(); i++) {
                if (audioPack->data[i]>=1) {
                  ((int*)audFrame->data[0])[i]=2147483647;
                } else if (audioPack->data[i]<=-1) {
                  ((int*)audFrame->data[0])[i]=-2147483647;
                } else {
                  ((int*)audFrame->data[0])[i]=audioPack->data[i]*8388608;
                  ((int*)audFrame->data[0])[i]<<=8;
                }
              }
              break;
            default:
              logW("this sample format is not supported! (%d)\n",audFrame->format);
              break;
          }
        }
        lastATime=audioPack->time;
        delete audioPack;
        sATime=sATime+mkts(0,23219955);
        if (avcodec_send_frame(audEncoder,audFrame)<0) {
          logW("couldn't write audio frame!\n");
          break;
        }
        while (1) {
          audPacket=av_packet_alloc();
          if (audPacket==NULL) {
            logW("couldn't allocate audio packet!\n");
            break;
          }
          if (avcodec_receive_packet(audEncoder,audPacket)) {
            av_packet_free(&audPacket);
            break;
          }
          audPacket->stream_index=audStream->index;
          //audStream->cur_dts=audFrame->pts;
          audPacket->dts=audPacket->pts;
          av_packet_rescale_ts(audPacket,(AVRational){1,audEncoder->sample_rate},audStream->time_base);
          //audStream->cur_dts=audPacket->pts-1024;
          //printf("tb: %d/%d dts: %ld\n",audStream->time_base.num,audStream->time_base.den,audStream->cur_dts);
          if (av_write_frame(out,audPacket)<0) {
            printf("unable to write frame ATTENTION\n");
          }
          audFrame->pts+=1024;
          audFrame->pkt_dts+=1024;
          avio_flush(out->pb);
          if ((wtEnd-wtStart)>mkts(0,16666667)) {
            printf("\x1b[1;32mWARNING! audio write took too long :( (%ldµs)\n",(wtEnd-wtStart).tv_nsec/1000);
            audioStalls++;
          }
          av_packet_free(&audPacket);
        }
        if ((lastATime-sATime)>mkts(0,40000000)) {
          ae->wantBlank=true;
          printf("\x1b[2K\x1b[0;33m%d: audio is late! (%d)\x1b[m\n",frame,++arecal);
        }
      }
    }
    // AUDIO CODE END //
    
    tEnd=curTime(CLOCK_MONOTONIC);

    if (!noSignal) {
      frame++;
    }
    if (quit) break;
  }

  if (av_write_trailer(out)<0) {
    logW("could not finish file...\n");
  }
  
  logD("finishing output...\n");
  cache.disable();

  av_free(out->pb->buffer);
  close(f);
  avformat_free_context(out);
  
  if (hesse) {
    if ((vaStat=vaDestroyImage(vaInst,img.image_id))!=VA_STATUS_SUCCESS) {
      logE("could not destroy image %x\n",vaStat);
    }
  }

  avcodec_free_context(&encoder);
  vaDestroyContext(vaInst,scalerC);
  //vaDestroyContext(vaInst,copierC);
  vaTerminate(vaInst);

  close(fd);
  
  logI("finished.\n");

  printf("--FRAME REPORT--\n");
  if (frame<=1) {
    logW("too short! can't estimate.\n");
  } else {
    for (int i=0; i<120; i++) {
      if (speeds[i]>0) {
        printf("%d FPS: %d (%.2f%%)\n",i,speeds[i],100.0*((double)speeds[i]/(double)(frame-2)));
      }
    }
  }
  printf("dropped frames: %d\n",recal);
  printf("video write stalls: %d\n",videoStalls);
  printf("audio resync count: %d\n",arecal);
  printf("audio write stalls: %d\n",audioStalls);
  printf("repeated framebuffer handles: %d\n",repeatCount);
  logI("finished recording %.2ld:%.2ld:%.2ld.%.3ld (%d).\n",vtime.tv_sec/3600,(vtime.tv_sec/60)%60,vtime.tv_sec%60,vtime.tv_nsec/1000000,frame);
  return 0;
}
