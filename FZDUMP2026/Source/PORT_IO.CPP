// Low level Printer port IO routines
// This file contains routines for sending and receiving single bytes from
// the FZ via high speed port

#include <iostream>
#include <conio.h>
#include <windows.h>
#include "port_io.h"

using namespace std;

//#include <time.h>
	 /*
extern "C"
{
#include "jtimer.h"
}
	   */

// inpout32.dll accessors (Win9x/XP) loaded at runtime
typedef short (__stdcall *Inp32Fn)(short portaddr);
typedef void (__stdcall *Out32Fn)(short portaddr, short data);

static HMODULE g_inpout_dll = NULL;
static Inp32Fn g_inp32 = NULL;
static Out32Fn g_out32 = NULL;

static bool ensure_inpout_loaded()
{
	if(g_inp32 && g_out32)
		return true;
	if(!g_inpout_dll){
		g_inpout_dll = LoadLibraryA("inpout32.dll");
		if(!g_inpout_dll)
			return false;
	}
	g_inp32 = (Inp32Fn)GetProcAddress(g_inpout_dll, "Inp32");
	g_out32 = (Out32Fn)GetProcAddress(g_inpout_dll, "Out32");
	if(!g_inp32 || !g_out32){
		return false;
	}
	return true;
}

static __forceinline unsigned char inportb(unsigned short port)
{
	if(!ensure_inpout_loaded())
		return 0xFF;
	return (unsigned char)(g_inp32((short)port) & 0xFF);
}

static __forceinline void outportb(unsigned short port, unsigned char value)
{
	if(!ensure_inpout_loaded())
		return;
	g_out32((short)port, (short)value);
}

// Control register shadow to avoid unreliable read-modify-write
static unsigned char g_ctrl_shadow = 0x00;
static int g_strobe_active_high = 1;
static int g_last_error = 0;
static unsigned long g_missed_busy_count = 0;
static const unsigned long kMaxMissedBusyCount = 100000;

static void write_ctrl()
{
	outportb((unsigned short)port_io::pp_status, g_ctrl_shadow);
}

static void set_strobe_pin(int high)
{
	// Control bit 0 is inverted: 0 -> pin high, 1 -> pin low
	if(high){
		g_ctrl_shadow &= (unsigned char)~0x01;
	}else{
		g_ctrl_shadow |= 0x01;
	}
	write_ctrl();
}

// Control register bit helpers (use shadow)
// LPT strobe is inverted on the wire: bit0=0 drives pin1 high.
#define STROBE()     do { g_ctrl_shadow &= (unsigned char)~0x01; write_ctrl(); } while(0) /* pin1 high */
#define NSTROBE()    do { g_ctrl_shadow |= 0x01; write_ctrl(); } while(0) /* pin1 low */
#define ACK()        do { g_ctrl_shadow |= 0x04; write_ctrl(); } while(0)
#define NACK()       do { g_ctrl_shadow &= (unsigned char)~0x04; write_ctrl(); } while(0)
#define NBUSY()      do { g_ctrl_shadow |= 0x08; write_ctrl(); } while(0)
#define BUSY()       do { g_ctrl_shadow &= (unsigned char)~0x08; write_ctrl(); } while(0)
#define TRISTATE()   do { g_ctrl_shadow |= 0x20; write_ctrl(); } while(0)
#define NOTRISTATE() do { g_ctrl_shadow &= (unsigned char)~0x20; write_ctrl(); } while(0)
#define INT_ENBL()   do { g_ctrl_shadow |= 0x10; write_ctrl(); } while(0)
#define INT_DIS()    do { g_ctrl_shadow &= (unsigned char)~0x10; write_ctrl(); } while(0)

// DOS-exact strobe pulse: writes raw control values 1 then 0, then restores shadow
static void strobe_dos_pulse(unsigned int ms)
{
	set_strobe_pin(g_strobe_active_high ? 1 : 0);
	Sleep(ms);
	set_strobe_pin(g_strobe_active_high ? 0 : 1);
}

