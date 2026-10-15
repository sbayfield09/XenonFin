/* Bounded MPEG program-stream framing. MPEG-1 and MPEG-2 PES headers are
 * distinct from the MPEG-1 video codec carried inside. Never feed container
 * headers, audio, subtitles, or an unsupported original file to pl_mpeg. */
#include "video_ps.h"
#include <string.h>
#include <stdio.h>
void xf_video_ps_init(xf_video_ps *s) { memset(s,0,sizeof(*s)); }
size_t xf_video_ps_space(void *context) { xf_video_ps *s=context;return s->failed?0:XF_PS_RING-s->count; }
int xf_video_ps_byte(void *context,unsigned char c) {
    xf_video_ps *s=context;
    if(s->failed || s->count==XF_PS_RING) return 0;
    s->ring[(s->head+s->count)%XF_PS_RING]=c;++s->count;++s->received;
    if(s->count>s->peak)s->peak=s->count;
    return 1;
}
static unsigned at(const xf_video_ps *s,size_t i) {return s->ring[(s->head+i)%XF_PS_RING];}
static void consume(xf_video_ps *s,size_t n) {s->head=(s->head+n)%XF_PS_RING;s->count-=n;}
static int fail(xf_video_ps *s,const char *error) {s->failed=1;snprintf(s->error,sizeof(s->error),"%s",error);return -1;}
static int pes(xf_video_ps *s,unsigned type,size_t length) {
    unsigned char *p=s->packet;size_t off=6,end=6+length,pts=0;
    if(type!=0xe0) return 1; /* skip audio, private streams and other video IDs */
    if(!length || end<=off) return fail(s,"Unbounded or empty MPEG-PS PES packet.");
    if((p[off]&0xc0)==0x80) {
        if(end-off<3 || (size_t)p[off+2]>end-off-3) return fail(s,"Truncated MPEG-2 PES header.");
        if((p[off+1]&0xc0)==0x40) return fail(s,"Invalid PES timestamp flags.");
        if(p[off+1]&0x80) {if(p[off+2]<5)return fail(s,"Missing PES timestamp.");pts=off+3;}
        off+=3+p[off+2];
    } else {
        unsigned n=0;
        while(off<end && p[off]==255) {if(++n>16)return fail(s,"Excessive PES stuffing.");++off;}
        if(off>=end) return fail(s,"Truncated MPEG-1 PES header.");
        if((p[off]&0xc0)==0x40) {if(end-off<2)return fail(s,"Truncated PES buffer field.");off+=2;}
        if(off>=end) return fail(s,"Missing PES flags.");
        if((p[off]&0xf0)==0x20) {pts=off;off+=5;}
        else if((p[off]&0xf0)==0x30) {pts=off;off+=10;}
        else if(p[off]==15) ++off;
        else return fail(s,"Invalid MPEG-1 PES flags.");
        if(off>end) return fail(s,"Truncated PES timestamp.");
    }
    if(pts) {
        if(!(p[pts]&1) || !(p[pts+2]&1) || !(p[pts+4]&1)) return fail(s,"Invalid PES timestamp markers.");
        s->pts90k=((uint64_t)(p[pts]&14)<<29)|((uint64_t)p[pts+1]<<22)|((uint64_t)(p[pts+2]&254)<<14)|((uint64_t)p[pts+3]<<7)|(p[pts+4]>>1);
        s->have_pts=1;
    }
    s->payload=off;s->payload_end=end;return 1;
}
int xf_video_ps_tick(xf_video_ps *s,int (*sink)(unsigned char),size_t (*space)(void),int eof) {
    unsigned budget=32768,progress=0;s->eof=!!eof;
    if(s->failed)return -1;
    while(budget) {
        size_t n,i;unsigned type;
        if(s->payload<s->payload_end) {
            size_t capacity=space();if(!capacity)return (int)progress;
            n=s->payload_end-s->payload;if(n>capacity)n=capacity;if(n>budget)n=budget;
            for(i=0;i<n;++i) if(!sink(s->packet[s->payload++])) return fail(s,"Elementary MPEG input rejected.");
            s->video_bytes+=n;budget-=(unsigned)n;progress=1;
            if(s->payload<s->payload_end) return 1;
            s->payload=s->payload_end=s->packet_size=0;
        }
        if(s->count<4) break;
        if(at(s,0)!=0 || at(s,1)!=0 || at(s,2)!=1) return fail(s,"Expected MPEG-PS start code; unsupported original/container.");
        type=at(s,3);
        if(!s->seen_pack && type!=0xba) return fail(s,"Stream is not MPEG-PS.");
        if(s->ended) return fail(s,"Unexpected data after program end.");
        if(type==0xb9) {consume(s,4);s->ended=1;progress=1;continue;}
        if(type==0xba) {
            if(s->count<5)break;
            if((at(s,4)&0xf0)==0x20)n=12;
            else if((at(s,4)&0xc0)==0x40) {if(s->count<14)break;n=14+(at(s,13)&7);}
            else return fail(s,"Invalid MPEG-PS pack header.");
            if(s->count<n)break;
            consume(s,n);s->seen_pack=1;progress=1;--budget;continue;
        }
        if(type<0xbb) return fail(s,"Unexpected elementary start code in program stream.");
        if(s->count<6)break;
        n=6+at(s,4)*256+at(s,5);
        if(s->count<n)break;
        /* Account input parsing as bounded work even for non-video packets. */
        if(type==0xe0) {
            for(i=0;i<n;++i)s->packet[i]=(unsigned char)at(s,i);
            s->packet_size=n;consume(s,n);++s->packets;progress=1;--budget;
            if(pes(s,type,n-6)<0)return -1;
        } else {consume(s,n);progress=1;--budget;}
    }
    if(eof && !s->count && s->payload==s->payload_end) {s->ended=1;return (int)progress;}
    if(eof && budget && s->count && s->payload==s->payload_end) return fail(s,"HTTP ended inside a MPEG-PS packet.");
    return (int)progress;
}
