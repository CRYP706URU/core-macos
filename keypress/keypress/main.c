//
//  main.c
//  keypress
//
//  Created by armored on 20/03/14.
//  Copyright (c) 2014 -. All rights reserved.
//
#include "unpacker.h"
#include "unpacker_addr.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach/i386/_structs.h>
#include <mach/i386/thread_status.h>
#else
#include "macho.h"
#endif

// simmetric functions used by unpacker
#include "common.h"
#include "cypher.h"
#include "integrity.h"
#include "text_sc_enc.h"
#include "dynamic_enc.h"
//

#define LC_SEG_TYPE_TEXT    1
#define LC_SEG_TYPE_DATA    2
#define LC_CMDS_NUM         3
#define LC_TEXT_VMADDR      0x1800000
#define UNPACKER_IMAGE_BASE 0x12000
#define HEADER_PAD_LEN      0x3000

void usage()
{
  printf("usage:\n\tkpress <input file> <output file>\n");
  exit(0);
}

struct mach_header* setup_macho_header()
{
  static struct mach_header mh_header;
  
  mh_header.magic       = MH_MAGIC;
  mh_header.cputype     = CPU_TYPE_I386;
  mh_header.cpusubtype  = CPU_SUBTYPE_X86_ALL;
  mh_header.filetype    = MH_EXECUTE;
  mh_header.ncmds       = LC_CMDS_NUM;
  mh_header.sizeofcmds  = 0;  // to adjust
  mh_header.flags       = MH_NOUNDEFS;
  
  return &mh_header;
}

struct segment_command* setup_lc_seg_data_header()
{
  static struct segment_command mh_segm;
  
  mh_segm.cmd       = LC_SEGMENT;
  mh_segm.cmdsize   = sizeof(struct segment_command);
  strcpy(mh_segm.segname, "__DATA");
  mh_segm.vmaddr    = 0;  // to adjust;
  mh_segm.vmsize    = 0;  // to adjust
  mh_segm.fileoff   = 0;
  mh_segm.filesize  = 0;  // to adjust
  mh_segm.maxprot   = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  mh_segm.initprot  = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  mh_segm.nsects    = 0;
  mh_segm.flags     = 0;
  
  return &mh_segm;
}

struct segment_command* setup_lc_seg_text_header()
{
  static struct segment_command mh_segm;
  
  mh_segm.cmd       = LC_SEGMENT;
  mh_segm.cmdsize   = sizeof(struct segment_command);
  strcpy(mh_segm.segname, "__TEXT");
  mh_segm.vmaddr    = LC_TEXT_VMADDR;
  mh_segm.vmsize    = 0;  // to adjust
  mh_segm.fileoff   = 0;
  mh_segm.filesize  = 0;  // to adjust
  mh_segm.maxprot   = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  mh_segm.initprot  = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  mh_segm.nsects    = 1;
  mh_segm.flags     = 0;
  
  return &mh_segm;
}

struct section* setup_lc_sec_text_header()
{
  static struct section mh_sec;

  strcpy(mh_sec.segname,  "__TEXT");
  strcpy(mh_sec.sectname, "__text");
  mh_sec.addr      = 0; // to adjust
  mh_sec.size      = 0; // to adjust
  mh_sec.offset    = 0; // to adjust
  mh_sec.align     = 4;
  mh_sec.reloff    = 0;
  mh_sec.nreloc    = 0;
  mh_sec.reserved1 = 0;
  mh_sec.reserved2 = 0;
  mh_sec.flags     = 0x80000400;
  
  return &mh_sec;
}

x86_thread_state32_t *
setup_lc_xthd_header(__int32_t addr)
{
  static x86_thread_state32_t thcmd;
  
  memset(&thcmd, 0, sizeof(thcmd));

  thcmd.__eip = addr;
  
  return &thcmd;
}

#define MAX_LEN 8192
#define MIN_LEN 1024

unsigned char* rand_buff(int *len)
{
  unsigned char* buff, *ptr;
  
  srand(time(NULL));
  
  int buff_len = rand()%(MAX_LEN-MIN_LEN)+MIN_LEN;
  
  ptr = buff = (unsigned char*)malloc(buff_len);
  
  uint32_t buff_int = rand();
  
  for (int i=0; i<buff_len; i+=4)
  {
    buff_int = rand();
    memcpy(ptr, &buff_int, 4);
    ptr+=4;
  }
  
  *len = buff_len;
  
  return buff;
}

