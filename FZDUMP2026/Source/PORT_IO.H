// Storage device header
#if !defined(_port_h_)
#define _port_h_

class port_io
{
public:
	port_io();
	~port_io();
	static int detect(void);
	static void reset_lines(void);
	static int ack(void);
	static int nack(void);
	static int busy(void);
	static int nbusy(void);
	static int strobe(void);
	static int nstrobe(void);
	static void pulse_strobe(unsigned int ms);
	static void send_test_byte(unsigned char v, unsigned int strobe_ms);
	static void send_test_byte_us(unsigned char v, unsigned int strobe_us);
	static void set_strobe_polarity(int active_high);
	static int get_strobe_polarity(void);
	static int tristate(void);
	static int notristate(void);
	static void header_preamble(unsigned int strobe_us, unsigned int ackbusy_us, unsigned int gap_us);
	static void write_data(unsigned char v);
	static unsigned char read_status(void);
	static unsigned char read_control(void);
	static unsigned char read_data(void);
	static int get_last_error(void);
	static void clear_last_error(void);
	static unsigned long get_missed_busy_count(void);
	static void reset_missed_busy_count(void);
	static unsigned long long get_loops_per_us(void);
	int open_input(void);
	int open_output(void);
	int in(void);
	int out(int b);
	static unsigned int pp_data;
	static unsigned int pp_status;
	static unsigned int pp_control;
	static unsigned int bidirectional;
private:
};
#endif
