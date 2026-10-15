#include "video_sync.h"
void xf_video_sync_sample(xf_video_sync *s, uint64_t now, int blank) {
    if(blank && !s->level) {
        if(s->edges) {
            uint64_t period=now-s->last_edge_us;
            /* Ignore missed-edge multiples; median rejects poll jitter. This
             * is an observed refresh estimate, not a hardware timestamp. */
            if(period>=12000 && period<=25000) {
                unsigned sorted[16],i,j,n;
                s->periods[s->sample_at++%16]=(unsigned)period;
                if(s->samples<16) ++s->samples;
                n=s->samples;
                for(i=0;i<n;++i) {
                    unsigned value=s->periods[i];j=i;
                    while(j && sorted[j-1]>value) {sorted[j]=sorted[j-1];--j;}
                    sorted[j]=value;
                }
                s->period_us=sorted[n/2];
            }
        }
        s->last_edge_us=now; ++s->edges;
    }
    s->level=!!blank;
}
int xf_video_sync_ready(xf_video_sync *s, uint64_t now) {
    unsigned divisor=s->mode==XF_SYNC_HALF?2:1;
    if(!s->mode || s->fallbacks) return 1;
    if(!s->wait_at) s->wait_at=now+1; /* zero is the unset sentinel */
    if(s->mode==XF_SYNC_HALF && s->samples>=4 && (s->period_us<15000 || s->period_us>18500)) s->half_rejected=1;
    if(s->half_rejected) divisor=1; /* explicitly reported fallback to ON */
    if(s->level && (!s->released || s->edges-s->released>=divisor) &&
       (s->mode!=XF_SYNC_HALF || s->samples>=4)) {
        s->released=s->edges; s->wait_us+=now+1-s->wait_at; s->wait_at=0; return 1;
    }
    if(now+1-s->wait_at>=100000) {
        ++s->faults; ++s->fallbacks; s->wait_us+=now+1-s->wait_at; s->wait_at=0; return 1;
    }
    return 0;
}
