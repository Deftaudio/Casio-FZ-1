#include <iostream>
#include <windows.h>
#include <conio.h>
#include "st_dev.h"

using namespace std;

static const WORD kColorNormal = (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
static const WORD kColorTitle = (WORD)(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
static const WORD kColorAccent = (WORD)(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
static const WORD kColorWarning = (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
static const WORD kColorError = (WORD)(FOREGROUND_RED | FOREGROUND_INTENSITY);

static WORD current_console_attr()
{
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if(h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info)){
		return info.wAttributes;
	}
	return (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void set_console_attr(WORD attr)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if(h != INVALID_HANDLE_VALUE){
		SetConsoleTextAttribute(h, attr);
	}
}

static void print_colored(const char *text, WORD attr)
{
	WORD old_attr = current_console_attr();
	set_console_attr(attr);
	cout << text;
	set_console_attr(old_attr);
}

// local micro delay for checksum marker
static void busy_wait_us(unsigned int us)
{
	LARGE_INTEGER freq, start, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
	const long long target = (long long)((unsigned long long)us * (unsigned long long)freq.QuadPart / 1000000ULL);
	while(1){
		QueryPerformanceCounter(&now);
		if((now.QuadPart - start.QuadPart) >= target){
			break;
		}
	}
}

fz_port::fz_port(fz_direction d)
	: storage_device( d )
{
}

int fz_port::open()
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
	print_colored("Set FZ-1 to PORT DUMP [ SAVE EXEC ]. press ENTER,YES on FZ\n", kColorWarning);
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
unsigned int j,count = 0;
unsigned int checksum_acc = 0;
unsigned char checksum = 0;
unsigned char *buffer;

	int remote_code[]={0x7f,0,0,0,0,0,0x7f,0x7f,0,1,0,0,0,0,0,0,0};
	WORD old_attr = current_console_attr();

	system("cls");
	set_console_attr(kColorNormal);
	cout << "\n\n\n\n\n\n\n";
	cout << "--------------------------------------------------------------------------------";
	cout << "                    Set FZ-1 to ";
	set_console_attr(kColorError);
	cout << "DATA DUMP -> ";
	switch (fzh.dump_type){
		case 0:
			cout << "FULL DUMP -> LOAD FULL\n";
			break;
		case 1:
			cout << "VOICE DUMP -> LOAD VOICE\n";
			break;
		case 2:
			cout << "BANK DUMP -> LOAD BANK\n";
			break;
		case 3:
			cout << "EFFECT DUMP -> LOAD EFFECT\n";
			break;
	}

	cout << "\n";
	set_console_attr(kColorWarning);
	cout << "                         Hit a key when ready";
	set_console_attr(kColorNormal);
	cout << "\n--------------------------------------------------------------------------------";

	if(_getch() == 27){
		set_console_attr(old_attr);
		cout << "\nCanceled, ";
		return 1;
	}
	cout << "\n\n";
	cout << "               Make sure FZ is in DATA DUMP / PORT mode and waiting...\n";
	set_console_attr(old_attr);

	// Header preamble observed in FZ->FZ capture:
	// DATA=0x00, /ACKO and /BUSYO active for 12us while STBO high for 20us.
	port_io::header_preamble(20, 12, 10);

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
	Sleep(1000);
*/
	// convert header
	j= (fzh.num_blocks >> 8) & 0xff;
	fzh.num_blocks_hi= j;
	fzh.num_waves_lo= fzh.num_waves & 0xff;
	j= (fzh.num_waves >> 8) & 0xff;
	fzh.num_waves_hi= j;
	fzh.bank_num = 0x7f;
	fzh.voice_num = 0x7f; // Voice patch number 0-64
	buffer = (unsigned char *) &fzh;
	while(count < 16){
		checksum_acc += buffer[count];
		if( port.out(buffer[count]) != 0 ){
			set_console_attr(kColorError);
			cout << "\nLPT error sending header byte " << count
				 << " (io err " << port_io::get_last_error() << ")\n";
			set_console_attr(kColorNormal);
			return 1;
		}
		count++;
	}

	// calculate checksum
	checksum = (unsigned char)((0x100 - (checksum_acc & 0xFF)) & 0xFF);
	if(port.out(checksum) != 0){
		set_console_attr(kColorError);
		cout << "\nLPT error sending header checksum"
			 << " (io err " << port_io::get_last_error() << ")\n";
		set_console_attr(kColorNormal);
		return 1;
	}

	//works fine with 3x8000
	busy_wait_us(8000);
	busy_wait_us(8000);
	busy_wait_us(8000);
	//Sleep(120); // match observed FZ->FZ delay
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
		set_console_attr(kColorError);
		cout << "checksum error!" << "block " << temp << "\n";
		set_console_attr(kColorNormal);
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
		if(port.out(buffer[count]) != 0){
			set_console_attr(kColorError);
			cout << "\nLPT error sending data byte " << count
				 << " (io err " << port_io::get_last_error() << ")\n";
			set_console_attr(kColorNormal);
			return 1;
		}
		count++;
	}

	// send checksum
	if(port.out(checksum) != 0){
		set_console_attr(kColorError);
		cout << "\nLPT error sending data checksum"
			 << " (io err " << port_io::get_last_error() << ")\n";
		set_console_attr(kColorNormal);
		return 1;
	}

	//delay after sending a block of data to process checksum
	//50ms = 50000 worked fine - can't get any lower
	busy_wait_us(45000);

	return 0;

}
