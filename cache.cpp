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
          fwrite(cc.buf,1,cc.size,o);
          delete[] cc.buf;
          break;
        case cSeek:
          fseek(o,cc.size,cc.whence);
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
    return fwrite(buf,1,len,o);
  }

  // i hope memory allocs are fast
  nbuf=new unsigned char[len];
  memcpy(nbuf,buf,len);
  cqueue.push(CacheCommand(cWrite,nbuf,len,0));
  return len;
}

int WriteCache::seek(ssize_t pos, int whence) {
  if (!running) {
    return fseek(o,pos,whence);
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

void WriteCache::setFile(FILE* f) {
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

WriteCache::WriteCache(): o(NULL), running(false), shallStop(false), busy(false), tid(-1) {
}
