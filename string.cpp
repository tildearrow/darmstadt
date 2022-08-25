#include "darmstadt.h"

string strFormat(const char* format, ...) {
  va_list va;
  char* str=NULL;
  string ret;
  va_start(va,format);
#ifdef _WIN32
  str=new char[32768];
  if (vsnprintf(str,32767,format,va)<0) {
    va_end(va);
    delete[] str;
    return string("");
  }
#else
  if (vasprintf(&str,format,va)<0) {
    va_end(va);
    if (str!=NULL) {
      free(str);
    }
    return string("");
  }
#endif
  va_end(va);
  ret=str;
  if (str!=NULL) {
    free(str);
  }
  return ret;
}
