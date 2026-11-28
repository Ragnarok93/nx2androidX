#pragma once
#include <stdint.h>
#include <stdio.h>
#include "spatial_upscaler.h"

// Present-thread-owned snapshot, updated only after a real present succeeds.
// Extent is the reconstruction BEFORE the optional display/LSFG resolve.
typedef struct {
    int valid, output_pass, reconstructed;
    unsigned replay_flags;
    uint32_t w,h;
} dm_recon_menu_status;
static inline int dm_recon_menu_publish(dm_recon_menu_status *s,int accepted,
        int output_pass,int reconstructed,unsigned replay_flags,uint32_t w,uint32_t h) {
    if (!accepted || !w || !h) return 0;
    int changed=!s->valid||s->output_pass!=output_pass||s->reconstructed!=reconstructed||
        s->replay_flags!=replay_flags||s->w!=w||s->h!=h;
    *s=(dm_recon_menu_status){1,output_pass,reconstructed,replay_flags,w,h};
    return changed;
}
static inline void dm_recon_menu_label(const dm_recon_menu_status *s,int extent,char *out,size_t n) {
    if (!s->valid) {snprintf(out,n,"Last: waiting");return;}
    if(extent) {
        snprintf(out,n,"Last: %ux%u%s",s->w,s->h,s->reconstructed?"":" native");
    } else if(s->replay_flags&SP_REPLAY_TEMPORAL) {
        if(s->reconstructed)snprintf(out,n,"Last: GSR 2 + %s",spatial_mode_name(s->output_pass));
        else snprintf(out,n,"Last: GSR 2 replay");
    } else {
        snprintf(out,n,"Last pass: %s",s->reconstructed?spatial_mode_name(s->output_pass):"None");
    }
}

 // Last graph observed on an accepted real present; not a claim that any
 // generated image reached the display. Zero means no usable graph.
typedef struct {unsigned scale;} dm_fg_menu_status;
static inline int dm_fg_menu_publish(dm_fg_menu_status *s,int real,int scale) {
    if(!real)return 0;
    unsigned next=scale>=25&&scale<=100?(unsigned)scale:0;
    int changed=s->scale!=next;s->scale=next;return changed;
}
static inline void dm_fg_menu_label(const dm_fg_menu_status *s,char *out,size_t n) {
    if(s->scale)snprintf(out,n,"Last graph: %u%%",s->scale);
    else snprintf(out,n,"Last: waiting");
}
