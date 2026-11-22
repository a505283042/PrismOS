/* SPDX-License-Identifier: MIT
 * Aster HiFi HIF2 playlist reader, firmware-owned global playback.
 * No decoder/I2S/Bluetooth or APP per-category state lives here.
 *
 * The APP publishes a checked HIF2 disk snapshot with:
 * 36 byte header; 160 byte records (u64 filesize + UTF-8 relative path[152]);
 * payload CRC32 and header CRC32; two alternating generation slots.
 *
 * Only read/verify the catalog when music has stopped, or when a track ended.
 * Never retain an SD file handle across USB MSC ownership transitions.
 */
#include "hifi_playlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#ifndef HIFI_PLAYLIST_HOST_TEST
#include "aster_power.h"
#include "aster_os.h"
#else
#include <sys/stat.h>
/* Host-only executable uses an isolated temporary SD fixture. */
static int aster_power_activity_begin(void) { return 0; }
static void aster_power_activity_end(void) { }
#endif
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#define HIFI_WARN(...) ESP_LOGW("hifi_playlist", __VA_ARGS__)
#else
#define HIFI_WARN(...) ((void)0)
#endif

/* Use the same mounted SD filesystem adapter as APP storage and the media
 * decoder. The ESP-IDF POSIX /SD path is not a reliable alias for the
 * Zephyr-compatible /SD: mount on all supported IDF builds. */
#ifdef HIFI_PLAYLIST_HOST_TEST
typedef FILE *hifi_file_t;
static bool hfile_open(hifi_file_t *f,const char *p) { *f=fopen(p,"rb"); return *f!=NULL; }
static void hfile_close(hifi_file_t *f) { if(*f)fclose(*f); *f=NULL; }
static bool hfile_read(hifi_file_t *f,void *p,size_t n) { return fread(p,1,n,*f)==n; }
static bool hfile_seek(hifi_file_t *f,uint32_t pos) { return fseek(*f,(long)pos,SEEK_SET)==0; }
static bool hfile_size(const char *p,uint64_t *n) {
    struct stat st;
    if(stat(p,&st) || !S_ISREG(st.st_mode))return false;
    *n=(uint64_t)st.st_size;return true;
}
static bool hfile_regular(const char *p) { uint64_t n; return hfile_size(p,&n); }
#else
typedef aster_os_file_t hifi_file_t;
static bool hfile_open(hifi_file_t *f,const char *p) {
    return aster_os_file_open(f,p,ASTER_OS_READ)==0;
}
static void hfile_close(hifi_file_t *f) {
    if(f->handle)(void)aster_os_file_close(f);
}
static bool hfile_read(hifi_file_t *f,void *p,size_t n) {
    size_t done=0;
    while(done<n) {
        int got=aster_os_file_read(f,(uint8_t *)p+done,n-done);
        if(got<=0)return false;
        done+=(size_t)got;
    }
    return true;
}
static bool hfile_seek(hifi_file_t *f,uint32_t pos) {
    return aster_os_file_seek(f,(int64_t)pos,ASTER_OS_SEEK_SET)==0;
}
static bool hfile_size(const char *p,uint64_t *n) {
    aster_os_entry_t st={0};
    if(aster_os_stat(p,&st) || st.type!=ASTER_OS_FILE)return false;
    *n=st.size;return true;
}
static bool hfile_regular(const char *p) { uint64_t n; return hfile_size(p,&n); }
#endif
#if defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define HIFI_QUEUE_PSRAM EXT_RAM_BSS_ATTR
#else
#define HIFI_QUEUE_PSRAM
#endif

