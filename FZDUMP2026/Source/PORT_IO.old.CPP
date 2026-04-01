// Low level Printer port IO routines
// This file contains routines for sending and receiving single bytes from
// the FZ via high speed port



#include <iostream.h>
#include <conio.h>
#include <dos.h>
#include "port_io.h"

//#include <time.h>
	 /*
extern "C"
{
#include "jtimer.h"
}
	   */

#define INTR 0x0F
#define __CPPARGS ...
// who knows why

// define various signals that can be sent out port
#define STROBE	outportb(pp_status,1)
#define NSTROBE outportb(pp_status,0)
#define ACK     outportb(pp_status,inportb(pp_status)|4)
#define NACK    outportb(pp_status,inportb(pp_status)&251)
#define NBUSY   outportb(pp_status,inportb(pp_status)|8)
#define BUSY    outportb(pp_status,inportb(pp_status)&247)
#define TRISTATE    outportb(pp_status,inportb(pp_status)|32)
#define INT_ENBL    outportb(pp_status,inportb(pp_status)|16)
#define INT_DIS    outportb(pp_status,inportb(pp_status)&239)


unsigned int port_io::pp_data;
unsigned int port_io::pp_status;
unsigned int port_io::pp_control;
unsigned int port_io::bidirectional;
int byte_in; // byte recieved from FZ

void interrupt handler(__CPPARGS)
{

	outportb(port_io::pp_status,52);  // busy line hi
	byte_in = inportb(port_io::pp_data);  // read input byte
	//flag++;
	outportb(0x20,0x20);    //clear hardware interrupts

}

int port_io::in(void)
{
	int k = 4000;
 //	int timeout = 0;
	int save_int;
	byte_in = -1;
	NBUSY;
/*while(0==0){
	if(kbhit())
		return -1;
	cout << (int) inportb(pp_data) << ":" << inportb(pp_status) <<"\n";
} */
	while(byte_in < 0){
		if(k-- < 0){
			k = 4000; // crude timer
			if(kbhit())
				return -1;
		}
	}
	//flag = 0;
//cout << ">" << (int) inportb(pp_data) << "<";
	return byte_in;  // read input byte

}

port_io::port_io()
{
	save_int = 0;
}

port_io::detect()
{
	int j,byte_in1,byte_in2;
	unsigned int far *p;
	// determine port address
	pp_data = 0;
	p = (unsigned int far *) 0x411;
	j = ((*p) & 0xff) >> 6;
 //	printf("  number of ports = %d\n",j);
	if(j < 1){
		return 1;
	}
	p = (unsigned int far *)0x408;
	j = (*p);
	cout << "Printer port detected,  port address = " << hex << j << "\n";
	pp_data = j;
	pp_control = pp_data + 1;
	pp_status = pp_data + 2;
	NSTROBE;

	// Determine if port is bi-directional
	TRISTATE;
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

port_io::open_input()
{
//	if(detect() != 0)
  //		return 1;
//cout << "prepare for data in port\n";
	TRISTATE;     //tristate data lines
	ACK;
	NBUSY; // clear any rubbish from fz output
	delay(2);
	BUSY;
	outportb(0x20,0x20); // clear interrupt pending
//cout << "saving interupt flags\n";
	save_int=inportb(0x21); // save interupt flags
	setvect(INTR, handler);
//	outportb(0x21,inportb(0x21) & 127); //enable printer interupt (7)
	outportb(0x21,125); //enable printer interupt (7) and keyboard
	INT_ENBL;     //enable parallel port interrupt
//	delay(1);

	return 0;
}

port_io::~port_io()
{
	INT_DIS;     // disable parallel port interrupt
	if(save_int != 0){
		outportb(0x21,save_int); //allow interupts again
	}
	TRISTATE; // TRI-STATE PC
	NACK;  // TRI-STATE FZ
}

port_io::open_output()
{
   //	ACK;
   //	delay(1);
	NACK;
	NBUSY;
	NSTROBE;
	save_int = 0;
	return 0;
}

int port_io::out(int b)
{
int save_int;
// unsigned int timeout=0;

	save_int=inportb(0x21); // save interupt flags
	outportb(0x21,255); // disable interupts
	outportb(pp_data,b);  // put data on bus
    //	delay_usec(2);    // anything over 0 ok.
	BUSY;
	STROBE;
	// Wait for busy signal
//	while((inportb(pp_control) & 0x80)  != 0 && timeout++ < 30000 ){
	while((inportb(pp_control) & 0x80)  != 0 ){
	}
	NSTROBE;
	NBUSY;
//	ACK;
	outportb(0x21,save_int); //enable interupts

	if(kbhit())
		return -1;

	while((inportb(pp_control) & 0x80) == 0){
	}
 //	NACK;

	return 0;
}

port_io::ack()
{
	ACK;
	return 0;
}

port_io::nack()
{
	NACK;
	return 0;
}

port_io::busy()
{
	BUSY;
	return 0;
}

port_io::nbusy()
{
	NBUSY;
	return 0;
}
