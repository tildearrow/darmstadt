#include "darmstadt.h"

void* WriteCache::run() {
  CacheCommand cc;
  do {
    usleep(50000);
    while (true) {
      m.lock();
      if (cqueue.empty()) {
        m.unlock();
        break;
      }
      busy=true;
      cc=cqueue.front();
      cqueue.pop_front();
      m.unlock();
      switch (cc.cmd) {
        case cWrite:
          if (::write(o,cc.buf,cc.size)<0) {
            logE("error while writing! %s\n",strerror(errno));
          }
          if (cc.whence==1) {
            m.lock();
            ringBufPosR+=cc.size;
            if (ringBufPosR>=ringBufSize) ringBufPosR-=ringBufSize;
            m.unlock();
          } else {
            delete[] cc.buf;
          }
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

  // **NEW RING BUFFER CODE**
  m.lock();
  ssize_t remaining=ringBufPosR-ringBufPosW-1;
  m.unlock();
  if (remaining<=0) remaining+=ringBufSize;
  if (remaining<len) {
    // worst case: just allocate memory
    printf("not enough space in the ringbuffer! allocating memory...\n");
    nbuf=new unsigned char[len];
    memcpy(nbuf,buf,len);
    m.lock();
    cqueue.push_back(CacheCommand(cWrite,nbuf,len,0));
    m.unlock();
  } else {
    ssize_t boundRemain=ringBufSize-ringBufPosW;
    if (boundRemain<len) {
      // write two times
      unsigned char* rbAddr=ringBuf+ringBufPosW;
      memcpy(rbAddr,buf,boundRemain);
      memcpy(ringBuf,buf+boundRemain,len-boundRemain);
      m.lock();
      cqueue.push_back(CacheCommand(cWrite,rbAddr,boundRemain,1));
      cqueue.push_back(CacheCommand(cWrite,ringBuf,len-boundRemain,1));
      m.unlock();
    } else {
      // write once
      unsigned char* rbAddr=ringBuf+ringBufPosW;
      memcpy(rbAddr,buf,len);
      m.lock();
      cqueue.push_back(CacheCommand(cWrite,rbAddr,len,1));
      m.unlock();
    }
    ringBufPosW+=len;
    if (ringBufPosW>=ringBufSize) ringBufPosW-=ringBufSize;
  }

  return len;
}

int WriteCache::seek(ssize_t pos, int whence) {
  if (!running) {
    return lseek(o,pos,whence);
  }
  m.lock();
  cqueue.push_back(CacheCommand(cSeek,NULL,pos,whence));
  m.unlock();
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

bool WriteCache::enable(size_t ringSize) {
  if (ringBuf==NULL) {
    ringBufSize=ringSize;
    ringBuf=new unsigned char[ringBufSize];
    // clean the ring buffer and force it to be allocated
    memset(ringBuf,0,ringBufSize);
  }
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

size_t WriteCache::queueSize() {
  size_t ret=0;
  m.lock();
  for (CacheCommand& i: cqueue) {
    if (i.cmd==cWrite) ret+=i.size;
  }
  m.unlock();
  return ret;
}

WriteCache::WriteCache(): o(-1), running(false), shallStop(false), busy(false), tid(-1), ringBuf(NULL), ringBufSize(DARM_RINGBUF_SIZE), ringBufPosR(0), ringBufPosW(0) {
}