void setup_unpacker_param(in_param* out_param, int payload_len, char* _unpacker_buff, int _unpacker_len)
{
  out_param->hash = calc_integrity(_unpacker_buff, _unpacker_len);
  out_param->check_integrity_offset  = _ENDCALL_ADDR - _CHECK_INTEGRITY_ADDR;
  out_param->strlen_offset           = _ENDCALL_ADDR - _STRLEN_ADDR;
  out_param->mh_mmap_offset          = _ENDCALL_ADDR - _DMH_MMAP_V1_ADDR; // using dmh_mmap_v1
  out_param->crypt_payload_offset          = _ENDCALL_ADDR - _CRPYT_PAYLOAD_ADDR;
  out_param->open_and_resolve_dyld_offset  = _ENDCALL_ADDR  - _OPEN_AND_RESOLVE_ADDR;
  out_param->sys_mmap_offset               = _ENDCALL_ADDR  - _SYS_MMAP_ADDR;
  out_param->sigtramp_offset               = 0x00A1FFFF; /* _ENDCALL_ADDR  - _SIGTRAMP */
  out_param->BEGIN_ENC_TEXT_offset         = _ENDCALL_ADDR  - _BEGIN_ENC_TEXT_ADDR;
  out_param->END_ENC_TEXT_offset           = _ENDCALL_ADDR  - _END_ENC_TEXT_ADDR;
  out_param->macho_len                     = payload_len;
  memcpy(out_param->crKey, gKey, gKey_len);
}

char* open_payload(char* file_in, int *payload_len)
{
  struct stat stat_in;
  
  FILE *fd_in   = fopen(file_in,  "rb");
  
  stat(file_in, &stat_in);
  
  char *buff_in = (char*)malloc((size_t)stat_in.st_size);
  
  
  
  while(*payload_len < stat_in.st_size)
  {
    int rbyte = fread(buff_in + *payload_len, 1,(size_t)stat_in.st_size - *payload_len, fd_in );
    *payload_len += rbyte;
    if (rbyte == 0)
      break;
  }
  
  fclose(fd_in);
  
  if (*payload_len != stat_in.st_size)
  {
    printf("\treading error\n");
    return NULL;
  }
  
  return buff_in;
}

void encrypt_dynamic_func(char* _unpacker_buff)
{
  // obfuscate sysenter call of _dmh_mmap_v1
  uint32_t  d_obf_sys_map_off   = _SYS_MMAP_ADDR - _MAIN_ADDR;
  uint32_t* d_obf_sys_map_addr  = (uint32_t*)(_unpacker_buff + d_obf_sys_map_off);
  *d_obf_sys_map_addr = 0x8Bc4458B;
  
  // obfuscate sysenter call of _dmh_mmap_v2
//  #define _SYS_MMAP_V2_BC_OFF 0xe  
//  d_obf_sys_map_off   = _SYS_MMAP_V2 - _MAIN_ADDR;
//  d_obf_sys_map_addr  = (uint32_t*)(_unpacker_buff+d_obf_sys_map_off + _SYS_MMAP_V2_BC_OFF);
//  *d_obf_sys_map_addr = 0x8Bc4458B;
  
  // intial encryption of: _dmh_mmap_v1
  uint32_t d_enc_begin = _DMH_MMAP_ENC_V1_ADDR - _MAIN_ADDR;
  uint32_t d_enc_end   = _DMH_MMAP_END_V1_ADDR - _MAIN_ADDR;
  DYNAMIC_ENC(_unpacker_buff + d_enc_end, _unpacker_buff + d_enc_begin);
  
  // intial encryption of: _dmh_open_v1
  d_enc_begin = _DMH_OPEN_ENC_V1_ADDR - _MAIN_ADDR;
  d_enc_end   = _DMH_OPEN_END_V1_ADDR - _MAIN_ADDR;
  DYNAMIC_ENC(_unpacker_buff + d_enc_end, _unpacker_buff + d_enc_begin);
}

void encrypt_unpacker_func(char* _unpacker_buff)
{
  int begin_enc_off = _BEGIN_ENC_TEXT_ADDR - _MAIN_ADDR;
  int enc_len       = _END_ENC_TEXT_ADDR   - _BEGIN_ENC_TEXT_ADDR;
  enc_unpacker_text_section(_unpacker_buff + begin_enc_off, enc_len);
}

void save_packed_macho(void* buff, int len, FILE* fd_out)
{
  fwrite(buff, 1, len, fd_out);
}

