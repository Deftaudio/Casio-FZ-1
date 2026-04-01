#include <iostream.h>
#include "st_dev.h"
#include "dos.h"
#include "conio.h"


fz_port::fz_port(fz_direction d)
	: storage_device( d )
{
}

fz_port::open()
{
	if(direction == source){
		return port.open_input();
	}else{
		return port.open_output();
	}
}

fz_port::~fz_port()
{

}

int fz_port::get_header(fz_header &fzh)
{
unsigned int count = 0;
int checksum=0,t;
unsigned char byte_in;
unsigned char *buffer;

	buffer = (unsigned char *) &fzh;
	cout << "Set FZ-1 to PORT DUMP [ SAVE EXEC ]. press ENTER,YES on FZ\n";
	checksum = 0;
	while(count < 16 + 1){ // extra 1 is checksum
		if((t = port.in()) == -1)  // read input byte
			return 1;
		byte_in = t;
//cout << t << " ";
		buffer[count++] = byte_in;
		checksum += byte_in;
	}
	count--; // adjust count

	// check checksum
	checksum &= 255;
	// Note: header checksum is always wrong
	// but allows me to calculate what the first byte should have been
	if(buffer[0] == 0)
		buffer[0] = 256 - checksum;
//cout << "cs=" << checksum << "\n";
	/*
	if(checksum != 0){
		cout << "INTERNAL ERROR: HEADER CHECKSUM\n";
		return 1;
	}
	*/
//cout << "\n";
//return 1;
	// fix up data packing problems
	fzh.num_blocks = fzh.num_blocks_hi;
	fzh.num_blocks = (fzh.num_blocks << 8) + fzh.num_blocks_lo;
	fzh.num_waves = fzh.num_waves_hi;
	fzh.num_waves = (fzh.num_waves << 8) + fzh.num_waves_lo;
//   printf("\n nbh = %d, nbl = %d\n",(int) fzh.num_blocks_hi, (int) fzh.num_blocks_lo);
//   printf("\n nb = %d, nw = %d\n",(int) fzh.num_blocks, (int) fzh.num_waves);
   return 0;
}

int fz_port::send_header(fz_header &fzh)
{
unsigned int j,k,count = 0;
unsigned char checksum=0;
unsigned char *buffer;

	int remote_code[]={0x7f,0,0,0,0,0,0x7f,0x7f,0,1,0,0,0,0,0,0,0};

	cout << "\nSet FZ-1 to DATA DUMP, ";
	switch (fzh.dump_type){
		case 0:
			cout << "FULL DUMP, LOAD FULL\n";
			break;
		case 1:
			cout << "VOICE DUMP, LOAD VOICE\n";
			break;
		case 2:
			cout << "BANK DUMP, LOAD BANK\n";
			break;
		case 3:
			cout << "EFFECT DUMP, LOAD EFFECT\n";
			break;
	}
	cout << "Hit a key when ready";
	if(getch() == 27){
		cout << "\nCanceled, ";
		return 1;
	}

/* This does not work
	// Put FZ into remote mode
	remote_code[8] = fzh.dump_type;
	for(j=0;j<15;j++){
		checksum+=remote_code[j];
		port.out(remote_code[j]);
	}
	checksum &= 255;
	checksum = 256-checksum;
	port.out(checksum);
	delay(1000);
*/
	// convert header
	j= (fzh.num_blocks >> 8) & 0xff;
	fzh.num_blocks_hi= j;
	fzh.num_waves_lo= fzh.num_waves & 0xff;
	j= (fzh.num_waves >> 8) & 0xff;
	fzh.num_waves_hi= j;
	fzh.bank_num = 0x7f;
	fzh.voice_num = 0x7f; // Voice patch number 0-64
	checksum = 0;

	buffer = (unsigned char *) &fzh;
	while(count < 16){
		checksum -= buffer[count];
		if( port.out(buffer[count++]) != 0 )
			return 1;
	}

	// calculate checksum
	if(port.out(checksum) != 0)
		return 1;

	delay(140); // seems to be necessary 130-150 ms
	return 0;
}

int fz_port::get_next_block(unsigned char *buffer)
{
		static int temp=0;
unsigned int count = 0;
unsigned char checksum = 0;
unsigned char byte_in;

	while( count < 1025 ){ // extra 1 is checksum
		byte_in = port.in();  // read input byte
		buffer[count++] = byte_in;
		checksum += byte_in;
	}

	// check checksum
temp++;
	if(checksum != 0 ){
		cout << "checksum error!" << "block " << temp << "\n";
		return 1;
	}
	return 0;
}

int fz_port::send_next_block(unsigned char *buffer)
{
unsigned int count = 0;
unsigned char checksum = 0;

	while(count < 1024){
		checksum -= buffer[count];
		if(port.out(buffer[count++]) != 0)
			return 1;
	}

	// send checksum
	if(port.out(checksum) != 0)
		return 1;
	return 0;
}

