// ~> DARMSTADT <~ - low-overhead full-hardware screen capture.
// so many things have occurred since...

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdexcept>
#include <string>
#include <deque>
#include <queue>
#include <mutex>

#include <jack/jack.h>

#include <pulse/error.h>
#include <pulse/simple.h>

#include "ta-log.h"

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavutil/channel_layout.h>
  #include <libavutil/hwcontext.h>
  #include <libavutil/hwcontext_vaapi.h>
}

#define DEVICE_PATH "/dev/dri/card1"

#define DARM_VERSION "v3.0pre2"

#define DARM_RINGBUF_SIZE 134217728

#define S(x) std::string(x)
typedef std::string string;

bool operator ==(const struct timespec& l, const struct timespec& r);
bool operator >(const struct timespec& l, const struct timespec& r);
bool operator <(const struct timespec& l, const struct timespec& r);
struct timespec operator +(const struct timespec& l, const struct timespec& r);
struct timespec operator -(const struct timespec& l, const struct timespec& r);
struct timespec operator +(const struct timespec& l, const long& r);
struct timespec operator -(const struct timespec& l, const long& r);
struct timespec mkts(time_t sec, long nsec);
struct timespec stots(string s);
string tstos(struct timespec ts);
struct timespec curTime(clockid_t clockSource);

string strFormat(const char* format, ...);

enum SyncMethod {
  syncVBlank=0,
  syncTimer
};

enum ScaleMethod {
  scaleFit=0,
  scaleFill,
  scaleOrig
};

enum EncodeSpeeds {
  encPerformance=0,
  encBalanced,
  encQuality
};

enum AudioType {
  audioTypeNone=0,
  audioTypeJACK,
  audioTypePulse,
  audioTypeALSA
};

struct qFrame {
  long fd;
  unsigned int format, pitch;
  size_t objsize;
  struct timespec ts;
  drmModeFB* fb;
  drmModePlane* plane;
  qFrame(int a, unsigned int b, unsigned int c, struct timespec d, drmModeFBPtr f, drmModePlanePtr pla): fd(a), pitch(c), objsize(b*c), ts(d), fb(f), plane(pla) {}
  qFrame(): fd(-1), pitch(0), objsize(0), ts(mkts(0,0)), fb(NULL), plane(NULL) {}
};

enum Vendor {
  VendorIntel=0,
  VendorAMD,
  VendorNVIDIA, // ?!?!
  VendorOther=255
};

struct Device {
  string path, busID, name;
  Vendor vendor;
};

struct Param {
  string shortName;
  string name;
  string valName;
  string desc;
  bool value;
  bool (*func)(string);
  Param(string sn, string n, bool v, bool (*f)(string), string vn, string d): shortName(sn), name(n), valName(vn), desc(d), value(v), func(f) {}
};

struct Category {
  string name;
  string cmdName;
  Category(string n, string c): name(n), cmdName(c) {}
};

struct AudioPacket {
  struct timespec time;
  // 1024 samples. up to 8 channels.
  float data[8192];
};

class AudioEngine {
  public:
    bool wantBlank;
    // read an audio packet. returns NULL if there aren't any.
    virtual AudioPacket* read();
    virtual int sampleRate();
    virtual int channels();
    virtual bool start();
    virtual bool init(string devName);
    virtual const char* engineName();
};

class JACKAudioEngine: public AudioEngine {
  jack_client_t* ac;
  jack_port_t* ai[8];
  jack_status_t as;
  int chan;
  string name;
  string devName;
  int collPos;
  AudioPacket ap;
  std::queue<AudioPacket*> apqueue;
  public:
    AudioPacket* read();
    int process(jack_nframes_t count);
    int sampleRate();
    int channels();
    bool start();
    bool init(string dn);
    const char* engineName();
};

class PulseAudioEngine: public AudioEngine {
  pa_simple* ac;
  pa_sample_spec asf;
  int as;
  pthread_t tid;
  AudioPacket ap;
  string devName;
  std::queue<AudioPacket*> apqueue;
  public:
    AudioPacket* read();
    void* thr();
    int sampleRate();
    int channels();
    bool start();
    bool init(string dn);
    const char* engineName();
};

enum CacheCommands {
  cWrite=0,
  cSeek,
  cRead // ???
};

struct CacheCommand {
  CacheCommands cmd;
  unsigned char* buf;
  ssize_t size;
  int whence;
  CacheCommand(CacheCommands c, unsigned char* b, int s, int w): cmd(c), buf(b), size(s), whence(w) {}
  CacheCommand() {}
};

class WriteCache {
  int o;
  bool running, shallStop, busy;
  pthread_t tid;
  std::deque<CacheCommand> cqueue;
  std::mutex m;
  unsigned char* ringBuf;
  ssize_t ringBufSize;
  ssize_t ringBufPosR, ringBufPosW;
  public:
    void* run();

    int write(unsigned char* buf, ssize_t len);
    int seek(ssize_t pos, int whence);
    ssize_t queueSize();
    bool flush();
    void setFile(int f);
    bool enable(ssize_t ringSize);
    bool disable();
    WriteCache();
};

extern bool quit;