int main(int argc, const char * argv[])
{
  int payload_len = 0;
  in_param  out_param;
  struct section*         sc;
  struct load_command     lc;
  struct mach_header*     mh = NULL;
  struct segment_command* st = NULL;
  struct segment_command* sd = NULL;
  x86_thread_state32_t*   th = NULL;
  char *_unpacker_buff       = (char*)_tmp_unpacker_buff;
  int   _unpacker_len        = _tmp_unpacker_buff_len;

  if(argc < 3)
    usage();
  
  char* file_in  = (char*)argv[1];
  char* file_out = (char*)argv[2];
  
  FILE *fd_out  = fopen((const char*)file_out, "wb");
  
  if (fd_out == 0)
  {
    printf("\terror opening output file\n");
    return -1;
  }
  
  printf("\tinput file is %s output file is %s\n", file_in, file_out);
  
  char *payload_buff = open_payload(file_in, &payload_len);
    
  if (payload_buff == NULL)
  {
    printf("\terror opening input file\n");
    return -2;
  }
  
  printf("\treading %d bytes from %s\n\ttry to encrypt payload...\n",
         payload_len, file_in);
  
  CRYPT_PAYLOAD((uint8_t*)payload_buff, (uint8_t*)payload_buff, payload_len, gKey);

  
#ifndef _WIN32
  sleep(1);
#endif
  
  printf("\tpacking...\n");
  
  /////////////////////////////////////////////
  // _text section len = unpacker code +
  //                     in_param + macho paylod
    
  int __text_len = _unpacker_len + sizeof(in_param) + payload_len;
  
  /////////////////////////////////////////////
  // setup load command
  
  mh = setup_macho_header();
  st = setup_lc_seg_text_header();
  sd = setup_lc_seg_data_header();
  th = setup_lc_xthd_header(LC_TEXT_VMADDR);
  sc = setup_lc_sec_text_header();
  
  lc.cmd      = LC_UNIXTHREAD;
  lc.cmdsize  = sizeof(x86_thread_state32_t) + sizeof(int) + sizeof(int) + sizeof(lc);
  int flavor  = x86_THREAD_STATE32;
  int count   = 16;
  
  /////////////////////////////////////////////
  // adjust param
  
  mh->sizeofcmds = sizeof(struct segment_command) +   /* __TEXT segment */
                   sizeof(struct section)         +   /* __text section */
                   sizeof(struct segment_command) +   /* __DATA segment */
                   sizeof(lc) +                       /* lc struct      */
                   sizeof(flavor) +                   /* flavor for LC_X */
                   sizeof(count) +                    /* count  for LC_X */
                   sizeof(x86_thread_state32_t);      /* LC_X Thread     */

  th->__eip = LC_TEXT_VMADDR + mh->sizeofcmds + sizeof(struct mach_header) + HEADER_PAD_LEN;
  
  st->cmdsize += sizeof(struct section);
  
  int vmsize_len = __text_len + mh->sizeofcmds + sizeof(struct mach_header);
  int vmsize_pad_len = ((vmsize_len/4096)+1)*4096 - vmsize_len;
  st->vmsize = st->filesize = vmsize_len + vmsize_pad_len + HEADER_PAD_LEN;
 
  int sd_len;
  unsigned char* sd_buff = rand_buff(&sd_len);
  
  sd->vmsize = sd->filesize = sd_len;
  sd->vmaddr = st->vmaddr + st->vmsize + 0x1000 + HEADER_PAD_LEN;
  
  sc->addr    = th->__eip;
  sc->size    = _unpacker_len + sizeof(in_param);
  sc->offset  = mh->sizeofcmds + sizeof(struct mach_header) + HEADER_PAD_LEN;
  
  /////////////////////////////////////////////
  // Save Load Command section

  save_packed_macho(mh,  sizeof(struct mach_header), fd_out);
  save_packed_macho(st,  sizeof(struct segment_command), fd_out);
  save_packed_macho(sc,  sizeof(struct section), fd_out);
  save_packed_macho(sd,  sizeof(struct segment_command), fd_out);
  save_packed_macho(&lc, sizeof(lc), fd_out);
  
  save_packed_macho(&flavor, sizeof(int), fd_out);
  save_packed_macho(&count,  sizeof(int), fd_out);
  save_packed_macho(th,      sizeof(x86_thread_state32_t), fd_out);
  
  char *headerpad = (char*)calloc(HEADER_PAD_LEN, 1);
  save_packed_macho(headerpad, HEADER_PAD_LEN, fd_out);
  
  /////////////////////////////////////////////
  // encrypt and save unpacker text section

  encrypt_dynamic_func(_unpacker_buff);
  encrypt_unpacker_func(_unpacker_buff);
  save_packed_macho(_unpacker_buff, _unpacker_len, fd_out);
  
  /////////////////////////////////////////////
  // save unpacker input param
  
  setup_unpacker_param(&out_param, payload_len, _unpacker_buff, _unpacker_len);
  save_packed_macho(&out_param, sizeof(in_param) - sizeof(uint32_t), fd_out);
  
  /////////////////////////////////////////////
  // save enc payload 
  
  save_packed_macho(payload_buff, payload_len, fd_out);
  
  char *vmsize_pad = (char*)calloc(vmsize_pad_len, 1);
  save_packed_macho(vmsize_pad, vmsize_pad_len, fd_out);
  
  /////////////////////////////////////////////
  // save bogus data section
  
  save_packed_macho(sd_buff, sd_len, fd_out);

  /////////////////////////////////////////////
  // finalize
  
  fclose(fd_out);

  chmod(file_out, (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH));
  
  printf("\tdone.\n");
  
  return 0;
}

