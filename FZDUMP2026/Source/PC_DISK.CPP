// These routines save and load samples to disk (on PC).  It handles 3 file
// formats,  - FZEDIT compatile,
//           - Windows WAVE files,
//           - FZDUMP beta format (obsolete)


#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include <string.h>
#include <conio.h>
#include <direct.h>
#include <windows.h>
#include "st_dev.h"
#include "casio.h"
#include "wave.h"

using namespace std;

// Disk header format (from FZCOM shareware)

struct fz_file_header {
	long            indicator;       /* FZCom file indicator           00 */
	short           version;         /* FZCom file format version no.  04 */
	unsigned char   status;          /* Type of file (F:0,B:1,V:2,E:3) 05 */
	unsigned char   bn;              /* Number of banks in this file   06 */
	unsigned char   vn;              /* Number of voices in this file  07 */
	unsigned char   dum2;		 // not used
	short           block_count;     /* Number of blocks in the file   08 */
	short           wn_block_count;  /* Number of PCM data blocks      0A */
	short		unused;          /* Reserved                       0C */

};

struct browser_entry {
	string name;
	string full_path;
	int is_dir;
	unsigned long size_kb;
};

struct browser_dir_state {
	string dir;
	int selected;
};

static string g_browser_initial_dir;

static string browser_app_dir()
{
	char module_path[MAX_PATH];
	char *last_sep;

	module_path[0] = 0;
	if(GetModuleFileNameA(NULL, module_path, MAX_PATH) == 0){
		return ".";
	}
	last_sep = strrchr(module_path, '\\');
	if(last_sep == 0){
		last_sep = strrchr(module_path, '/');
	}
	if(last_sep != 0){
		*last_sep = 0;
	}
	return module_path[0] ? module_path : ".";
}

static string browser_resolve_start_dir(const char *path)
{
	char full_path[MAX_PATH];
	DWORD attrs;

	if(path == 0 || *path == 0){
		return browser_app_dir();
	}
	if(GetFullPathNameA(path, MAX_PATH, full_path, NULL) == 0){
		return browser_app_dir();
	}
	attrs = GetFileAttributesA(full_path);
	if(attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0){
		return browser_app_dir();
	}
	return full_path;
}

static WORD browser_default_attr()
{
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if(h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info)){
		return info.wAttributes;
	}
	return (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

static void browser_set_attr(WORD attr)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if(h != INVALID_HANDLE_VALUE){
		SetConsoleTextAttribute(h, attr);
	}
}

static void browser_clear_screen()
{
	system("cls");
}

static int browser_console_size(int &width, int &height)
{
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if(h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &info)){
		width = 80;
		height = 25;
		return 1;
	}
	width = info.srWindow.Right - info.srWindow.Left + 1;
	height = info.srWindow.Bottom - info.srWindow.Top + 1;
	return 0;
}

static string browser_join_path(const string &dir, const string &name)
{
	if(dir.empty()){
		return name;
	}
	if(dir[dir.size() - 1] == '\\' || dir[dir.size() - 1] == '/'){
		return dir + name;
	}
	return dir + "\\" + name;
}

static string browser_parent_path(const string &path)
{
	size_t end = path.size();
	while(end > 0 && (path[end - 1] == '\\' || path[end - 1] == '/')){
		end--;
	}
	if(end <= 3){
		return path.substr(0, end);
	}
	size_t pos = path.find_last_of("\\/", end - 1);
	if(pos == string::npos){
		return path;
	}
	if(pos <= 2){
		return path.substr(0, pos + 1);
	}
	return path.substr(0, pos);
}

static int browser_is_root_path(const string &path)
{
	return path.size() <= 3;
}

static int browser_entry_compare(const browser_entry &a, const browser_entry &b)
{
	if(a.is_dir != b.is_dir){
		return a.is_dir > b.is_dir;
	}
	return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
}

static int browser_is_supported_load_file(const char *name)
{
	const char *ext = strrchr(name, '.');
	if(ext == 0 || *(ext + 1) == 0){
		return 0;
	}
	ext++;
	return (_stricmp(ext, "fzf") == 0 ||
		_stricmp(ext, "fzb") == 0 ||
		_stricmp(ext, "fzv") == 0 ||
		_stricmp(ext, "fze") == 0 ||
		_stricmp(ext, "wav") == 0);
}

