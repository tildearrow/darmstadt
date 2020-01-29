// ~> DARMSTADT <~ - low-overhead full-hardware screen capture.
//
// ...I need to write this little note.
// this project was originally designed for my own internal usage during
// early 2019, as a result of severe flaws (such as stuttering) with
// FFmpeg and OBS.
// since then, I have been tuning the software, trying to achieve
// 4K60 frame perfection.
// I went as far as having to practice raster mastery and write caching.
// .......................
// ...until...
// Shiny.............




























// -AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA!!!!!!!1

#include <stdio.h>
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
#include <queue>

#include <jack/jack.h>

#include <pulse/simple.h>

#include "ta-log.h"

// METHOD 1: use our own method for capture, and FFmpeg for encode
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavutil/hwcontext.h>
  #include <libavutil/hwcontext_vaapi.h>
}

#define DEVICE_PATH "/dev/dri/card1"

#define DARM_VERSION "v2.0pre"

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
  string name;
  bool value;
  bool (*func)(string);
  Param(string n, bool v, bool (*f)(string)): name(n), value(v), func(f) {}
};

struct AudioPacket {
  struct timespec time;
  // 1024 samples. up to 8 channels.
  float data[8192];
};

class AudioEngine {
  public:
    // read an audio packet. returns NULL if there aren't any.
    virtual AudioPacket* read();
    virtual int sampleRate();
    virtual int channels();
    virtual bool start();
    virtual bool init(string devName);
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
};

class PulseAudioEngine: public AudioEngine {
  pa_simple* ac;
};

int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx);
