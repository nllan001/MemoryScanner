#include <iostream>
#include <stdio.h>
#include <vector>
#include <windows.h>

using namespace std;

#define WRITABLE ( PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY )
/*
#define IS_IN_SEARCH(mb, offset) (mb->searchmask[(offset)/8] & (1<<((offset) % 8)))
#define REMOVE_FROM_SEARCH(mb, offset) mb->searchmask[(offset)/8] &= ~(1<<(offset % 8))
*/

typedef enum {
	COND_UNCONDITIONAL,
	COND_EQUALS,
	COND_INCREASED,
	COND_DECREASED
} Search_Condition;

// Memory block data structure
// -hproc: Handle of the process the memory block belongs to.
// -addr: Base address of the memory block in the process's virtual address space.
// -size: Size of the page region of pages with similar attributes.
// -buffer: Buffer to hold 
// -searchmask: A boolean array to show which bytes in the buffer are to be ignored.
// -matches: How many matches have been found that agree with the conditions placed.
// -data_size: The size of the data type of concern. ex. 1 for unsigned char and 4 for int.
// -next: Next memory block. Acts as a linked list.
typedef class _Memblock {
public:
	HANDLE hProc;
	unsigned char *addr;
	int size;
	vector<unsigned char> buffer;
	vector<bool> searchmask;
	unsigned int matches;
	int data_size;
	_Memblock *next;

	// Initialize a memory block
	_Memblock(HANDLE hProc, MEMORY_BASIC_INFORMATION *meminfo, int data_size) {
		this->hProc = hProc;
		this->addr = (unsigned char*) meminfo->BaseAddress;
		this->size = meminfo->RegionSize;
		vector<unsigned char> temp_buf(meminfo->RegionSize, 0);
		this->buffer = temp_buf;
		vector<bool> temp_mask(meminfo->RegionSize, 0);
		for(unsigned int i = 0; i < temp_mask.size(); i += data_size) temp_mask[i] = 1;
		this->searchmask = temp_mask;
		this->matches = meminfo->RegionSize / data_size;
		this->data_size = data_size;
		this->next = NULL;
	}

	// Check whether the byte of interest is marked present in the search mask
	bool is_in_search(SIZE_T offset) {
		if(offset < ((SIZE_T) this->size)) {
			return this->searchmask[offset];
		} else {
			return false;
		}
	}

	// Set off a byte's flag in the search mask
	void remove_from_search(SIZE_T offset) {
		if(offset < ((SIZE_T) this->size)) {
			this->searchmask[offset] = 0;
		}
	}

	// Update a memory block with which bytes the condition specifies
	void update(Search_Condition condition, unsigned int val) {
		unsigned char temp_buf[128*1024];
		vector<bool> temp_search(this->searchmask.size(), 0);
		SIZE_T bytes_left;
		SIZE_T total_read;
		SIZE_T bytes_to_read;
		SIZE_T bytes_read;

		if(this->matches > 0) {
			bytes_left = this->size;
			total_read = 0;
			this->matches = 0;

			while(bytes_left > 0) {
				bytes_to_read = (bytes_left < sizeof(temp_buf)) ? bytes_left : sizeof(temp_buf);
				if(ReadProcessMemory(this->hProc, this->addr + total_read, temp_buf, bytes_to_read, &bytes_read)) {
					if(bytes_read != bytes_to_read) {
						break;
					}

					for(SIZE_T offset = 0; offset < bytes_read; offset += this->data_size) {
						//if(this->is_in_search(total_read+offset)) {
						if(this->searchmask[total_read+offset]) {
							bool is_match = false;
							unsigned int temp_val;

							// Read the value from the buffer depending on data size
							switch(this->data_size) {
								case 1:
									temp_val = *((unsigned char*) &temp_buf[offset]);
									break;
								case 2:
									temp_val = *((unsigned short*) &temp_buf[offset]);
									break;
								case 4:
								default:
									temp_val = *((unsigned int*) &temp_buf[offset]);
									break;
							}

							// Update matches in the buffer based on condition
							switch(condition) {
								case COND_EQUALS:
									is_match = (temp_val == val);
									break;
								default:
									break;
							}

							if(is_match) {
								this->matches++;
								temp_search[total_read+offset] = 1;
							} //else {
								//this->remove_from_search(total_read+offset);
							//	this->searchmask[total_read+offset] = 0;
							//}
						}
					}

					// Copy the temp buf into the actual buffer and update reading data
					for(unsigned int i = 0; i < bytes_read; i++) {
						this->buffer[total_read + i] = temp_buf[i];
					}
					bytes_left -= bytes_read;
					total_read += bytes_read;
				} else {
					break;
				}
			}
		}
		//this->size = total_read;
		this->searchmask = temp_search;
	}

	~_Memblock() {}

} Memblock;