static int browser_find_saved_selection(const vector<browser_dir_state> &states,
	const string &dir)
{
	size_t i;
	for(i = 0; i < states.size(); ++i){
		if(_stricmp(states[i].dir.c_str(), dir.c_str()) == 0){
			return states[i].selected;
		}
	}
	return 0;
}

static void browser_save_selection(vector<browser_dir_state> &states,
	const string &dir,
	int selected)
{
	size_t i;
	for(i = 0; i < states.size(); ++i){
		if(_stricmp(states[i].dir.c_str(), dir.c_str()) == 0){
			states[i].selected = selected;
			return;
		}
	}
	browser_dir_state state;
	state.dir = dir;
	state.selected = selected;
	states.push_back(state);
}

static int browser_load_directory(const string &dir, vector<browser_entry> &entries)
{
	WIN32_FIND_DATAA fd;
	HANDLE h;
	string pattern = browser_join_path(dir, "*");

	entries.clear();
	h = FindFirstFileA(pattern.c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE){
		return 1;
	}

	do{
		browser_entry entry;
		if(strcmp(fd.cFileName, ".") == 0){
			continue;
		}
		if(strcmp(fd.cFileName, "..") == 0){
			continue;
		}
		entry.name = fd.cFileName;
		entry.full_path = browser_join_path(dir, fd.cFileName);
		entry.is_dir = ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) ? 1 : 0;
		if(entry.is_dir){
			entry.size_kb = 0;
		}else{
			unsigned long long size_bytes =
				((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
			entry.size_kb = (unsigned long)((size_bytes + 1023ULL) / 1024ULL);
		}
		if(!entry.is_dir && !browser_is_supported_load_file(fd.cFileName)){
			continue;
		}
		entries.push_back(entry);
	}while(FindNextFileA(h, &fd));
	FindClose(h);

	sort(entries.begin(), entries.end(), browser_entry_compare);

	if(!browser_is_root_path(dir)){
		browser_entry up_entry;
		up_entry.name = "..";
		up_entry.full_path = browser_parent_path(dir);
		up_entry.is_dir = 1;
		entries.insert(entries.begin(), up_entry);
	}
	return 0;
}

static string browser_shorten_name(const browser_entry &entry, int width)
{
	string label = entry.name;
	if(entry.is_dir){
		if(entry.name == ".."){
			label = "[..]";
		}else{
			label = "[" + entry.name + "]";
		}
	}
	if((int)label.size() > width){
		if(width > 3){
			label = label.substr(0, width - 3) + "...";
		}else{
			label = label.substr(0, width);
		}
	}
	return label;
}

static string browser_size_text(const browser_entry &entry)
{
	char temp[32];
	if(entry.is_dir){
		return "";
	}
	sprintf(temp, "%luK", entry.size_kb);
	return temp;
}

static WORD browser_entry_attr(const browser_entry &entry, int selected)
{
	if(selected){
		return (WORD)(BACKGROUND_BLUE | BACKGROUND_GREEN |
			FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}
	if(entry.is_dir){
		return (WORD)(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}
	{
		const char *ext = strrchr(entry.name.c_str(), '.');
		if(ext != 0){
			ext++;
			if(_stricmp(ext, "fzf") == 0){
				return (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
			}
			if(_stricmp(ext, "fzb") == 0){
				return (WORD)(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
			}
			if(_stricmp(ext, "fzv") == 0){
				return (WORD)(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
			}
			if(_stricmp(ext, "fze") == 0){
				return (WORD)(FOREGROUND_RED | FOREGROUND_INTENSITY);
			}
			if(_stricmp(ext, "wav") == 0){
				return (WORD)(FOREGROUND_GREEN | FOREGROUND_BLUE);
			}
		}
	}
	return browser_default_attr();
}

static void build_fz_voice_name(const char *path, char *dest, int dest_size)
{
	const char *base;
	const char *ext;
	int i;
	int out = 0;

	if(dest_size <= 0){
		return;
	}

	memset(dest, ' ', dest_size);

	base = strrchr(path, '\\');
	if(base == 0){
		base = strrchr(path, '/');
	}
	base = (base == 0) ? path : (base + 1);

	ext = strrchr(base, '.');
	if(ext == 0){
		ext = base + strlen(base);
	}

	for(i = 0; base + i < ext && out < 12 && out < dest_size; ++i){
		unsigned char ch = (unsigned char)base[i];
		if(isalnum(ch)){
			dest[out++] = (char)toupper(ch);
		}else if(ch == ' ' || ch == '_' || ch == '-'){
			dest[out++] = (char)ch;
		}else{
			dest[out++] = ' ';
		}
	}
}

static void browser_draw(const string &dir,
	const vector<browser_entry> &entries,
	int selected,
	int page_start)
{
	int width, height;
	int usable_rows, col_width, cols, page_size;
	int i, idx, row, col;
	WORD default_attr = browser_default_attr();

	browser_console_size(width, height);
	usable_rows = height - 8;
	if(usable_rows < 5){
		usable_rows = 5;
	}
	col_width = 28;
	if(width < 60){
		col_width = 20;
	}
	cols = (width - 2) / col_width;
	if(cols < 1){
		cols = 1;
	}
	page_size = usable_rows * cols;

	browser_clear_screen();
	cout << "Select file to send to FZ\n\n";
	cout << "Path: " << dir << "\n";
	cout << "Arrows: move   ENTER: open dir/send file   BACKSPACE: up   ESC: cancel\n\n";

	for(row = 0; row < usable_rows; ++row){
		for(col = 0; col < cols; ++col){
			idx = page_start + col * usable_rows + row;
			if(idx >= (int)entries.size()){
				continue;
			}
			browser_set_attr(browser_entry_attr(entries[idx], idx == selected));
			string size_text = browser_size_text(entries[idx]);
			int name_width = col_width - 4;
			if(!size_text.empty()){
				name_width -= ((int)size_text.size() + 1);
			}
			if(name_width < 6){
				name_width = 6;
			}
			string label = browser_shorten_name(entries[idx], name_width);
			if(idx == selected){
				cout << ">";
			}else{
				cout << " ";
			}
			cout << label;
			for(i = (int)label.size(); i < name_width; ++i){
				cout << " ";
			}
			if(!size_text.empty()){
				cout << " " << size_text;
				for(i = name_width + 1 + (int)size_text.size(); i < col_width - 2; ++i){
					cout << " ";
				}
			}else{
				for(i = name_width; i < col_width - 2; ++i){
					cout << " ";
				}
			}
			browser_set_attr(default_attr);
		}
		cout << "\n";
	}
	browser_set_attr(default_attr);
	if(entries.empty()){
		cout << "(empty directory)\n";
	}
	cout << dec;
	cout << "\nItem " << (entries.empty() ? 0 : (selected + 1))
		 << " of " << entries.size();
	if(page_size < (int)entries.size()){
		cout << "   Page " << (page_start / page_size + 1)
			 << " of " << ((entries.size() + page_size - 1) / page_size);
	}
	cout << "\n";
}

static int browser_pick_file(char *out_path, int out_size)
{
	static int browser_state_initialized = 0;
	static string current_dir;
	static vector<browser_dir_state> states;
	vector<browser_entry> entries;
	int selected = 0;
	int page_start = 0;

	if(!browser_state_initialized){
		current_dir = g_browser_initial_dir.empty() ? browser_app_dir() : g_browser_initial_dir;
		browser_state_initialized = 1;
	}

	selected = browser_find_saved_selection(states, current_dir);

	while(1){
		int width, height, usable_rows, col_width, cols, page_size;
		int ch;

		if(browser_load_directory(current_dir, entries) != 0){
			browser_clear_screen();
			cout << "Unable to open directory:\n" << current_dir << "\n\nHit a key.";
			_getch();
			return 1;
		}
		if(selected >= (int)entries.size()){
			selected = entries.empty() ? 0 : (int)entries.size() - 1;
		}
		if(selected < 0){
			selected = 0;
		}

		browser_console_size(width, height);
		usable_rows = height - 8;
		if(usable_rows < 5){
			usable_rows = 5;
		}
		col_width = (width < 60) ? 20 : 28;
		cols = (width - 2) / col_width;
		if(cols < 1){
			cols = 1;
		}
		page_size = usable_rows * cols;
		if(page_size < 1){
			page_size = 1;
		}
		page_start = (selected / page_size) * page_size;

		browser_draw(current_dir, entries, selected, page_start);
		ch = _getch();
		if(ch == 27){
			return 1;
		}
		if(ch == 8){
			if(!browser_is_root_path(current_dir)){
				browser_save_selection(states, current_dir, selected);
				current_dir = browser_parent_path(current_dir);
				selected = browser_find_saved_selection(states, current_dir);
			}
			continue;
		}
		if(ch == 13){
			if(entries.empty()){
				continue;
			}
			if(entries[selected].is_dir){
				browser_save_selection(states, current_dir, selected);
				current_dir = entries[selected].full_path;
				selected = browser_find_saved_selection(states, current_dir);
			}else{
				browser_save_selection(states, current_dir, selected);
				strncpy(out_path, entries[selected].full_path.c_str(), out_size - 1);
				out_path[out_size - 1] = 0;
				return 0;
			}
			continue;
		}
		if(ch == 0 || ch == 224){
			ch = _getch();
			switch(ch){
			case 72:
				if(selected > 0){
					selected--;
				}
				break;
			case 80:
				if(selected + 1 < (int)entries.size()){
					selected++;
				}
				break;
			case 75:
				if(selected - usable_rows >= 0){
					selected -= usable_rows;
				}
				break;
			case 77:
				if(selected + usable_rows < (int)entries.size()){
					selected += usable_rows;
				}
				break;
			case 71:
				selected = 0;
				break;
			case 79:
				if(!entries.empty()){
					selected = (int)entries.size() - 1;
				}
				break;
			case 73:
				selected -= page_size;
				if(selected < 0){
					selected = 0;
				}
				break;
			case 81:
				selected += page_size;
				if(selected >= (int)entries.size() && !entries.empty()){
					selected = (int)entries.size() - 1;
				}
				break;
			}
		}
	}
}


pc_disk::pc_disk(fz_direction d, int use_browser_mode_in)
	: storage_device( d )
{
	f1= NULL;
	use_browser_mode = use_browser_mode_in;
}

void pc_disk::set_browser_initial_dir(const char *path)
{
	g_browser_initial_dir = browser_resolve_start_dir(path);
}

int pc_disk::open()
{

	block_num = 0;
	cout << "\n\n\n\n\n\n\n\n\n\n\n\n";
	if(direction == destination){
		cout << "Saving FZ Sound to Hard Disk.\n";
		cout << "Use an extension of .WAV if you want sound stored as a wave file\n\n";
		cout << "Filename? :";
		cin >> file_name;
	}else{
		cout << "Sending Sound to FZ.\n";
		if(use_browser_mode){
			if(browser_pick_file(file_name, sizeof(file_name)) != 0){
				return 2;
			}
		}else{
			cout << "Type Filename (include extension)? :";
			cin >> file_name;
		}
	}
	return 0;
}

pc_disk::~pc_disk()
{
	if(f1 != NULL){
		fclose(f1);
	}
}

int pc_disk::skip_bytes(FILE *f,long n)
{
long x;

	for(x=0;x < n;x++){
		fgetc(f);
	}
	return 0;
}

int pc_disk::read_int(FILE *f)
{
unsigned int t,w;

	t=fgetc(f);
	w=fgetc(f);
	w=w<<8;
	w+=t;
	return w;
 }

// read a 4 byte integer from wav file
long pc_disk::read_long(FILE *f)
{
int x,j[4];
unsigned long l=0;
	for(x=3;x>=0;x--){
		j[x]=fgetc(f);
	}

	for(x=0;x<4;x++){
		l=l<<8;
		l+=j[x];
	}
	return l;
}

int pc_disk::get_header(fz_header &fzh)
{
	char file_ext[10],chunk_id[6];
	int j,skip_it;
	long chunk_size;
	char *k;
	long temp_long;
	fz_file_header fh;
	fpos_t pos;


	// get file extension
	k = strrchr(file_name,'.');
	if(k==0){ // if no file extension given,assume .fzb
		strcat(file_name,".fzb");
		strcpy(file_ext,"fzb");
	}else{
		strcpy(file_ext,k+1);
	}
	if((f1 = fopen (file_name,"rb"))==NULL){
		cout << "Could't find file " << file_name << "\n";
		return 1;
	}
	file_type = fz;
	if(_stricmp(file_ext,"fzf")==0 || _stricmp(file_ext,"fzb")==0 ||
	   _stricmp(file_ext,"fzv")==0 || _stricmp(file_ext,"fze")==0){
		// one of expected file types


		fseek( f1, 1000L, SEEK_SET );  // find header in file
		if(fread(&fh, sizeof(fh), 1, f1) != 1){
			puts("Can't read file header" );
			return 1;
		}
		if( fgetpos( f1, &pos ) != 0 )
			j=-1;

		fseek( f1, 0L, SEEK_SET );  // Start of file
		if(fh.indicator != 476549224){ // FZCOM file header magic number
			// assume old FZDUMP file header
			printf("reading beta file header\n" );
			if(fread(&fzh, 16, 1, f1) != 1){
				puts("Can't read beta file header" );
				return 1;
			}
			// fix up data packing problems
			fzh.num_blocks = fzh.num_blocks_hi;
			fzh.num_blocks = (fzh.num_blocks << 8) + fzh.num_blocks_lo;
			fzh.num_waves = fzh.num_waves_hi;
			fzh.num_waves = (fzh.num_waves << 8) + fzh.num_waves_lo;

		}else{
		// Convert header
			fzh.dump_type = fh.status;
			fzh.num_blocks = fh.block_count;
			fzh.num_banks = fh.bn;
			fzh.num_voice = fh.vn;  // Number of voice blocks?
			fzh.num_waves = fh.wn_block_count;

			fzh.num_blocks_lo= fzh.num_blocks & 0xff;
			j= (fzh.num_blocks >> 8) & 0xff;
			fzh.num_blocks_hi= j;
			fzh.num_waves_lo= fzh.num_waves & 0xff;
			j= (fzh.num_waves >> 8) & 0xff;
			fzh.num_waves_hi= j;
			fzh.bank_num = 0x7f;
			fzh.voice_num = 0x7f; // Voice patch number 0-64

		}

		if(fzh.dump_type > 3 || fzh.num_banks > 8 || fzh.num_voice > 64){
			cout << "Sorry, can't send this type of file (" << file_name << ")\n";
			return 1;
		}
	}else{
		if(_stricmp(file_ext,"wav")==0){
      //			cout << "wave file!\n";
			file_type = wave;
			while(!feof(f1)){
				for(j = 0;j < 4; j++){
					chunk_id[j] = fgetc(f1);
				}
				chunk_id[4]=0; // null terminate
				chunk_size=0;
				skip_it=1;
    //				cout << "chunk " << chunk_id << "\n";
				if(strcmp(chunk_id,"RIFF")==0){ // found file header
					skip_it=0;
					chunk_size=read_long(f1);
				}
				if(strcmp(chunk_id,"WAVE")==0){ // found wave header
					skip_it=0;
				}
				if(strcmp(chunk_id,"fmt ")==0){ // found wave fmt header
					skip_it=0;
					chunk_size=read_long(f1);
					wFormatTag=read_int(f1);
					nChannels=read_int(f1);
					nSamplesPerSec=read_long(f1);
					nAvgBytesPerSec=read_long(f1);
					nBlockAlign=read_int(f1);
					wBitsPerSample=read_int(f1);
					num_samples = chunk_size / nBlockAlign;
					temp_long=nSamplesPerSec/1000;

//					cout << "    chans " << nChannels << ", align " << nBlockAlign << ", " << wBitsPerSample << " bit, rate " <<  temp_long << "khz" <<  "\n";
					skip_bytes(f1,chunk_size - 16);
				}
				if(strcmp(chunk_id,"data")==0){ // found wave header
					chunk_size=read_long(f1);
					num_samples = chunk_size / nBlockAlign;
					break;
				}
				if(skip_it==1){  //some other header,get its size
					chunk_size=read_long(f1);
					skip_bytes(f1,chunk_size);
				}
  //				cout << "    (" << chunk_size << " bytes)\n";
			}
			fzh.bank_num = 0x7f;
			fzh.voice_num = 0x7f;
			fzh.dump_type=1; // voice
			fzh.num_banks=0;
			fzh.num_voice=1;
			temp_long = ((num_samples * 2) + 1023) / 1024;
			fzh.num_waves = temp_long;
			fzh.num_blocks = fzh.num_voice + fzh.num_waves;
			if( (wBitsPerSample != 8 && wBitsPerSample != 16) || nChannels != 1){
				cout << "Sorry, this program does not support this wave file\n";
				cout << "Convert this file to 16 bit mono, uncompressed then try again\n";
				return 1;
			}
			return 0;
		}else{
			cout << "Sorry, this program does not support this file type (*." << file_ext << ")\n";
			if(_stricmp(file_ext,"fzp")==0)
				cout << "You need a program called FZEDIT\n";
			return 1;
		}
	}

	return 0;
}
int pc_disk::send_header(fz_header &fzh)
{
	char *k;

	char file_ext[6]=".tmp";
	k = strrchr(file_name,'.');
	if(k==0){ // if no file extension given,assume .fzb
		strcpy(file_ext,".fzb");
	}else{
		strcpy(file_ext,k);
		*k = 0; // chop off extension
	}
	file_type = fz;
//cout << " file: " << file_name << "  ext: " << file_ext << "\n";
	if(strcmp(file_ext,".wav")== 0 && fzh.dump_type != 1){
		cout << "SORRY, Can only VOICE DUMP to a WAV file\n";
		return 1;
	}
	switch(fzh.dump_type){
		case 0:
			cout << "Full Dump\n";
			strcpy(file_ext,".fzf");
			break;
		case 1:
			// does user want output as wave file?
			if(strcmp(file_ext,".wav") == 0){
				file_type = wave;
				cout << "Voice Dump to wave file\n";
				strcpy(file_ext,".wav");
			}else{
				cout << "Voice Dump\n";
				strcpy(file_ext,".fzv");
			}
			break;
		case 2:
			cout << "Bank Dump\n";
			strcpy(file_ext,".fzb");
			break;
		case 3:
			cout << "Effect Dump\n";
			strcpy(file_ext,".fze");
			break;
	}
	strcat(file_name,file_ext);
	cout <<	"Saving to " << file_name << "\n";
	if((f1=fopen (file_name,"wb"))==0){
		cout << "\nfile error\n";
		return 1;
	}
	if( file_type == fz){
		// copy fzh to fzht
		memcpy(&fzht,&fzh,sizeof(fzh));
	}
	return 0;
}

int pc_disk::get_next_block(unsigned char *buffer)
{
voicedata *v;
// signed char *b2;
int j,k,l;
   //	b2=(signed char *)buffer;
	if(file_type == wave){
		if(block_num++ == 0){ // need to construct voice data
			// clear buffer and format it as per voice header
			for(j=0; j < 300 ;j++)
				buffer[j]=0;

			v = (voicedata *) buffer;
			// calculate sample start and end points!!!!!!
			v->waved  = num_samples;
			v->gened=v->waved - 2;
			v->loop=0x1d7;  // loop flags
			// set loop start and ends (no - loop)
			for(j=0;j<8;j++){
				v->loopst[j] = v->gened;
				v->looped[j] = v->gened;
				v->looptm[j] = 64;
				v->dca_rate[j]=0xc0;
				v->dcf_rate[j]=0x90;
			}
			v->dca_end=7;
			v->dcf_end=7;
			v->dca_rate[0]=0x7f;
			v->dca_stop[0]=0xff;
		       //	v->dca_stop[1]=0x00;
			v->dcf_rate[0]=0x7f;
			v->dcf_stop[0]=0xff;
		       //	v->dcf_stop[1]=0x00;
			v->lfo_name=0x80;
			v->lfo_rate=0x40;
			v->hwid=0x60;   // highest note
			v->lwid=0x24;   // lowest
			v->cent=0x48;   // center note
			v->vel_dca_kf = 0x30;  // velocity sensitive
			// select best sample rate
			if( nSamplesPerSec < 14000){
				v->samp = 2; // 9khz
			}else{
				if( nSamplesPerSec > 27000){
					v->samp = 0; // 36khz
				}else{
					v->samp = 1; // 18khz
				}
			}
			build_fz_voice_name(file_name, v->name, sizeof(v->name));
		}else{
			if( wBitsPerSample == 8){
				// convert to 16 bit (crudely)
				fread(buffer, 512, 1, f1);
				for(j=1022,k=511;j>=0;j-=2){
					l = buffer[k--];
					l = (l - 0x80) * 0xff;
					buffer[j+1] = (l >> 8) & 0xff;
					buffer[j] = l & 0xff;
				}

			}else{
				fread(buffer, 1024, 1, f1);
			}
		}
	}else{
		fread(buffer, 1024, 1, f1);
	}
	return 0;
}

int pc_disk::send_next_block(unsigned char *buffer)
{
	voicedata *v;
	unsigned char *tempp;
	fz_file_header *fh;

	wave_file_header wav_head;

	v = (voicedata *) buffer;
	if(file_type == wave){
		if(block_num++ == 0){ // need to construct wave header
		// construct wave file header
		// this is a bit crude, will write extra data to disk after
		// wave end point (remainder of last 1024 byte block)
//cout << "Saving as a wave sample\n";
//cout << "wav start " << (int)v->wavst << "  end " << (int)v->waved << "\n";
//cout << "gen start " << (int)v->genst << "  end " << (int) v->gened << "\n" ;
//kk = (unsigned char *)v;
//for(x = 0 ;x < 16 ; x++){
//printf ("%2x ",(int) kk[x]);
//}
//cout << "\n"     ;
			num_samples = v->gened;
			strncpy(wav_head.chnk1_name,"RIFF",4);
			strncpy(wav_head.chnk2_name,"WAVE",4);
			strncpy(wav_head.chnk3_name,"fmt ",4);
			strncpy(wav_head.chnk4_name,"data",4);
			wav_head.chnk3_size = 16;
			wav_head.chnk4_size = num_samples * 2;
			wav_head.chnk1_size = wav_head.chnk4_size + 36;
			wav_head.wFormatTag = 1;
			wav_head.nChannels = 1;
			switch (v->samp){
				case 0:
					wav_head.nSamplesPerSec = 36000;
					break;
				case 1:
					wav_head.nSamplesPerSec = 18000;
					break;
				default:
					wav_head.nSamplesPerSec = 9000;
					break;
			}
			wav_head.nAvgBytesPerSec = 2 * wav_head.nSamplesPerSec;
			wav_head.nBlockAlign = 2;
			wav_head.wBitsPerSample = 16;
			fwrite(&wav_head, 44, 1, f1);

		}else{
			fwrite(buffer, 1024, 1, f1);
		}
	}else{
		if(block_num++ == 0){ // need to construct header
			tempp = buffer + 1000;
			fh = (fz_file_header *) tempp;
			fh->indicator		= 476549224;		/* FZCom file indicator           00 */
			fh->version		= 1;				/* FZCom file format version no.  04 */
			fh->status		= fzht.dump_type;	/* Type of file (F:0,B:1,V:2,E:3) 05 */
			fh->bn			= fzht.num_banks;	/* Number of banks in this file   06 */
			fh->vn			= fzht.num_voice;	/* Number of voices in this file  07 */
			fh->block_count		= fzht.num_blocks;	/* Number of blocks in the file   08 */
			fh->wn_block_count	= fzht.num_waves;	/* Number of PCM data blocks      0A */
			fh->dum2		= 0;
			fh->unused		= 0;
		}
		fwrite(buffer, 1024, 1, f1);
	}
	return 0;
}