static unsigned long long g_loops_per_us = 0;

static void calibrate_loops_per_us(void)
{
	if(g_loops_per_us != 0){
		return;
	}
	LARGE_INTEGER freq, start, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
	const unsigned long long target_us = 1000000; // 1 second minimum calibration window
	volatile unsigned int sink = 0;
	unsigned long long loops = 0;
	while(1){
		// Batch increments to reduce QPC overhead skew
		for(int i = 0; i < 8192; ++i){
			sink++;
			loops++;
		}
		QueryPerformanceCounter(&now);
		unsigned long long elapsed_us =
			(unsigned long long)(now.QuadPart - start.QuadPart) * 1000000ULL / (unsigned long long)freq.QuadPart;
		if(elapsed_us >= target_us){
			if(elapsed_us == 0){
				elapsed_us = 1;
			}
			g_loops_per_us = loops / elapsed_us;
			if(g_loops_per_us == 0){
				g_loops_per_us = 1;
			}
			break;
		}
	}
	(void)sink;
}

static void busy_wait_us(unsigned int us)
{
	if(g_loops_per_us == 0){
		calibrate_loops_per_us();
	}
	volatile unsigned long long sink = 0;
	unsigned long long loops = (unsigned long long)us * g_loops_per_us;
	for(unsigned long long i = 0; i < loops; ++i){
		sink++;
	}
	(void)sink;
}

static unsigned long long loops_for_us(unsigned int us)
{
	if(g_loops_per_us == 0){
		calibrate_loops_per_us();
	}
	return (unsigned long long)us * g_loops_per_us;
}

static int wait_mask_change_spin(unsigned char mask, unsigned char prev)
{
	unsigned int i;
	for(i = 0; i < 200000; ++i){
		if((inportb((unsigned short)port_io::pp_control) & mask) != prev)
			return 1;
	}
	return 0;
}

static int wait_mask_value_spin(unsigned char mask, unsigned char value)
{
	unsigned int i;
	for(i = 0; i < 200000; ++i){
		if((inportb((unsigned short)port_io::pp_control) & mask) == value)
			return 1;
	}
	return 0;
}


unsigned int port_io::pp_data;
unsigned int port_io::pp_status;
unsigned int port_io::pp_control;
unsigned int port_io::bidirectional;
static const unsigned short kCandidatePorts[] = { 0x378, 0x3BC, 0x278 };

#if 0
static void dump_port_state(const char *tag)
{
	unsigned char status = inportb((unsigned short)port_io::pp_control);
	unsigned char control = inportb((unsigned short)port_io::pp_status);
	cout << "\n[" << tag << "] status=0x" << hex << (int)status
		 << " control=0x" << hex << (int)control
		 << " shadow=0x" << hex << (int)g_ctrl_shadow << dec;
	cout << " busy=" << (((status & 0x80) == 0) ? 1 : 0);
	cout << " ack=" << (((status & 0x40) == 0) ? 1 : 0);
}
#endif

// (removed ms-based waits)

static int wait_status_zero(unsigned char mask, DWORD timeout_ms)
{
	DWORD start = GetTickCount();
	while((inportb((unsigned short)port_io::pp_control) & mask) != 0){
		if(_kbhit())
			return 0;
		if(GetTickCount() - start >= timeout_ms)
			return 0;
	}
	return 1;
}

static int wait_status_one(unsigned char mask, DWORD timeout_ms)
{
	DWORD start = GetTickCount();
	while((inportb((unsigned short)port_io::pp_control) & mask) == 0){
		if(_kbhit())
			return 0;
		if(GetTickCount() - start >= timeout_ms)
			return 0;
	}
	return 1;
}

int port_io::in(void)
{
	int k = 4000;
	int value = -1;

	NBUSY();
	while(value < 0){
		if((inportb((unsigned short)pp_control) & 0x80) == 0){
			value = (int)inportb((unsigned short)pp_data);
			break;
		}
		if(k-- < 0){
			k = 4000;
			if(_kbhit())
				return -1;
		}
	}
	return value & 0xFF;

}