enum { HIF_MAGIC = 0x32464948u, HIF_VERSION = 2, HIF_MAX = 16384 };
#ifdef HIFI_PLAYLIST_HOST_TEST
static const char *const catalog_paths[2] = {
    "/SD/aster/apps/aster.hifi/data/hifi_catalog_a.bin",
    "/SD/aster/apps/aster.hifi/data/hifi_catalog_b.bin"
};
#else
static const char *const catalog_paths[2] = {
    "/SD:/aster/apps/aster.hifi/data/hifi_catalog_a.bin",
    "/SD:/aster/apps/aster.hifi/data/hifi_catalog_b.bin"
};
#endif
typedef struct {
    uint32_t magic, version, generation, count;
    uint32_t payload_crc, signature_xor, signature_sum, flags;
    uint32_t header_crc;
} hif_header_t;
typedef struct {
    uint64_t filesize;
    char path[152];
} hif_row_t;
_Static_assert(sizeof(hif_header_t) == 36, "HIF2 disk header drift");
_Static_assert(sizeof(hif_row_t) == 160, "HIF2 disk row drift");

static HIFI_QUEUE_PSRAM uint16_t shuffled[HIF_MAX];
static uint32_t shuffle_count, shuffle_generation;
static uint32_t shuffle_cursor, prng_state = 0x35793ae1u;
static bool shuffle_started;
static uint32_t verified_generation, verified_crc, verified_count;
static int verified_slot = -1;
static int last_index = -1;
static char last_path[ASTER_MEDIA_PATH_MAX];

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i=0; i<n; i++) {
        crc ^= p[i];
        for (unsigned k=0;k<8;k++)
            crc = (crc>>1) ^ (0xedb88320u & (0u - (crc&1u)));
    }
    return crc;
}
static uint32_t next_random(void)
{
    uint32_t x = prng_state ? prng_state : 0x35793ae1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}
