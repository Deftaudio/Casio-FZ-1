// casio.h
// casio fz-1 internal structures

#if !defined(_casio_h_)
#define _casio_h_

#if !defined(_byte_defined_)
#define _byte_defined_
typedef unsigned char byte;
#endif

// define casio voice data structure
#pragma pack(push, 1)
struct voicedata {
long	wavst;	//
long	waved;	//
long	genst;	//
long	gened;	//
short	loop;	//
byte	loop_sus;	//
byte	loop_end;	//
long	loopst[8];	//
long	looped[8];	//
short	loopxf[8];	//
unsigned short	looptm[8];	//
short	dcp;	//
byte	dcf;	//
byte	dcq;	//
byte	dca_sus;	//
byte	dca_end;	//
byte	dca_rate[8];	//
unsigned char	dca_stop[8];	//
byte	dcf_sus;	//
byte	dcf_end;	//
byte	dcf_rate[8];	//
unsigned char	dcf_stop[8];	//
unsigned short	lfo_delay;	//
unsigned char	lfo_name;	//
unsigned char	lfo_atck;	//
byte	lfo_rate;	//
byte	lfo_dcp;	//
byte	lfo_dca;	//
byte	lfo_dcf;	//
byte	lfo_dcq;	//
byte	vel_dcq_kf;	//
byte	dca_kf;	//
byte	dca_rs;	//
byte	dcf_kf;	//
byte	dcf_rs;	//
byte	vel_dca_kf;	//
byte	vel_dca_rs;	//
byte	vel_dcf_kf;	//
byte	vel_dcf_rs;	//
unsigned char	hwid;	//
unsigned char	lwid;	//
unsigned char	cent;	//
unsigned char	samp;	// sampling freq
char	name[14];
};
#pragma pack(pop)

#endif
