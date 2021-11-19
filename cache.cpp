#include "darmstadt.h"

void* WriteCache::run() {
  CacheCommand cc;
  do {
    usleep(50000);
    while (!cqueue.empty()) {
      busy=true;
      cc=cqueue.front();
      cqueue.pop();
      switch (cc.cmd) {
        case cWrite:
          if (::write(o,cc.buf,cc.size)<0) {
            logE("error while writing! %s\n",strerror(errno));
          }
          delete[] cc.buf;
          break;
        case cSeek:
          if (lseek(o,cc.size,cc.whence)<0) {
            logE("error while SEEKING! %s\n",strerror(errno));
          }
          break;
        case cRead:
          logE("hey! you try'na read!\n");
          abort();
          break;
      }
    }
    busy=false;
  } while (!shallStop);
  return NULL;
}

int WriteCache::write(unsigned char* buf, size_t len) {
  unsigned char* nbuf;
  if (!running) {
    return ::write(o,buf,len);
  }

  // i hope memory allocs are fast
  nbuf=new unsigned char[len];
  memcpy(nbuf,buf,len);
  cqueue.push(CacheCommand(cWrite,nbuf,len,0));
  return len;
}

int WriteCache::seek(ssize_t pos, int whence) {
  if (!running) {
    return lseek(o,pos,whence);
  }
  cqueue.push(CacheCommand(cSeek,NULL,pos,whence));
  return 0;
}

bool WriteCache::flush() {
  if (!running) {
    return true;
  }
  while (busy) {
    usleep(1000);
  }
  return true;
}

void WriteCache::setFile(int f) {
  o=f;
}

void* wcRun(void* inst) {
  return ((WriteCache*)inst)->run();
}

bool WriteCache::enable() {
  if (!running) {
    shallStop=false;
    running=true;
    // pthread
    pthread_create(&tid,NULL,wcRun,this);
    return true;
  }
  return false;
}

bool WriteCache::disable() {
  void* unused;
  if (running) {
    shallStop=true;
    pthread_join(tid,&unused);
    running=false;
    return true;
  }
  return false;
}

int WriteCache::queueSize() {
  return cqueue.size();
}

WriteCache::WriteCache(): o(-1), running(false), shallStop(false), busy(false), tid(-1) {
}
