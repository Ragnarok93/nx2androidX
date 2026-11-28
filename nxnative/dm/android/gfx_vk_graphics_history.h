#ifndef NX2A_VK_GRAPHICS_HISTORY_H
#define NX2A_VK_GRAPHICS_HISTORY_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Preserve the previous PPE2 file format. Keep complete records, trim a
// partial final record, and migrate legacy game-directory history once.
// A read/allocation failure never truncates a valid history.
static inline FILE *vk_graphics_history_open(const char *path,const char *legacy,
        uint32_t magic,uint32_t stride,void **records,size_t *count) {
    const uint32_t hdr[2]={magic,stride}; uint32_t got[2];
    *records=NULL; *count=0;
    if (!path || !*path || !stride) return NULL;
    FILE *f=fopen(path,"r+b"); int existing=f!=NULL, valid=0;
    if (!f && errno!=ENOENT) return NULL;
    if (!f && legacy && *legacy && strcmp(path,legacy)) {
        f=fopen(legacy,"rb");
        if (!f && errno!=ENOENT) return NULL;
    }
    if (f) {
        struct stat st;
        if (fstat(fileno(f),&st) || st.st_size<0) goto fail;
        valid=fread(got,sizeof got,1,f)==1 && !memcmp(got,hdr,sizeof got);
        if (ferror(f)) goto fail;
        if (valid) {
            uint64_t bytes=(uint64_t)st.st_size-sizeof hdr;
            // Avoid unbounded allocations from a malformed or huge history.
            if (bytes>(64u<<20)) goto fail;
            size_t n=(size_t)(bytes/stride);
            if (n) {
                *records=malloc(n*(size_t)stride);
                if (!*records || fread(*records,stride,n,f)!=n) goto fail;
            }
            *count=n;
            if (existing) {
                off_t end=(off_t)(sizeof hdr+n*(size_t)stride);
                if (st.st_size!=end && ftruncate(fileno(f),end)) goto fail;
                if (fseeko(f,end,SEEK_SET)) goto fail;
                return f;
            }
        }
        fclose(f); f=NULL;
    }
    // Create or migrate atomically rather than truncating the only copy.
    char tmp[1100]; int len=snprintf(tmp,sizeof tmp,"%s.tmp.XXXXXX",path);
    if (len<0 || (size_t)len>=sizeof tmp) goto fail;
    int fd=mkstemp(tmp);
    if (fd<0) goto fail;
    f=fdopen(fd,"w+b");
    if (!f) { close(fd); unlink(tmp); goto fail; }
    if (fwrite(hdr,sizeof hdr,1,f)!=1 ||
        (*count && fwrite(*records,stride,*count,f)!=*count) ||
        fflush(f) || fsync(fd) || rename(tmp,path)) {
        unlink(tmp); goto fail;
    }
    return f;
fail:
    if (f) fclose(f);
    free(*records); *records=NULL; *count=0;
    return NULL;
}
#endif
