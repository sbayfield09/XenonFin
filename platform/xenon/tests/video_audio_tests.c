#include "../video_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static xf_av_audio audio;static unsigned checks,pending,submitted;static unsigned char capture[65536];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"AV audio line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int queued(void *p){(void)p;return (int)pending;}
static int available(void *p){(void)p;return 65536-(int)pending;}
static int submit(void *p,const unsigned char *bytes,size_t n){(void)p;CHECK(n==2048 && pending+n<=XF_AV_DMA_BYTES);CHECK(submitted+n<=sizeof(capture));memcpy(capture+submitted,bytes,n);submitted+=(unsigned)n;pending+=(unsigned)n;return 1;}
static void reset(int64_t offset){
    xf_audio_backend backend={queued,available,submit,NULL,NULL,NULL};xf_av_audio_init(&audio,&backend);audio.enabled=1;
    pending=submitted=0;memset(capture,0xaa,sizeof(capture));
    audio.have_video_pts=audio.have_audio_pts=1;audio.video_pts=45000;audio.audio_pts=45000+offset;
    memset(audio.decoder.pcm,0x31,sizeof(audio.decoder.pcm));audio.decoder.pcm_size=sizeof(audio.decoder.pcm);audio.decoder.pcm_write=0;
    xf_av_audio_decode(&audio,0);CHECK(audio.aligned && !audio.failed);
}
int main(void){
    uint64_t position;unsigned i;
    reset(0);xf_av_audio_output(&audio,1,0,0);CHECK(!audio.started && !pending);
    xf_av_audio_output(&audio,1,1,0);CHECK(audio.started && pending==8192 && audio.submitted==2048);
    CHECK(xf_av_audio_clock(&audio,&position) && position==0); /* submission is NOT playback */
    pending=4096;CHECK(xf_av_audio_clock(&audio,&position) && position==21333);
    for(i=0;i<10000;++i){xf_av_audio_output(&audio,0,1,0);CHECK(xf_av_audio_clock(&audio,&position) && position==21333 && pending==4096);}
    pending=0;CHECK(xf_av_audio_clock(&audio,&position) && position==42666);
    for(i=0;i<1000;++i)CHECK(xf_av_audio_clock(&audio,&position) && position==42666);
    /* Genuine input starvation latches one underrun until sufficient preroll. */
    audio.decoder.pcm_size=0;xf_av_audio_output(&audio,1,1,0);CHECK(audio.underruns==1 && audio.rebuffering && !pending);
    xf_av_audio_output(&audio,1,1,0);CHECK(audio.underruns==1);
    audio.decoder.pcm_size=XF_AV_RECOVER_PCM-4;xf_av_audio_output(&audio,1,1,0);CHECK(!pending && audio.underruns==1);
    audio.decoder.pcm_size=XF_AV_RECOVER_PCM;xf_av_audio_output(&audio,1,0,0);CHECK(!pending);
    xf_av_audio_output(&audio,1,1,0);CHECK(pending==8192 && !audio.rebuffering);
    /* Positive offset inserts sample-exact silence, no descriptor padding gap. */
    reset(9000);
    for(i=0;i<4;++i){xf_av_audio_output(&audio,1,1,0);pending=0;}
    CHECK(audio.initial_offset_us==100000 && !audio.silence_frames && submitted>=24576 && submitted<=32768);
    for(i=0;i<19200;++i)CHECK(!capture[i]);
    for(;i<submitted;++i)CHECK(capture[i]==0x31);
    /* Demux-blocked silence advances ONLY through DMA. New timestamped PCM
     * begins at its correct media sample, including a gap inside a block. */
    reset(0);audio.decoder.pcm_size=0;audio.bridge=1;
    xf_av_audio_output(&audio,1,1,1);CHECK(pending==8192 && audio.bridge_frames==2048);
    pending=0;CHECK(xf_av_audio_clock(&audio,&position) && position==42666);
    audio.maps[0].base_pts=45000+5625;audio.decoder.pcm_size=XF_AV_RECOVER_PCM;
    for(i=0;i<3;++i){xf_av_audio_output(&audio,1,1,0);pending=0;}
    CHECK(!audio.bridge && submitted>12000);
    for(i=0;i<12000;++i)CHECK(!capture[i]);
    for(;i<submitted;++i)CHECK(capture[i]==0x31);
    reset(-9000);for(i=0;i<8;++i)xf_av_audio_decode(&audio,0);
    CHECK(!audio.trim_frames && !audio.trimmed_frames && audio.clock_offset_us== -100000 && audio.initial_offset_us== -100000);
    xf_av_audio_output(&audio,1,1,0);CHECK(pending==8192 && capture[0]==0x31);CHECK(!xf_av_audio_clock(&audio,&position) && !position);
    /* Timestamp marks inside a split frame apply to the next frame start. */
    reset(0);audio.have_audio_pts=0;audio.aligned=0;
    CHECK(xf_av_audio_stamp(&audio,1,100,45000)==1);
    audio.decoder.on_frame(&audio,99,0);CHECK(!audio.have_audio_pts && audio.stamp_count==1);
    audio.decoder.on_frame(&audio,100,0);CHECK(audio.have_audio_pts && !audio.stamp_count && !audio.timestamp_error_us);
    CHECK(xf_av_audio_stamp(&audio,1,200,47160)==1);audio.decoder.on_frame(&audio,200,1152);CHECK(!audio.failed && !audio.timestamp_error_us);
    CHECK(xf_av_audio_stamp(&audio,1,300,100000)==1);audio.decoder.on_frame(&audio,300,2304);CHECK(!audio.failed && audio.gap_events==1);
    CHECK(xf_av_audio_stamp(&audio,1,400,45000)==1);audio.decoder.on_frame(&audio,400,3456);CHECK(audio.failed);
    reset(0);
    CHECK(xf_av_audio_stamp(&audio,1,100,51660)==1);audio.decoder.on_frame(&audio,100,3456);
    CHECK(!audio.failed && audio.gap_events==1 && audio.timestamp_error_us==2000);
    CHECK(xf_av_audio_stamp(&audio,1,200,53640)==1);audio.decoder.on_frame(&audio,200,4608);
    CHECK(!audio.failed && audio.gap_events==2 && audio.timestamp_error_us== -2000);
    reset(0);for(i=0;i<XF_AV_STAMPS;++i)CHECK(xf_av_audio_stamp(&audio,1,i,45000)==1);CHECK(!xf_av_audio_stamp(&audio,1,999,45000));
    /* Actual Layer II header fails before an unsupported decoder can run. */
    reset(0);audio.decoder.pcm_size=0;
    for(i=0;i<4096;++i)CHECK(xf_av_audio_byte(&audio,(unsigned char)(i%4==0?0xff:i%4==1?0xfd:i%4==2?0x84:0)));
    xf_av_audio_decode(&audio,0);CHECK(audio.failed && strstr(audio.error,"Layer III") && !audio.decoder.mp3_frames);
    reset(0);audio.decoder.finished=1;audio.decoder.pcm_size=400;audio.decoder.pcm_read=0;
    xf_av_audio_output(&audio,1,1,0);CHECK(pending==2048 && audio.tail_frames==412 && !xf_av_audio_done(&audio));pending=0;CHECK(xf_av_audio_done(&audio));
    reset(0);pending=65537;CHECK(!xf_av_audio_clock(&audio,&position) && audio.failed);
    /* Counters cross 32-bit sample and repeated 64 KiB hardware ring wraps.
     * Empty software PCM does not affect the consumed-sample calculation. */
    reset(0);audio.started=1;audio.submitted=(1ULL<<33)+2048;audio.played=(1ULL<<33);pending=8192;audio.decoder.pcm_size=0;
    CHECK(xf_av_audio_clock(&audio,&position) && position==(1ULL<<33)*1000000/48000);
    pending=4096;CHECK(xf_av_audio_clock(&audio,&position) && audio.played==(1ULL<<33)+1024);
    pending=6144;CHECK(xf_av_audio_clock(&audio,&position) && audio.played==(1ULL<<33)+1024); /* stale larger queue cannot reverse time */
    pending=0;CHECK(xf_av_audio_clock(&audio,&position) && audio.played==audio.submitted);
    for(i=0;i<500;++i)CHECK(xf_av_audio_clock(&audio,&position) && audio.played==audio.submitted);
    audio.rebuffering=1;CHECK(!xf_av_audio_clock(&audio,&position) && position==audio.submitted*1000000/48000);
    xf_av_audio_init(&audio,NULL);CHECK(!audio.played && !audio.submitted && !audio.started);
    /* A partially consumed stereo frame is not a fully consumed frame. */
    reset(0);audio.decoder.finished=1;audio.decoder.pcm_size=0;audio.tail_limit_frames=1;
    xf_av_audio_output(&audio,1,1,1);CHECK(submitted==2048);pending=0;xf_av_audio_output(&audio,1,1,1);CHECK(submitted==2048);
    reset(0);xf_av_audio_output(&audio,1,1,0);pending=8191;CHECK(xf_av_audio_clock(&audio,&position) && !position);
    /* A timestamp overlap is rejected, never repaired by dropping real PCM. */
    reset(0);audio.maps[0].base_pts=audio.origin_pts-90;xf_av_audio_output(&audio,1,1,0);
    CHECK(audio.failed && !audio.late_frames && !audio.trimmed_frames && !submitted && strstr(audio.error,"no samples discarded"));
    puts("Native movie audio DMA consumption clock, timestamp-origin alignment without PCM trimming, pause/rebuffer, PTS and codec rejection PASS");return 0;
}