// Linked list of memory blocks
typedef class _Scan {
public:
	Memblock *head;

	// Initialize the linked list with memory blocks of the specified process
	_Scan(unsigned int pid, int data_size) {
		head = NULL;
		MEMORY_BASIC_INFORMATION meminfo;
		unsigned char *addr;
		
		HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, false, pid);

		if(hProc) {
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			while(addr < si.lpMaximumApplicationAddress) {
				if(VirtualQueryEx(hProc, addr, &meminfo, sizeof(meminfo)) == 0) {
					break;
				}
				if((meminfo.State & MEM_COMMIT) && (meminfo.Protect & WRITABLE)) {
					Memblock *mb = new Memblock(hProc, &meminfo, data_size);
					if(mb) {
						mb->next = head;
						head = mb;
					}
				}
				addr = (unsigned char*) meminfo.BaseAddress + meminfo.RegionSize;
			}
		}
	}

	// Update the linked list with new conditions
	void update(Search_Condition condition, unsigned int val) {
		Memblock *temp_head = this->head;
		while(temp_head) {
			if(condition == COND_UNCONDITIONAL) {
				for(unsigned int i = 0; i < temp_head->searchmask.size(); i += temp_head->data_size) {
					temp_head->searchmask[i] = 1;
					temp_head->matches = temp_head->searchmask.size() / temp_head->data_size;
				}
				temp_head = temp_head->next;
			} else {
				temp_head->update(condition, val);
				temp_head = temp_head->next;
			}
		}
	}

	// Print the info about the memory blocks in the list
	void scan_dump() {
		Memblock *temp_head = this->head;
		while(temp_head) {
			printf("0x%08x %d\r\n", temp_head->addr, temp_head->size);
			/*
			for(int i = 0; i < temp_head->size; i++) {
				printf("%02x", temp_head->buffer[i]);
			}
			*/
			temp_head = temp_head->next;
		}
	}

	void print_matches() {
		Memblock *temp_head = this->head;
		while(temp_head) {
			for(unsigned int offset = 0; offset < temp_head->size; offset += temp_head->data_size) {
				if(temp_head->is_in_search(offset)) {
					printf("0x%08x\r\n", temp_head->addr + offset);
				}
			}
			temp_head = temp_head->next;
		}
	}

	unsigned int get_matches() {
		Memblock *temp_head = this->head;
		unsigned int count = 0;
		while(temp_head) {
			for(unsigned int offset = 0; offset < temp_head->size; offset += temp_head->data_size) {
				if(temp_head->is_in_search(offset)) {
					count++;
				}
			}
			temp_head = temp_head->next;
		}
		return count;
	}

	unsigned int get_matches2() {
		Memblock *temp_head = this->head;
		unsigned int count = 0;
		while(temp_head) {
			count += temp_head->matches;
			temp_head = temp_head->next;
		}
		return count;
	}

	unsigned int get_size() {
		Memblock *temp_head = this->head;
		unsigned int size = 0;
		while(temp_head) {
			size += temp_head->size;
			temp_head = temp_head->next;
		}
		return size;
	}

	int get_blocks() {
		Memblock *temp_head = this->head;
		int count = 0;
		while(temp_head) {
			count++;
			temp_head = temp_head->next;
		}
		return count;
	}

	~_Scan() {
		while(head) {
			Memblock *temp_head = head;
			head = head->next;
			delete temp_head;
		}
	}

} Scan;

int main(int argc, char *argv[]) {
	Scan new_scan(atoi(argv[1]), 4);
	if(new_scan.head) {
		/*
		new_scan.update(COND_UNCONDITIONAL, 4);
		cout << new_scan.get_matches() << " " << new_scan.get_matches2() << " " << new_scan.get_blocks() << " " << new_scan.get_size() << endl;
		new_scan.scan_dump();
		*/
		new_scan.update(COND_EQUALS, 123454321);
		cout << new_scan.get_matches() << " " << new_scan.get_matches2() << " " << new_scan.get_blocks() << " " << new_scan.get_size() << endl;
		//new_scan.print_matches();
		/*
		new_scan.update(COND_EQUALS, 2000);
		cout << new_scan.get_matches() << " " << new_scan.get_matches2() << " " << new_scan.get_blocks() << endl;
		//new_scan.print_matches();
		new_scan.update(COND_EQUALS, 2000);
		cout << new_scan.get_matches() << " " << new_scan.get_matches2() << " " << new_scan.get_blocks() << endl;
		//new_scan.print_matches();
		new_scan.update(COND_EQUALS, 1000);
		cout << new_scan.get_matches() << " " << new_scan.get_matches2() << " " << new_scan.get_blocks() << endl;
		*/
	}
	return 0;
}
















