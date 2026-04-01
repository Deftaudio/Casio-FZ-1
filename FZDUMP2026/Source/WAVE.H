// wave file header
// wave.h
struct wave_file_header{
	char chnk1_name[4];
	long chnk1_size;
	char chnk2_name[4];
	char chnk3_name[4];
	long chnk3_size;
	int wFormatTag;
	int nChannels;
	long nSamplesPerSec;
	long nAvgBytesPerSec;
	int nBlockAlign;
	int wBitsPerSample;
	char chnk4_name[4];
	long chnk4_size;
	};