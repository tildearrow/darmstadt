#include "darmstadt.h"

bool PulseAudioEngine::init(string dn) {
  FILE* catcher;
  char tDevName[4096];
  wantBlank=false;
  ac=NULL;
  
  asf.format=PA_SAMPLE_FLOAT32LE;
  asf.rate=44100;
  asf.channels=2;
  
  // TERRIBLE HACK: pulse-simple doesn't allow me to easily record from the monitor
  if (dn=="") {
    catcher=popen("pactl list | grep 'Monitor Source' | sed -r 's/^.+: //'","r");
    if (catcher!=NULL) {
      if (fgets(tDevName,4095,catcher)==NULL) {
        pclose(catcher);
      } else {
        pclose(catcher);
        
        devName=tDevName;
        devName.erase(devName.end()-1);
        logD("using device %s.\n",devName.c_str());
      }
    }
  } else {
    devName=dn;
  }
  
  ac=pa_simple_new(NULL,"darmstadt",PA_STREAM_RECORD,(devName=="")?(NULL):(devName.c_str()),"capture",&asf,NULL,NULL,&as);
  
  if (ac==NULL) {
    logW("couldn't set PulseAudio up. %s.\n",pa_strerror(as));
    return false;
  }
  return true;
}

void* pulseThreadRun(void* e) {
  return ((PulseAudioEngine*)e)->thr();
}

bool PulseAudioEngine::start() {
  pthread_create(&tid,NULL,pulseThreadRun,this);
  return true;
}

AudioPacket* PulseAudioEngine::read() {
  AudioPacket* ret=NULL;
  if (apqueue.empty()) {
    return NULL;
  } else {
    ret=apqueue.front();
    apqueue.pop();
    return ret;
  }
}

int PulseAudioEngine::sampleRate() {
  return asf.rate;
}

int PulseAudioEngine::channels() {
  return asf.channels;
}

void* PulseAudioEngine::thr() {
  while (1) {
    if (pa_simple_read(ac,ap.data,1024*asf.channels*sizeof(float),&as)<0) {
      logW("audio read error: %s!\n",pa_strerror(as));
      continue;
    }
    ap.time=curTime(CLOCK_MONOTONIC);
    apqueue.push(new AudioPacket(ap));
    if (wantBlank) {
      wantBlank=false;
      memset(ap.data,0,1024*sizeof(float)*asf.channels);
      apqueue.push(new AudioPacket(ap));
    }
  }
  return NULL;
}

const char* PulseAudioEngine::engineName() {
  return "PulseAudio";
}
