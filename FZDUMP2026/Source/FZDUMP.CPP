#include <iostream>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>
#include <mmsystem.h>
#include "st_dev.h"
#include "port_io.h"

using namespace std;

#pragma comment(lib, "winmm.lib")

static const WORD kColorNormal = (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
static const WORD kColorTitle = (WORD)(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
static const WORD kColorAccent = (WORD)(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
static const WORD kColorWarning = (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
static const WORD kColorError = (WORD)(FOREGROUND_RED | FOREGROUND_INTENSITY);
static const WORD kColorDim = (WORD)(FOREGROUND_RED | FOREGROUND_GREEN);

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

static void print_hit_key_prompt()
{
	print_colored("Hit a key\n", kColorWarning);
}

static void draw_progress_bar(int current, int total)
{
	const int bar_width = 34;
	int filled = bar_width;
	int percent = 100;
	int k;

	if(total > 0){
		filled = (current * bar_width) / total;
		percent = (current * 100) / total;
	}

	cout << "\r";
	print_colored("Transfer ", kColorWarning);
	set_console_attr(kColorNormal);
	cout << "[";
	set_console_attr(kColorAccent);
	for(k = 0; k < filled; ++k){
		cout << "#";
	}
	set_console_attr(kColorDim);
	for(k = filled; k < bar_width; ++k){
		cout << "-";
	}
	set_console_attr(kColorNormal);
	cout << "] ";
	set_console_attr(kColorTitle);
	cout.width(3);
	cout << percent;
	set_console_attr(kColorNormal);
	cout << "% ";
	set_console_attr(kColorDim);
	cout << "| ";
	set_console_attr(kColorAccent);
	cout << current;
	set_console_attr(kColorNormal);
	cout << "/";
	set_console_attr(kColorAccent);
	cout << total;
	set_console_attr(kColorNormal);
	cout.flush();
}

static void clear_screen()
{
	system("cls");
}

static void print_port_status()
{
	unsigned char status = port_io::read_status();
	unsigned char control = port_io::read_control();
	unsigned char data = port_io::read_data();
	cout << "status=0x" << hex << (int)status
		 << " control=0x" << hex << (int)control
		 << " data=0x" << hex << (int)data << dec;
	cout << " busy(bit7)=" << (((status & 0x80) != 0) ? 1 : 0);
	cout << " busy(asserted)=" << (((status & 0x80) == 0) ? 1 : 0);
	cout << " ack(bit6)=" << (((status & 0x40) != 0) ? 1 : 0);
	cout << " ack(asserted)=" << (((status & 0x40) == 0) ? 1 : 0);
	cout << "\n";
}

static void wait_key()
{
	set_console_attr(kColorWarning);
	cout << "Hit a key...";
	set_console_attr(kColorNormal);
	_getch();
	cout << "\n";
}

static void test_port_lines()
{
	clear_screen();
	cout << "LPT line test mode\n";
	cout << "Connect logic probe/5V source as needed.\n\n";
	port_io::reset_lines();

	cout << "Data lines: set LOW (0x00)\n";
	port_io::notristate();
	port_io::write_data(0x00);
	print_port_status();
	wait_key();

	cout << "Data lines: set HIGH (0xFF)\n";
	port_io::write_data(0xFF);
	print_port_status();
	wait_key();

	cout << "/STROBE: LOW (pin 1 asserted)\n";
	port_io::nstrobe();
	print_port_status();
	wait_key();

	cout << "/STROBE: HIGH (pin 1 deasserted)\n";
	port_io::strobe();
	print_port_status();
	wait_key();

	cout << "ACK: LOW\n";
	port_io::nack();
	print_port_status();
	wait_key();

	cout << "ACK: HIGH\n";
	port_io::ack();
	print_port_status();
	wait_key();

	cout << "BUSY: LOW\n";
	port_io::nbusy();
	print_port_status();
	wait_key();

	cout << "BUSY: HIGH\n";
	port_io::busy();
	print_port_status();
	wait_key();

	cout << "TRISTATE: ON (inputs)\n";
	port_io::tristate();
	print_port_status();
	wait_key();

	cout << "TRISTATE: OFF (outputs)\n";
	port_io::notristate();
	print_port_status();
	wait_key();

	cout << "Live status (press any key to exit):\n";
	while(!_kbhit()){
		port_io::tristate();
		cout << "[TRISTATE ON] ";
		print_port_status();
		Sleep(1000);
	}
	_getch();
	cout << "\nExiting test mode.\n";
}

int do_transfer(storage_device *source_dev, storage_device *destination_dev)
{
fz_header h_block;
byte buffer[1026];
int j;
	UINT prev_timer = 0;
	int result = 0;
	WORD old_attr = current_console_attr();
	clear_screen();
	prev_timer = timeBeginPeriod(1);
	SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	port_io::reset_missed_busy_count();
	result = source_dev->open();
	if(result != 0){
		if(result == 2){
			goto cleanup;
		}
		print_colored("Error: source open failed\n", kColorError);
		goto cleanup;
	}
	if((destination_dev->open()) != 0){
		print_colored("Error: destination open failed\n", kColorError);
		result = 1;
		goto cleanup;
	}
	if((source_dev->get_header(h_block)) != 0){
		print_colored("Error: get_header failed\n", kColorError);
		result = 1;
		goto cleanup;
	}
	cout << dec;
	clear_screen();
	print_colored("                           Transfer Preparation\n\n", kColorTitle);
	print_colored("Total Blocks: ", kColorWarning);
	cout << h_block.num_blocks;
	print_colored("PCM Blocks: ", kColorWarning);
	cout << h_block.num_waves;
	print_colored("  Banks: ", kColorWarning);
	cout << (int)h_block.num_banks;
	print_colored("  Voices: ", kColorWarning);
	cout << (int)h_block.num_voice << "\n";
	if((destination_dev->send_header(h_block)) != 0){
		print_colored("Error: send_header failed\n", kColorError);
		if(port_io::get_last_error() == 2){
			print_colored("Too many missed /BUSY events during transfer.\n", kColorError);
		}
		result = 1;
		goto cleanup;
	}
	cout << dec;
	clear_screen();
	print_colored("                               Sending To FZ\n\n", kColorTitle);
	print_colored("Blocks: ", kColorWarning);
	cout << h_block.num_blocks;
	print_colored("  Waves: ", kColorWarning);
	cout << h_block.num_waves;
	print_colored("  Banks: ", kColorWarning);
	cout << (int)h_block.num_banks;
	print_colored("  Voices: ", kColorWarning);
	cout << (int)h_block.num_voice << "\n\n";
	print_colored("Transfer Status\n\n", kColorTitle);
	for(j = 0;j < h_block.num_blocks;j++){
		int current = j + 1;
		if((source_dev->get_next_block(buffer)) != 0){
			result = 1;
			goto cleanup;
		}
		if((destination_dev->send_next_block(buffer)) != 0){
			if(port_io::get_last_error() == 2){
				print_colored("\nToo many missed /BUSY events during transfer.\n", kColorError);
			}
			result = 1;
			goto cleanup;
		}
		draw_progress_bar(current, h_block.num_blocks);
		if(_kbhit()){
			result = 1;
			goto cleanup;
		}
	}
	cout << "\n";
	print_colored("Status   ", kColorDim);
	print_colored("Transfer finished successfully.\n\n", kColorAccent);
	print_colored("Recovered /BUSY events: ", kColorDim);
	cout << port_io::get_missed_busy_count() << "\n";
	print_colored("Transfer complete.\n", kColorAccent);
	print_colored("Hit a key to continue...", kColorWarning);
	_getch();
	cout << "\n";
cleanup:
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	if(prev_timer != 0){
		timeEndPeriod(1);
	}
	set_console_attr(old_attr);
	return result;
}

int main(int argc, char *argv[])
{
storage_device *source_dev=NULL, *destination_dev=NULL;
int detect_port=0;
char s;

	if(argc > 1){
		pc_disk::set_browser_initial_dir(argv[1]);
	}else{
		pc_disk::set_browser_initial_dir(0);
	}

	clear_screen();
	print_colored("                                    FZ-DUMP V0.7\n", kColorTitle);
	cout << "                           (c) Jeff McClintock, 1996\n"
	"                    (c) Andrei Kudryavtsev, Deftaudio, 2026\n\n";
	print_colored("              WARNING: BETA SOFTWARE! - USE AT YOUR OWN RISK\n\n\n", kColorWarning);
	print_colored("Detecting hardware...\n", kColorWarning);
	if((detect_port = port_io::detect()) == 0){
	}else{
		print_colored("No printer port detected.\n", kColorError);
		return 0;
	}
	// clear_screen();
	print_colored("\nUsing Parallel LPT Port.\n", kColorAccent);
	cout << "\nTurn on External PORT on FZ-1: ";
	{
		WORD old_attr = current_console_attr();
		set_console_attr((WORD)(FOREGROUND_RED | FOREGROUND_INTENSITY));
		cout << "DATA DUMP -> SELECT DEVICE -> DUMP DEV = PORT\n";
		set_console_attr(old_attr);
	}
	set_console_attr(kColorWarning);
	cout << "\nHit a key";
	set_console_attr(kColorNormal);
	_getche();
	while( 1 ){

		clear_screen();
		print_colored("                                 FZ-DUMP V0.7\n", kColorTitle);
		cout << "                           (c) Jeff McClintock 1996\n"
	  "                    (c) Andrei Kudryavtsev, Deftaudio, 2026\n\n";
		print_colored("                WARNING: BETA SOFTWARE! - USE AT YOUR OWN RISK\n\n\n", kColorWarning);
		print_colored("                         Casio FZ sample dumper - PORT mode\n\n", kColorAccent);
		cout << "\n";
		print_colored("                             1 - Send Data to FZ\n", kColorAccent);
//		cout << "bd " <<  port_io::bidirectional;
		if(port_io::bidirectional == 1)
			print_colored("                             2 - Get sample from FZ (DON'T USE)", kColorAccent);
		print_colored("\n                             3 - Information\n", kColorAccent);
		print_colored("                             4 - LPT line test\n", kColorAccent);
		print_colored("                             5 - Send One File to FZ\n", kColorAccent);
		print_colored("                             q - quit\n\n", kColorAccent);
		print_colored("                    Choice? :", kColorWarning);
		s = _getche();
		clear_screen();

		switch (s){
		case '1':
			while(1){
				port_io browser_port;
				destination_dev = new fz_port(destination);
				source_dev = new pc_disk(source, 1);
				int transfer_result = do_transfer(source_dev,destination_dev);
				delete source_dev;
				delete destination_dev;
				browser_port.open_output();
				if(transfer_result == 2){
					break;
				}
				if(transfer_result != 0){
					print_hit_key_prompt();
					_getch();
				}
			}
			break;
		case '2':
			if(port_io::bidirectional == 0){
				break;
			}
			source_dev = new fz_port(source);
			destination_dev = new pc_disk(destination);
			if( do_transfer(source_dev,destination_dev) != 0){
				print_hit_key_prompt();
				_getch();
			}
			delete source_dev;
			delete destination_dev;
			break;
		case '3':
		     //	s=0;
		     //	while(s != 'Q' && s  != 'q' && s != 27){
			clear_screen();
			cout << "\nParallel Port     : ";
			if(detect_port == 0){
				cout << "DETECTED";
				if(port_io::bidirectional == 1)
					cout << " (bi-directional)";
				else{
					cout << "\nMONO-directional port\n"
					"only transfers PC -> FZ available";
				}
				cout << "\nport address      : " << hex << port_io::pp_data << dec;
			}else
				cout << "NOT DETECTED";
					     /*
			cout << "\n1 - Busy\n"
				"2 - No Busy\n"
				"3 - Ack\n"
				"4 - No Ack\n";  */
			s = _getche();
		    /*
			switch (s){
				case '1':
					port_io::busy();
					break;
				case '2':
					port_io::nbusy();
					break;
				case '3':
					port_io::ack();
					break;
				case '4':
					port_io::nack();
					break;
			}
			} */
			break;
		case '4':
			test_port_lines();
			break;
		case '5':
			destination_dev = new fz_port(destination);
			source_dev = new pc_disk(source, 0);
			if( do_transfer(source_dev,destination_dev) != 0){
				print_hit_key_prompt();
				_getch();
			}
			delete source_dev;
			delete destination_dev;
			break;
		case 'q':
		case 'Q':
		case 27: // <ESC>
			clear_screen();
			print_colored("Now Disconnect FZ Cable\n", kColorWarning);
			return 0;
		default:
			;
		}
	}
     //	return 0;
}
