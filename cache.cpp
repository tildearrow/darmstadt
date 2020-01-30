#include "darmstadt.h"

void* WriteCache::run() {
  CacheCommand cc;
  do {
    usleep(50000);
    while (!cqueue.empty()) {
      cc=cqueue.back();
      cqueue.pop();
      switch (cc.cmd) {
        case cWrite:
          fwrite(cc.buf,1,cc.size,o);
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
  } while (!shallStop);
  return NULL;
}

int WriteCache::write(void* buf, size_t len) {
  char* nbuf;
  if (!running) {
    return fwrite(buf,1,len,o);
  }

  // i hope memory allocs are fast
  nbuf=new char[len];
  memcpy(nbuf,buf,len);
  cqueue.push(CacheCommand(cWrite,nbuf,len,0));
  return len;
}

int WriteCache::seek(int pos, int whence) {
  if (!running) {
    return fseek(o,pos,whence);
  }
  cqueue.push(CacheCommand(cSeek,NULL,pos,whence));
  return pos;
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
  if (running) {
    shallStop=true;
    flush();
    running=false;
    return true;
  }
  return false;
}
