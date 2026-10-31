#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
static inline int gbc_host_mkdir(const char *p){return _mkdir(p);}
#else
static inline int gbc_host_mkdir(const char *p){return ::mkdir(p,0777);}
#endif
#ifndef GBC_HOST_SD_ROOT
#define GBC_HOST_SD_ROOT "/tmp/gbc_s3_state_sd"
#endif
struct CoverIoStats { unsigned openCalls=0,live=0,reads=0,maxRead=0; };
extern CoverIoStats coverIo;
#define FILE_READ 0
#define FILE_WRITE 1
class File {
  FILE *fp; bool dir;
public:
  File():fp(nullptr),dir(false){} explicit File(FILE *f,bool d=false):fp(f),dir(d){if(f)++coverIo.live;}
  operator bool() const { return fp != nullptr || dir; }
  bool isDirectory() const { return dir; }
  size_t size() { if(!fp)return 0; long p=ftell(fp); fseek(fp,0,SEEK_END); long n=ftell(fp); if(p>=0)fseek(fp,p,SEEK_SET); return n<0?0:(size_t)n; }
  size_t read(uint8_t *buf,size_t n) { ++coverIo.reads;if(n>coverIo.maxRead)coverIo.maxRead=(unsigned)n;return fp?fread(buf,1,n,fp):0; }
  size_t write(const uint8_t *buf,size_t n) { return fp?fwrite(buf,1,n,fp):0; }
  void flush(){if(fp)fflush(fp);} void close(){if(fp){fclose(fp);fp=nullptr;--coverIo.live;}dir=false;}
};
class SDClass {
  static void map(const char *p,char *out,size_t cap){snprintf(out,cap,"%s%s",GBC_HOST_SD_ROOT,p?p:"");}
public:
  File open(const char *p,int mode){++coverIo.openCalls;char full[512];map(p,full,sizeof(full));struct stat st{};if(stat(full,&st)==0&&S_ISDIR(st.st_mode))return File(nullptr,true);FILE*f=fopen(full,mode==FILE_WRITE?"wb":"rb");return File(f);}
  bool exists(const char*p){char full[512];map(p,full,sizeof(full));struct stat st{};return stat(full,&st)==0;}
  bool mkdir(const char*p){char full[512];map(p,full,sizeof(full));return gbc_host_mkdir(full)==0||exists(p);}
  bool remove(const char*p){char full[512];map(p,full,sizeof(full));return ::remove(full)==0||access(full,F_OK)!=0;}
  bool rename(const char*a,const char*b){char fa[512],fb[512];map(a,fa,sizeof(fa));map(b,fb,sizeof(fb));return ::rename(fa,fb)==0;}
};
extern SDClass SD;
