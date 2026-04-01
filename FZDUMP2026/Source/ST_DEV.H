// Storage device header
#if !defined(_store_dev_)
#define _store_dev_
#include <stdio.h>
#include "port_io.h"

// common data type defines
typedef unsigned char byte;

enum fz_direction {source,destination};
enum file_type {fz,wave};

// common structures
/*struct fz_header{
	byte dump_type;  // 0 - FULL, 2 - BANK, 1 - VOICE, 3 - EFFECT, 0 - PARAMETERS
	int  num_blocks;
	byte num_banks;
	byte num_voice;
	int  num_waves;
	byte bank_num;
	byte voice_num;
	byte pad[7];    // pad it out
};*/
struct fz_header{
	byte dump_type;  // 0 - FULL, 2 - BANK, 1 - VOICE, 3 - EFFECT, 0 - PARAMETERS
	byte num_blocks_lo;
	byte num_blocks_hi;  // temp data holder from disk
	byte num_banks;
	byte num_voice;  // Number of voice blocks
	byte num_waves_lo;
	byte num_waves_hi;
	byte bank_num;
	byte voice_num; // Voice patch number 0-64
	int  num_blocks;
	int  num_waves;
	byte pad[4];		// filler
};

  // virtual base class
  // eg fz port, pc hard disk

class storage_device
{
public:
	storage_device(fz_direction d);
	virtual int open(void) = 0; // pure virtual function
	virtual int get_header(fz_header &fzh) = 0; // pure virtual function
	virtual int send_header(fz_header &fzh) = 0; // pure virtual function
	virtual int get_next_block(unsigned char *buffer) = 0; // pure virtual function
	virtual int send_next_block(unsigned char *buffer) = 0; // pure virtual function
	virtual ~storage_device() {}  // must have a virtual destructor
	fz_direction direction;
private:

};

// patch in/out high speed port
class fz_port : public storage_device
{
public:
	fz_port(fz_direction d);
	~fz_port();
	int open(void);
	int get_header(fz_header &fzh);
	int send_header(fz_header &fzh);
	int get_next_block(unsigned char *buffer);
	int send_next_block(unsigned char *buffer);
	int get_port_byte(void);
	void put_port_byte(int b);
private:
	port_io port;

};

// patch to/source pc hard disk
class pc_disk : public storage_device
{
public:
	pc_disk(fz_direction d, int use_browser_mode = 0);
	static void set_browser_initial_dir(const char *path);
	~pc_disk();
	int open(void);
	int get_header(fz_header &fzh);
	int send_header(fz_header &fzh);
	int get_next_block(unsigned char *buffer);
	int send_next_block(unsigned char *buffer);
private:
	FILE *f1;
	long read_long(FILE *f);
	int read_int(FILE *f);
	int skip_bytes(FILE *f,long n);
	int file_type;  // flags if we're reading a .wav file or not
	int block_num;  // only needed for wave files
	int use_browser_mode;
	char file_name[260];
	// various info from wave files
	int wFormatTag,nChannels,nBlockAlign,wBitsPerSample;
	long nSamplesPerSec,nAvgBytesPerSec,num_samples;
	fz_header fzht;  // temporary fzheader storage
};

#endif