void hifi_playlist_seed(uint32_t entropy)
{
    prng_state ^= entropy;
    (void)next_random();
}
void hifi_playlist_reset_shuffle(void)
{
    shuffle_started=false;
    shuffle_count=shuffle_cursor=shuffle_generation=0;
}
static bool valid_relative(const char *name)
{
    if (!name || !name[0] || name[0]=='/' || name[0]=='\\')
        return false;
    if (strchr(name, ':') || strchr(name, '\\'))return false;
    size_t n=strlen(name);
    if (!strcmp(name,"..") || !strncmp(name,"../",3) ||
        strstr(name,"/../") || (n>=3 && !strcmp(name+n-3,"/..")))
        return false;
    return true;
}
static bool read_header(hifi_file_t *file,const char *path,hif_header_t *head)
{
    uint64_t bytes=0;
    if(!hfile_size(path,&bytes) || !hfile_read(file,head,sizeof(*head)))return false;
    uint32_t crc=~crc_update(0xffffffffu,(const uint8_t *)head,
                             offsetof(hif_header_t,header_crc));
    return head->magic==HIF_MAGIC && head->version==HIF_VERSION &&
           head->count>0 && head->count<=HIF_MAX &&
           head->header_crc==crc &&
           bytes==sizeof(*head)+(uint64_t)head->count*sizeof(hif_row_t);
}
static bool verify_payload(hifi_file_t *file,const hif_header_t *head)
{
    if(!hfile_seek(file,sizeof(*head)))return false;
    uint8_t block[4096];
    uint32_t remaining=head->count*(uint32_t)sizeof(hif_row_t);
    uint32_t crc=0xffffffffu;
    while(remaining) {
        size_t n=remaining<sizeof(block)?remaining:sizeof(block);
        if(!hfile_read(file,block,n))return false;
        crc=crc_update(crc,block,n);
        remaining-=(uint32_t)n;
    }
    return (~crc)==head->payload_crc;
}
static bool open_current(hif_header_t *head,hifi_file_t *opened)
{
    hifi_file_t files[2]={0};
    hif_header_t headers[2]={{0},{0}};
    bool valid[2]={false,false};
    for(int i=0;i<2;i++) {
        if(!hfile_open(&files[i],catalog_paths[i]))continue;
        if(!read_header(&files[i],catalog_paths[i],&headers[i])) {
            hfile_close(&files[i]);
            HIFI_WARN("HIF2 slot %d invalid header",i);
            continue;
        }
        valid[i]=true;
    }
    int first=valid[0] && valid[1] ?
              ((int32_t)(headers[0].generation-headers[1].generation)>=0 ? 0:1) :
              (valid[0]?0:1);
    for(int attempt=0;attempt<2;attempt++) {
        int slot=attempt==0?first:1-first;
        if(!valid[slot])continue;
        const hif_header_t *h=&headers[slot];
        bool known=verified_slot==slot &&
                   verified_generation==h->generation &&
                   verified_count==h->count &&
                   verified_crc==h->payload_crc;
        if(!known && !verify_payload(&files[slot],h)) {
            HIFI_WARN("HIF2 slot %d payload CRC/read failed",slot);
            hfile_close(&files[slot]);
            continue;
        }
        if(!known) {
            verified_slot=slot;
            verified_generation=h->generation;
            verified_count=h->count;
            verified_crc=h->payload_crc;
            last_index=-1;
            last_path[0]=0;
            hifi_playlist_reset_shuffle();
        }
        *head=*h;
        *opened=files[slot];
        memset(&files[slot],0,sizeof(files[slot]));
        hfile_close(&files[1-slot]);
        return true;
    }
    HIFI_WARN("no usable HIF2 catalog at /SD:/aster/apps/aster.hifi/data");
    return false;
}
static bool row_at(hifi_file_t *file,uint32_t i,hif_row_t *out)
{
    uint32_t offset=(uint32_t)sizeof(hif_header_t)+i*(uint32_t)sizeof(hif_row_t);
    if(!hfile_seek(file,offset) || !hfile_read(file,out,sizeof(*out)))return false;
    return memchr(out->path,0,sizeof(out->path)) && valid_relative(out->path);
}
static const char *relative_from_music(const char *absolute)
{
    static const char a[]="/SD:/music/";
    static const char b[]="/SD/music/";
    if(!strncmp(absolute,a,sizeof(a)-1))return absolute+sizeof(a)-1;
    if(!strncmp(absolute,b,sizeof(b)-1))return absolute+sizeof(b)-1;
    return NULL;
}
static int locate(hifi_file_t *file, uint32_t count, const char *absolute)
{
    if(last_index>=0 && (uint32_t)last_index<count &&
       !strcmp(last_path,absolute))
        return last_index;
    const char *relative=relative_from_music(absolute);
    if(!relative)return -1;
    hif_row_t row;
    for(uint32_t i=0;i<count;i++){
        if(!row_at(file,i,&row))return -1;
        if(!strcmp(relative,row.path)){
            last_index=(int)i;
            snprintf(last_path,sizeof(last_path),"%s",absolute);
            return (int)i;
        }
    }
    return -1;
}
static bool make_path(hifi_file_t *file,uint32_t index,char out[ASTER_MEDIA_PATH_MAX])
{
    hif_row_t row;
    if(!row_at(file,index,&row))return false;
    /* Both the native decoder and the native SD stat adapter use /SD:.
     * Keep /SD as a host-test-only alias for the isolated POSIX fixture. */
    int size=snprintf(out,ASTER_MEDIA_PATH_MAX,"/SD:/music/%s",row.path);
    char native[ASTER_MEDIA_PATH_MAX];
#ifdef HIFI_PLAYLIST_HOST_TEST
    int n=snprintf(native,sizeof(native),"/SD/music/%s",row.path);
#else
    int n=snprintf(native,sizeof(native),"/SD:/music/%s",row.path);
#endif
    if(size<=0 || (unsigned)size>=ASTER_MEDIA_PATH_MAX ||
       n<=0 || (size_t)n>=sizeof(native))return false;
    return hfile_regular(native);
}
static void start_shuffle(uint32_t count,uint32_t generation,uint32_t current)
{
    shuffle_count=count;
    shuffle_generation=generation;
    for(uint32_t i=0;i<count;i++)shuffled[i]=(uint16_t)i;
    for(uint32_t i=count;i>1;i--){
        uint32_t j=next_random()%i;
        uint16_t tmp=shuffled[i-1];
        shuffled[i-1]=shuffled[j];
        shuffled[j]=tmp;
    }
    for(uint32_t i=0;i<count;i++)if(shuffled[i]==current){
        uint16_t tmp=shuffled[0];
        shuffled[0]=shuffled[i];
        shuffled[i]=tmp;
        break;
    }
    shuffle_cursor=0;
    shuffle_started=true;
}
static bool choose_with_storage_activity(const char *current,uint32_t mode,
                                         int direction,bool automatic,
                                         char out[ASTER_MEDIA_PATH_MAX])
{
    if(!current || !out || (direction!=1 && direction!=-1) ||
       mode>HIFI_QUEUE_SHUFFLE)return false;
    out[0]=0;
    hif_header_t header={0};
    hifi_file_t file={0};
    if(!open_current(&header,&file))return false;
    int idx=locate(&file,header.count,current);
    if(idx<0) {
        HIFI_WARN("current track not in HIF2: %s",current);
        hfile_close(&file);
        return false;
    }
    if(automatic && mode==HIFI_QUEUE_REPEAT_ONE){
        bool yes=make_path(&file,(uint32_t)idx,out);
        hfile_close(&file);
        return yes;
    }
    if(mode==HIFI_QUEUE_SHUFFLE) {
        if(!shuffle_started || shuffle_count!=header.count ||
           shuffle_generation!=header.generation ||
           shuffled[shuffle_cursor]!=(uint16_t)idx)
            start_shuffle(header.count,header.generation,(uint32_t)idx);
        /* Back uses the existing visited order, not a new random song. */
        if(direction<0 && shuffle_cursor==0) {
            hfile_close(&file);return false;
        }
        if(direction>0 && shuffle_cursor+1>=shuffle_count)
            start_shuffle(header.count,header.generation,(uint32_t)idx);
        for(uint32_t attempt=0;attempt<header.count;attempt++){
            if(direction>0) {
                if(shuffle_cursor+1>=shuffle_count)break;
                shuffle_cursor++;
            } else shuffle_cursor--;
            uint32_t candidate=shuffled[shuffle_cursor];
            if(make_path(&file,candidate,out)){
                last_index=(int)candidate;
                snprintf(last_path,sizeof(last_path),"%s",out);
                hfile_close(&file);return true;
            }
            if(direction<0 && !shuffle_cursor)break;
        }
        hfile_close(&file);return false;
    }
    /* Single-track repeat affects only EOF. Manual NEXT/PREV steps through
     * the complete catalog, just like normal and full-repeat modes. */
    uint32_t candidate=(uint32_t)idx;
    for(uint32_t attempt=0;attempt<header.count;attempt++) {
        if(direction>0) {
            if(candidate+1>=header.count) {
                if(mode==HIFI_QUEUE_SEQUENCE)break;
                candidate=0;
            } else candidate++;
        } else {
            if(!candidate) {
                if(mode==HIFI_QUEUE_SEQUENCE)break;
                candidate=header.count-1;
            } else candidate--;
        }
        if(make_path(&file,candidate,out)){
            last_index=(int)candidate;
            snprintf(last_path,sizeof(last_path),"%s",out);
            hfile_close(&file);return true;
        }
    }
    hfile_close(&file);return false;
}

bool hifi_playlist_choose(const char *current,uint32_t mode,
                          int direction,bool automatic,
                          char out[ASTER_MEDIA_PATH_MAX])
{
    int activity=aster_power_activity_begin();
    if(activity) {
        HIFI_WARN("SD activity unavailable during queue change: %d",activity);
        if(out)out[0]=0;
        return false;
    }
    bool ok=choose_with_storage_activity(current,mode,direction,automatic,out);
    aster_power_activity_end();
    return ok;
}
