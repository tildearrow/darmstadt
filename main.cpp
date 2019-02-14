#include "darmstadt.h"

int fd, primefd;

drmModePlaneRes* planeres;
drmModePlane* plane;
drmModeFB* fb;
drmVBlank vblank;

unsigned int planeid;

int frame;
struct timespec btime, vtime;

int dw, dh;

pthread_t thr;
std::queue<qFrame> frames;

VADisplay vaInst;
VASurfaceAttribExternalBuffers vaBD;
VASurfaceID surface;
VASurfaceAttrib vaAttr[2];

int fuck, shit;

void* unbuff(void* data) {
  qFrame f;
  unsigned char* addr;
  while (1) {
    while (!frames.empty()) {
      f=frames.back();
      frames.pop();
      printf("popped frame. there are now %lu frames\n",frames.size());
      
      vaAttr[0]={   
            .type  = VASurfaceAttribMemoryType,
            .flags = VA_SURFACE_ATTRIB_SETTABLE,
            
        };
        vaAttr[0].value.type    = VAGenericValueTypeInteger;
            vaAttr[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
        vaAttr[1]={   
            .type  = VASurfaceAttribExternalBufferDescriptor,
            .flags = VA_SURFACE_ATTRIB_SETTABLE,
        };
            vaAttr[1].value.type    = VAGenericValueTypePointer;
            vaAttr[1].value.value.p = &vaBD;
            
            
      
      addr=(unsigned char*)mmap(NULL,f.objsize,PROT_READ,MAP_SHARED,f.fd,0);
      if (addr==MAP_FAILED) {
        perror("could not map this frame");
      } else {
        printf("the first pixel has red %d\n",addr[0]);
        munmap(addr,f.objsize);
      }
      
      // TODO: vaCreateSurfaces!!!
      
      close(f.fd);
    }
    usleep(10000);
  }
  return NULL;
}

int main(int argc, char** argv) {
  drmVersionPtr ver;
  // open card
  fd=open(DEVICE_PATH,O_RDWR);
  if (fd<0) {
    perror("couldn't open card");
    return 1;
  }
  ver=drmGetVersion(fd);
  if (ver==NULL) {
    printf("could not get version.\n");
    return 1;
  }
  drmFreeVersion(ver);
  printf("video card opened.\n");
  
  if (drmSetClientCap(fd,DRM_CLIENT_CAP_UNIVERSAL_PLANES,1)<0) {
    printf("i have to tell you... that universal planes can't happen...\n");
    return 1;
  }
  
  planeres=drmModeGetPlaneResources(fd);
  if (planeres==NULL) {
    printf("i have to tell you... that I am unable to get plane resources...\n");
    return 1;
  }
  
  if (planeres->count_planes<0) {
    printf("no planes. there are helicopters instead...\n");
    return 1;
  }
  
  for (unsigned int i=0; i<planeres->count_planes; i++) {
    plane=drmModeGetPlane(fd,planeres->planes[i]);
    if (plane==NULL) {
      printf("skipping plane %d...\n",planeres->planes[i]);
      continue;
    }
    
    if (plane->fb_id==0) {
      drmModeFreePlane(plane);
      plane=NULL;
      continue;
    }
    
    printf("using plane %d.\n",plane->plane_id);
    planeid=plane->plane_id;
    break;
  }
  
  if (plane==NULL) {
    printf("couldn't find any usable planes...\n");
    return 1;
  }
  
  fb=drmModeGetFB(fd,plane->fb_id);
  if (fb==NULL) {
    printf("could not get framebuffer...\n");
    return 1;
  }
  
  if (!fb->handle) {
    printf("setcap cap_sys_admin=ep\n");
    return 1;
  }
  
  dw=fb->width;
  dh=fb->height;
  
  if (planeres) drmModeFreePlaneResources(planeres);
  if (plane) drmModeFreePlane(plane);
  if (fb) drmModeFreeFB(fb);
  
  vaInst=vaGetDisplayDRM(fd);
  if (!vaDisplayIsValid(vaInst)) {
    printf("could not open VA-API...\n");
    return 1;
  }
  if (vaInitialize(vaInst,&fuck,&shit)!=VA_STATUS_SUCCESS) {
    printf("could not initialize VA-API...\n");
    return 1;
  }
  
  printf("va-api instance opened (%s).\n",vaQueryVendorString(vaInst));
  
  if (pthread_create(&thr,NULL,unbuff,NULL)<0) {
    printf("could not create encoding thread...\n");
    return 1;
  }
  
  printf("entering main loop. %dx%d\n",dw,dh);
  
  frame=0;
  
  btime=mkts(0,0);
  
  while (1) {
    vblank.request.sequence=1;
    vblank.request.type=DRM_VBLANK_RELATIVE;
    drmWaitVBlank(fd,&vblank);
    
    vtime=curTime(CLOCK_MONOTONIC)-btime;
    if (btime==mkts(0,0)) {
      btime=vtime;
      vtime=mkts(0,0);
    }
    printf("frame % 8d: %s\n",frame,tstos(vtime).c_str());
    
    plane=drmModeGetPlane(fd,planeid);
    if (plane==NULL) {
      printf("plane crash\n");
      break;
    }
    if (!plane->fb_id) {
      printf("no fb id\n");
      break;
    }
    fb=drmModeGetFB(fd,plane->fb_id);
    if (fb==NULL) {
      printf("no fb\n");
      break;
    }
    if (!fb->handle) {
      printf("no handle\n");
      break;
    }
    
    if (drmPrimeHandleToFD(fd,fb->handle,O_RDONLY,&primefd)<0) {
      printf("couldn't turn handle to FD :(\n");
      break;
    }
    printf("prime FD: %d\n",primefd);
    frames.push(qFrame(primefd,fb->height,fb->pitch,vtime));
    
    drmModeFreeFB(fb);
    drmModeFreePlane(plane);
    
    frame++;
    if (false) break;
  }
  
  printf("finished.\n");
  return 0;
}
