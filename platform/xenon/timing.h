#ifndef XF_TIMING_H
#define XF_TIMING_H
#include <stdint.h>
/* Split first: neither uptime*ticks nor uptime*1e6 may overflow. The pinned
 * Xenon frequency is 49,875,000 Hz, NOT an integer ticks-per-microsecond. */
static inline uint64_t xf_ticks_to_us(uint64_t ticks, uint64_t hz) {
    return ticks/hz*1000000U+(ticks%hz)*1000000U/hz;
}
static inline uint64_t xf_frame_pts(unsigned frame, unsigned numerator, unsigned denominator) {
    return ((uint64_t)frame*1000000U*denominator+numerator/2U)/numerator;
}
#define XF_TIME_BINS 1024U
#define XF_TIME_BIN_US 250U
typedef struct { uint64_t sum,max; unsigned count, bins[XF_TIME_BINS]; } xf_time_stats;
static inline void xf_time_add(xf_time_stats *s, uint64_t us) {
    uint64_t bin=(us+XF_TIME_BIN_US-1)/XF_TIME_BIN_US;
    if(bin>=XF_TIME_BINS) bin=XF_TIME_BINS-1;
    ++s->bins[bin]; ++s->count; s->sum+=us; if(us>s->max) s->max=us;
}
/* All-run p95, upper edge of 0.25 ms bins. Last bin is explicit overflow;
 * return the observed maximum if p95 lies there rather than truncate it. */
static inline uint64_t xf_time_p95(const xf_time_stats *s) {
    unsigned i,n=0,target=(unsigned)(((uint64_t)s->count*95U+99U)/100U);
    if(!target) return 0;
    for(i=0;i<XF_TIME_BINS;++i) if((n+=s->bins[i])>=target)
        return i==XF_TIME_BINS-1?s->max:(uint64_t)i*XF_TIME_BIN_US;
    return s->max;
}
#endif