port_io::port_io()
{
}

int port_io::detect()
{
	int j,byte_in1,byte_in2;
	unsigned short base = 0;

	if(!ensure_inpout_loaded()){
		cout << "inpout32.dll not found or missing exports.\n";
		return 1;
	}

	// Try common LPT base addresses; accept the first that doesn't read as 0xFF.
	for(j = 0; j < (int)(sizeof(kCandidatePorts) / sizeof(kCandidatePorts[0])); j++){
		unsigned short candidate = kCandidatePorts[j];
		unsigned char status = inportb(candidate + 1);
		if(status != 0xFF){
			base = candidate;
			break;
		}
	}

	if(base == 0){
		return 1;
	}

	cout << "Printer port detected,  port address = " << hex << base << dec << "\n";
	pp_data = base;
	pp_control = pp_data + 1; // status register (read)
	pp_status = pp_data + 2;  // control register (write)
	g_ctrl_shadow = 0x00;
	reset_lines();
	calibrate_loops_per_us();
	cout << "Calibrated loops/us = " << dec << g_loops_per_us << "\n";

	// Determine if port is bi-directional
	TRISTATE();
	outportb(pp_data,0);  // put data on bus
	byte_in1 = inportb(port_io::pp_data);  // read input byte
	outportb(pp_data,255);  // put data on bus
	byte_in2 = inportb(port_io::pp_data);  // read input byte
	if(byte_in1 == byte_in2)
		bidirectional = 1;
	else
		bidirectional = 0;
//cout << "b1 = " << byte_in1 << "  b2 = " << byte_in2 << "\n";
//getch();
	return 0;
}

void port_io::reset_lines(void)
{
	// Safe default: tri-state data lines, deassert ACK/BUSY/STROBE
	TRISTATE();
	NACK();  // pin16 low (default)
	NBUSY(); // pin17 low (default)
	NSTROBE(); // idle low (strobe inactive)
	outportb(pp_data, 0xFF);
}

int port_io::open_input()
{
	// Prepare PC to receive data (FZ -> PC)
	TRISTATE();     // tristate data lines
	ACK();
	NBUSY(); // clear any rubbish from fz output
	Sleep(2);
	BUSY();

	return 0;
}

port_io::~port_io()
{
	TRISTATE(); // TRI-STATE PC
	NACK();  // TRI-STATE FZ
}

int port_io::open_output()
{
   //	ACK;
   //	delay(1);
	NOTRISTATE(); // drive data lines (output mode)
	NACK();   // pin16 low (default)
	NBUSY();  // pin17 low (default)
	NSTROBE(); // idle low (strobe inactive)
	outportb(pp_data, 0xFF);
	return 0;
}

static int strobe_busy_handshake(unsigned int strobe_us)
{
	static const unsigned char kHandshakeMask = 0x80; // BUSY only
	unsigned char pre = (unsigned char)(inportb((unsigned short)port_io::pp_control) & kHandshakeMask);
	set_strobe_pin(1);
	if(strobe_us > 0){
		busy_wait_us(strobe_us);
	}
	set_strobe_pin(0);
	if(!wait_mask_change_spin(kHandshakeMask, pre)){
		return 0;
	}
	NBUSY();
	if(!wait_mask_value_spin(kHandshakeMask, pre)){
		return 0;
	}
	return 1;
}

static __forceinline int pre_poll_busy_active(unsigned short status_port,
	unsigned int poll_count)
{
	unsigned int i;
	if(poll_count == 0){
		poll_count = 1;
	}
	for(i = 0; i < poll_count; ++i){
		unsigned char s = inportb(status_port);
		if((s & 0x80) == 0){
			return 1;
		}
	}
	return 0;
}

