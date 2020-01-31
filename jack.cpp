#include "darmstadt.h"

int audio(jack_nframes_t count, void* arg) {
  return ((JACKAudioEngine*)arg)->process(count);
}

int JACKAudioEngine::process(jack_nframes_t count) {
  float* in[8];
  
  for (int i=0; i<chan; i++) {
    in[i]=(float*)jack_port_get_buffer(ai[i],count);
  }
  
  for (size_t i=0; i<count; i++) {
    for (int j=0; j<chan; j++) ap.data[collPos*chan+j]=in[j][i];
    collPos++;
    if (collPos>=1024) {
      // upload this buffer
      collPos=0;
      ap.time=curTime(CLOCK_MONOTONIC);
      apqueue.push(new AudioPacket(ap));
    }
  }
  
  return 0;
}

bool JACKAudioEngine::init(string dn) {
  size_t splitPos;
  string portName;
  chan=0;
  collPos=0;  
  ac=jack_client_open("darmstadt",JackNoStartServer,&as,NULL);
  if (ac==NULL) {
    logW("JACK server not running or something.\n");
    return false;
  }
  name=jack_get_client_name(ac);
  jack_set_process_callback(ac,audio,this);
  
  // select monitor if no device specified
  devName=dn;
  if (devName=="") {
    devName="system:monitor_1|system:monitor_2";
  }

  // create ports
  splitPos=0;
  while ((splitPos=devName.find('|',splitPos))!=string::npos) {
    if (chan>=8) {
      logW("darmstadt only records a maximum of 8 audio channels.\n");
      break;
    }
    portName=strFormat("in_%d",chan);
    ai[chan++]=jack_port_register(ac,portName.c_str(),JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
    splitPos++;
  }
  
  portName=strFormat("in_%d",chan);
  ai[chan++]=jack_port_register(ac,portName.c_str(),JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);

  return true;
}

bool JACKAudioEngine::start() {
  string portName, inPortName;
  size_t splitPos, initPos;
  int curChan;
  jack_activate(ac);

  // connect ports
  splitPos=0; initPos=0; curChan=0;
  while ((splitPos=devName.find('|',splitPos))!=string::npos) {
    portName=strFormat("in_%d",curChan++);
    inPortName=devName.substr(initPos,splitPos-initPos);
    logD("connect %s to %s\n",inPortName.c_str(),portName.c_str());
    jack_connect(ac,inPortName.c_str(),(name+":"+portName).c_str());
    splitPos++;
    initPos=splitPos;
  }
  
  splitPos=devName.size();
  portName=strFormat("in_%d",curChan++);
  inPortName=devName.substr(initPos,splitPos-initPos);
  logD("connect %s to %s\n",inPortName.c_str(),portName.c_str());
  jack_connect(ac,inPortName.c_str(),(name+":"+portName).c_str());

  return true;
}

AudioPacket* JACKAudioEngine::read() {
  AudioPacket* ret=NULL;
  if (apqueue.empty()) {
    return NULL;
  } else {
    ret=apqueue.front();
    apqueue.pop();
    return ret;
  }
}

int JACKAudioEngine::sampleRate() {
  return jack_get_sample_rate(ac);
}

int JACKAudioEngine::channels() {
  return chan;
}

const char* JACKAudioEngine::engineName() {
  return "JACK";
}
