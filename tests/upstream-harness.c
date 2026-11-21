/* A program that drives XaviXEmu's own sources, with none of this repository's
 * code: the machine, a scripted pad (tests/route.py's route), and a hash of the
 * picture, the sound and the state every 60 frames. It is the independent
 * witness of the core's tests: what the core shows must be what upstream's
 * unmodified sources show. waterbox/run-gate.sh builds it against a pristine
 * checkout of upstream's pinned commit (no patch applied) and compares its
 * pictures with the core's.
 *
 * Needs the bare ROM image of Excite Boxing (the .bin inside its ZIP).
 */
/* Headless XaviXEmu core harness: no Win32. Usage:
 *   harness rom.bin FRAMES [-s K state.bin] [-l state.bin] [-r] [-p dir]
 * -s K f : save state to f after frame K (K frames run)
 * -l f   : start from state f (frame counter resumes at the saved K, given in file name arg via -k)
 * -k K   : frame number the loaded state was saved at
 * -r     : save+load the state around every frame
 * -p dir : write PPM of frames 600,1200,1800,... to dir
 * Prints "frame fbhash audiohash statehash" every 60 frames. */
#include "drgqst_core.h"
#include "drgqst_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t fnv(const void *p, size_t n, uint64_t h){const uint8_t*b=p;while(n--){h^=*b++;h*=0x100000001b3ULL;}return h;}
static uint8_t *slurp(const char *f,size_t *n){FILE*fp=fopen(f,"rb");if(!fp){perror(f);exit(2);}fseek(fp,0,SEEK_END);*n=ftell(fp);rewind(fp);uint8_t*b=malloc(*n);if(fread(b,1,*n,fp)!=*n)exit(2);fclose(fp);return b;}
/* Scripted input0: bit0 select, bit1 back, 0x10 up 0x20 down 0x40 left 0x80 right.
 * "motion" for this game is the digital direction bits (what main.c derives
 * from the mouse position outside the neutral area). */
static uint8_t input_at(unsigned f){
	if(f<400) return 0;
	unsigned t=f-400, ph=t%240, seg=(t/240)%6;
	static const uint8_t dirs[6]={0x80,0x20,0x40,0x10,0x80,0x20};
	if(ph<4) return 0x01;               /* select pulse */
	if(ph>=60&&ph<70) return dirs[seg]; /* direction pulse */
	if(ph>=120&&ph<124) return (seg&1)?0x02:0x01;
	return 0;
}
int main(int argc,char**argv){
	if(argc<3) return 2;
	size_t rn; uint8_t*rom=slurp(argv[1],&rn); unsigned N=atoi(argv[2]);
	const char*sf=0,*lf=0,*pd=0; unsigned K=0,start=0; int rt=0;
	for(int i=3;i<argc;i++){
		if(!strcmp(argv[i],"-s")){K=atoi(argv[++i]);sf=argv[++i];}
		else if(!strcmp(argv[i],"-l")) lf=argv[++i];
		else if(!strcmp(argv[i],"-k")) start=atoi(argv[++i]);
		else if(!strcmp(argv[i],"-r")) rt=1;
		else if(!strcmp(argv[i],"-p")) pd=argv[++i];
	}
	static drgqst_core core;
	if(!drgqst_core_init_profile(&core,rom,rn,DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM)){fprintf(stderr,"init failed\n");return 1;}
	size_t ss=drgqst_state_serialized_size(); uint8_t*sb=malloc(ss),*sb2=malloc(ss);
	if(lf){size_t n;uint8_t*d=slurp(lf,&n);if(!drgqst_state_load(&core,d,n)){fprintf(stderr,"load failed\n");return 1;}free(d);}
	for(unsigned f=start;f<N;f++){
		if(rt){size_t w;if(!drgqst_state_save(&core,sb2,ss,&w)||!drgqst_state_load(&core,sb2,w)){fprintf(stderr,"roundtrip failed\n");return 1;}}
		core.machine.state.input0=input_at(f);
		const uint32_t*fb=drgqst_core_run_frame(&core);
		unsigned done=f+1;
		if(done%60==0||done==K){
			size_t w; if(!drgqst_state_save(&core,sb,ss,&w)){fprintf(stderr,"save failed\n");return 1;}
			uint64_t hf=fnv(fb,XAVIX_VIDEO_PIXELS*4,0xcbf29ce484222325ULL);
			uint64_t ha=fnv(drgqst_core_frame_audio(&core),sizeof core.frame_audio,0xcbf29ce484222325ULL);
			uint64_t hs=fnv(sb,w,0xcbf29ce484222325ULL);
			printf("%u %016llx %016llx %016llx\n",done,(unsigned long long)hf,(unsigned long long)ha,(unsigned long long)hs);
			if(sf&&done==K){FILE*o=fopen(sf,"wb");fwrite(sb,1,w,o);fclose(o);}
		}
		if(pd&&done%300==0){char p[512];snprintf(p,sizeof p,"%s/f%05u.ppm",pd,done);FILE*o=fopen(p,"wb");fprintf(o,"P6\n256 224\n255\n");
			for(int i=0;i<XAVIX_VIDEO_PIXELS;i++){uint32_t v=fb[i];uint8_t c[3]={v>>16,v>>8,v};fwrite(c,1,3,o);}fclose(o);}
	}
	return 0;
}