static __forceinline int wait_busy_active_window(unsigned short status_port,
	unsigned int poll_groups)
{
	unsigned int spin;
	if(poll_groups == 0){
		poll_groups = 1;
	}
	for(spin = 0; spin < poll_groups; ++spin){
		unsigned char s0 = inportb(status_port);
		if((s0 & 0x80) == 0){
			return 1;
		}
		unsigned char s1 = inportb(status_port);
		if((s1 & 0x80) == 0){
			return 1;
		}
		unsigned char s2 = inportb(status_port);
		if((s2 & 0x80) == 0){
			return 1;
		}
		unsigned char s3 = inportb(status_port);
		if((s3 & 0x80) == 0){
			return 1;
		}
		unsigned char s4 = inportb(status_port);
		if((s4 & 0x80) == 0){
			return 1;
		}
		unsigned char s5 = inportb(status_port);
		if((s5 & 0x80) == 0){
			return 1;
		}
		unsigned char s6 = inportb(status_port);
		if((s6 & 0x80) == 0){
			return 1;
		}
		unsigned char s7 = inportb(status_port);
		if((s7 & 0x80) == 0){
			return 1;
		}
	}
	return 0;
}

static __forceinline void wait_busy_inactive_forever(unsigned short status_port)
{
	while(1){
		unsigned char s0 = inportb(status_port);
		if((s0 & 0x80) != 0){
			return;
		}
		unsigned char s1 = inportb(status_port);
		if((s1 & 0x80) != 0){
			return;
		}
		unsigned char s2 = inportb(status_port);
		if((s2 & 0x80) != 0){
			return;
		}
		unsigned char s3 = inportb(status_port);
		if((s3 & 0x80) != 0){
			return;
		}
	}
}

static __forceinline int wait_second_busy_window(unsigned short status_port,
	unsigned int poll_groups)
{
	unsigned int spin;
	if(poll_groups == 0){
		poll_groups = 1;
	}
	for(spin = 0; spin < poll_groups; ++spin){
		unsigned char s0 = inportb(status_port);
		if((s0 & 0x80) == 0){
			return 1;
		}
		unsigned char s1 = inportb(status_port);
		if((s1 & 0x80) == 0){
			return 1;
		}
/*
		unsigned char s2 = inportb(status_port);
		if((s2 & 0x80) == 0){
			return 1;
		}
		unsigned char s3 = inportb(status_port);
		if((s3 & 0x80) == 0){
			return 1;
		}
*/

	}
	return 0;
}

int port_io::out(int b)
{
	static const unsigned int kPreBusyPollCount = 0;
	static const unsigned int kBusyDetectPollGroups = 5;
	static const unsigned int kStrobeHighUs = 0;
	static const unsigned int kDataHoldUs = 0;
	static const unsigned int kRetryBusyPollGroups = 5;
	static const unsigned int kMaxRetryCount = 10;
	static const unsigned int kBusyFallPollGroups = 10;
	const unsigned short status_port = (unsigned short)pp_control;
	const unsigned short data_port = (unsigned short)pp_data;
	unsigned int retry_count = 0;
	int saw_busy = 0;
	unsigned int fall_spin;

	NOTRISTATE(); // ensure data lines are outputs
	outportb(data_port, 0xFF); // idle bus before strobe
	// Generate short active-high strobe pulse
	STROBE();

	//moved to hear <<<
	outportb(data_port, (unsigned char)b);


	if(kStrobeHighUs > 0){
		busy_wait_us(kStrobeHighUs);
	}
	NSTROBE();

	saw_busy = pre_poll_busy_active(status_port, kPreBusyPollCount);

// moved from hear >>>

	while(1){
		if(kDataHoldUs > 0){
			busy_wait_us(kDataHoldUs);
		}

		if(!saw_busy){
			saw_busy = wait_busy_active_window(status_port, kBusyDetectPollGroups);
		}
		if(saw_busy){
			for(fall_spin = 0; fall_spin < kBusyFallPollGroups; ++fall_spin){
				unsigned char s0 = inportb(status_port);
				if((s0 & 0x80) != 0){
					break;
				}
				unsigned char s1 = inportb(status_port);
				if((s1 & 0x80) != 0){
					break;
				}
				unsigned char s2 = inportb(status_port);
				if((s2 & 0x80) != 0){
					break;
				}
				unsigned char s3 = inportb(status_port);
				if((s3 & 0x80) != 0){
					break;
				}
			}
		}else{
			g_missed_busy_count++;
			if(g_missed_busy_count > kMaxMissedBusyCount){
				g_last_error = 2;
				outportb(data_port, 0xFF);
				return 1;
			}
		}

		// Return bus to idle after data hold, then allow one quick same-byte replay
		outportb(data_port, 0xFF);

		if(retry_count >= kMaxRetryCount){
			break;
		}

		if(!wait_second_busy_window(status_port, kRetryBusyPollGroups)){
			break;
		}

		retry_count++;
		saw_busy = 1;
		outportb(data_port, (unsigned char)b);
	}

	g_last_error = 0;
	return 0;
}

