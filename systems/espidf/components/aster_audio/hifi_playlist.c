/* SPDX-License-Identifier: MIT
 * Aster HiFi HIF2 playlist reader, firmware-owned global playback.
 * No decoder/I2S/Bluetooth or APP per-category state lives here.
 *
 * The APP publishes a checked HIF2 disk snapshot with:
 * 36 byte header; 160 byte records (u64 filesize + UTF-8 relative path[152]);
 * payload CRC32 and header CRC32; two alternating generation slots.
 *
 * Only read/verify the catalog when music has stopped, or when a track ended.
 * Never share a long-lived fopen across USB MSC ownership transitions.
 */
#include "hifi_playlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>

enum { HIF_MAGIC = 0x32464948u, HIF_VERSION = 2, HIF_MAX = 16384 };
static const char *const catalog_paths[2] = {
    "/SD/aster/apps/aster.hifi/data/hifi_catalog_a.bin",
    "/SD/aster/apps/aster.hifi/data/hifi_catalog_b.bin"
};
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

static uint16_t shuffled[HIF_MAX];
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
    if (!strcmp(name,"..") || !strncmp(name,"../",3) ||
        strstr(name,"/../") || strstr(name,"/..") == name+strlen(name)-3)
        return false;
    return true;
}
static bool read_header(FILE *file, const char *path, hif_header_t *head)
{
    struct stat st;
    if (fstat(fileno(file), &st)!=0 ||
        fread(head,1,sizeof(*head),file)!=sizeof(*head))return false;
    uint32_t crc=~crc_update(0xffffffffu,(const uint8_t *)head,
                              offsetof(hif_header_t,header_crc));
    return head->magic==HIF_MAGIC && head->version==HIF_VERSION &&
           head->count>0 && head->count<=HIF_MAX &&
           head->header_crc==crc &&
           (uint64_t)st.st_size==sizeof(*head)+(uint64_t)head->count*sizeof(hif_row_t);
}
static bool verify_payload(FILE *file, const hif_header_t *head)
{
    if (fseek(file,sizeof(*head),SEEK_SET))return false;
    uint8_t block[4096];
    uint32_t remaining=head->count*(uint32_t)sizeof(hif_row_t);
    uint32_t crc=0xffffffffu;
    while(remaining) {
        size_t n=remaining<sizeof(block)?remaining:sizeof(block);
        if (fread(block,1,n,file)!=n)return false;
        crc=crc_update(crc,block,n);
        remaining-=(uint32_t)n;
    }
    return (~crc)==head->payload_crc;
}
static FILE *open_current(hif_header_t *head)
{
    FILE *candidate=NULL;
    hif_header_t chosen={0};
    int slot=-1;
    for(int i=0;i<2;i++){
        FILE *f=fopen(catalog_paths[i],"rb");
        hif_header_t h={0};
        if (!f) continue;
        if (!read_header(f,catalog_paths[i],&h)) {
            fclose(f);
            continue;
        }
        if(slot<0||(int32_t)(h.generation-chosen.generation)>0){
            if(candidate)fclose(candidate);
            candidate=f;chosen=h;slot=i;
        } else fclose(f);
    }
    if(!candidate)return NULL;
    if(verified_slot!=slot || verified_generation!=chosen.generation ||
       verified_count!=chosen.count || verified_crc!=chosen.payload_crc) {
        if (!verify_payload(candidate,&chosen)) {
            fclose(candidate);
            return NULL;
        }
        verified_slot=slot;
        verified_generation=chosen.generation;
        verified_count=chosen.count;
        verified_crc=chosen.payload_crc;
        last_index=-1;
        last_path[0]=0;
        hifi_playlist_reset_shuffle();
    }
    *head=chosen;
    return candidate;
}
static bool row_at(FILE *file, uint32_t i, hif_row_t *out)
{
    uint32_t offset=(uint32_t)sizeof(hif_header_t)+i*(uint32_t)sizeof(hif_row_t);
    if (fseek(file,(long)offset,SEEK_SET) ||
        fread(out,1,sizeof(*out),file)!=sizeof(*out))return false;
    return memchr(out->path,0,sizeof(out->path)) &&
           valid_relative(out->path);
}
static const char *relative_from_music(const char *absolute)
{
    static const char a[]="/SD:/music/";
    static const char b[]="/SD/music/";
    if(!strncmp(absolute,a,sizeof(a)-1))return absolute+sizeof(a)-1;
    if(!strncmp(absolute,b,sizeof(b)-1))return absolute+sizeof(b)-1;
    return NULL;
}
static int locate(FILE *file, uint32_t count, const char *absolute)
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
static bool make_path(FILE *file,uint32_t index,char out[ASTER_MEDIA_PATH_MAX])
{
    hif_row_t row;
    if(!row_at(file,index,&row))return false;
    int size=snprintf(out,ASTER_MEDIA_PATH_MAX,"/SD:/music/%s",row.path);
    if(size<=0 || size>=ASTER_MEDIA_PATH_MAX)return false;
    struct stat st;
    return stat(out,&st)==0 && S_ISREG(st.st_mode);
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
bool hifi_playlist_choose(const char *current,uint32_t mode,
                          int direction,bool automatic,
                          char out[ASTER_MEDIA_PATH_MAX])
{
    if(!current || !out || (direction!=1 && direction!=-1) ||
       mode>HIFI_QUEUE_SHUFFLE)return false;
    out[0]=0;
    hif_header_t header={0};
    FILE *file=open_current(&header);
    if(!file)return false;
    int idx=locate(file,header.count,current);
    if(idx<0){fclose(file);return false;}
    if(automatic && mode==HIFI_QUEUE_REPEAT_ONE){
        bool yes=make_path(file,(uint32_t)idx,out);
        fclose(file);
        return yes;
    }
    if(mode==HIFI_QUEUE_SHUFFLE) {
        if(!shuffle_started || shuffle_count!=header.count ||
           shuffle_generation!=header.generation ||
           shuffled[shuffle_cursor]!=(uint16_t)idx)
            start_shuffle(header.count,header.generation,(uint32_t)idx);
        /* Back uses the existing visited order, not a new random song. */
        if(direction<0 && shuffle_cursor==0) {
            fclose(file);return false;
        }
        if(direction>0 && shuffle_cursor+1>=shuffle_count)
            start_shuffle(header.count,header.generation,(uint32_t)idx);
        for(uint32_t attempt=0;attempt<header.count;attempt++){
            if(direction>0) {
                if(shuffle_cursor+1>=shuffle_count)break;
                shuffle_cursor++;
            } else shuffle_cursor--;
            uint32_t candidate=shuffled[shuffle_cursor];
            if(make_path(file,candidate,out)){
                last_index=(int)candidate;
                snprintf(last_path,sizeof(last_path),"%s",out);
                fclose(file);return true;
            }
            if(direction<0 && !shuffle_cursor)break;
        }
        fclose(file);return false;
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
        if(make_path(file,candidate,out)){
            last_index=(int)candidate;
            snprintf(last_path,sizeof(last_path),"%s",out);
            fclose(file);return true;
        }
    }
    fclose(file);return false;
}