int port_io::ack()
{
	ACK();
	return 0;
}

int port_io::nack()
{
	NACK();
	return 0;
}

int port_io::busy()
{
	BUSY();
	return 0;
}

int port_io::nbusy()
{
	NBUSY();
	return 0;
}

void port_io::header_preamble(unsigned int strobe_us, unsigned int ackbusy_us, unsigned int gap_us)
{
	NOTRISTATE();
	outportb(pp_data, 0x00);
	// Default /ACKO and /BUSYO low, then pulse them high during strobe
	NACK();  // pin16 low
	NBUSY(); // pin17 low
	STROBE(); // keep /STBO HIGH during entire preamble
	// Raise /ACKO and /BUSYO for ackbusy_us
	ACK();   // pin16 high
	BUSY();  // pin17 high
	busy_wait_us(ackbusy_us);
	// Return /ACKO and /BUSYO low
	NACK();
	NBUSY();
	// Return data bus to idle immediately after /ACKO and /BUSYO release
	outportb(pp_data, 0xFF);
	if(gap_us > 0){
		busy_wait_us(gap_us);
	}
}

int port_io::get_last_error(void)
{
	return g_last_error;
}

void port_io::clear_last_error(void)
{
	g_last_error = 0;
}

unsigned long port_io::get_missed_busy_count(void)
{
	return g_missed_busy_count;
}

void port_io::reset_missed_busy_count(void)
{
	g_missed_busy_count = 0;
}

unsigned long long port_io::get_loops_per_us(void)
{
	if(g_loops_per_us == 0){
		calibrate_loops_per_us();
	}
	return g_loops_per_us;
}

int port_io::strobe()
{
	STROBE();
	return 0;
}

int port_io::nstrobe()
{
	NSTROBE();
	return 0;
}

void port_io::pulse_strobe(unsigned int ms)
{
	STROBE();
	Sleep(ms);
	NSTROBE();
}

void port_io::send_test_byte(unsigned char v, unsigned int strobe_ms)
{
	NOTRISTATE();
	outportb(pp_data, v);
	BUSY();
	strobe_dos_pulse(strobe_ms);
	NBUSY();
}

void port_io::send_test_byte_us(unsigned char v, unsigned int strobe_us)
{
	NOTRISTATE();
	outportb(pp_data, v);
	set_strobe_pin(g_strobe_active_high ? 1 : 0);
	busy_wait_us(strobe_us);
	set_strobe_pin(g_strobe_active_high ? 0 : 1);
}

void port_io::set_strobe_polarity(int active_high)
{
	g_strobe_active_high = active_high ? 1 : 0;
}

int port_io::get_strobe_polarity(void)
{
	return g_strobe_active_high;
}

int port_io::tristate()
{
	TRISTATE();
	return 0;
}

int port_io::notristate()
{
	NOTRISTATE();
	return 0;
}

void port_io::write_data(unsigned char v)
{
	outportb(pp_data, v);
}

unsigned char port_io::read_status(void)
{
	return inportb((unsigned short)pp_control);
}

unsigned char port_io::read_control(void)
{
	return inportb((unsigned short)pp_status);
}

unsigned char port_io::read_data(void)
{
	return inportb((unsigned short)pp_data);
}
