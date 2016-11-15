#include <iostream>
#include <stdio.h>
#include <vector>
#include <windows.h>
#include <fstream>

using namespace std;

#define WRITABLE ( PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY )

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
// -buffer: Buffer to hold the bytes of memory obtained for each memory block.
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

	// Update a memory block with which bytes the condition specifies
	void update(Search_Condition condition, unsigned int val) {
		unsigned char temp_buf[128*1024];
		vector<bool> temp_search(this->searchmask.size(), 0);
		SIZE_T bytes_left;
		SIZE_T total_read;
		SIZE_T bytes_to_read;
		SIZE_T bytes_read;

		// Only check if there are at least some matches possible
		if(this->matches > 0) {
			bytes_left = this->size;
			total_read = 0;
			this->matches = 0;

			// Keep reading process memory while there are still bytes left to read
			while(bytes_left > 0) {
				bytes_to_read = (bytes_left < sizeof(temp_buf)) ? bytes_left : sizeof(temp_buf);
				if(ReadProcessMemory(this->hProc, this->addr + total_read, temp_buf, bytes_to_read, &bytes_read)) {
					if(bytes_read != bytes_to_read) {
						break;
					}

					// Iterate through the buffer, incrementing by the data size (unsigned char(1)/short(2)/int(4))
					for(SIZE_T offset = 0; offset < bytes_read; offset += this->data_size) {
						if(this->searchmask[total_read+offset]) {
							bool is_match = false;
							unsigned int temp_val;
							unsigned int prev_val = 0;

							// Read the value from the buffer depending on data size
							switch(this->data_size) {
								case 1:
									temp_val = *((unsigned char*) &temp_buf[offset]);
									prev_val = *((unsigned char*) &buffer[total_read+offset]);
									break;
								case 2:
									temp_val = *((unsigned short*) &temp_buf[offset]);
									prev_val = *((unsigned short*) &buffer[total_read+offset]);
									break;
								case 4:
								default:
									temp_val = *((unsigned int*) &temp_buf[offset]);
									prev_val = *((unsigned int*) &buffer[total_read+offset]);
									break;
							}

							// Update matches in the buffer based on condition
							switch(condition) {
								case COND_EQUALS:
									is_match = (temp_val == val);
									break;
								case COND_INCREASED:
									is_match = (prev_val < temp_val);
									break;
								case COND_DECREASED:
									is_match = (prev_val > temp_val);
									break;
								default:
									break;
							}

							if(is_match) {
								this->matches++;
								temp_search[total_read+offset] = 1;
							}
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
		// Update searchmask with newly made searchmask based off of currently seen  matches
		this->searchmask = temp_search;
	}

	~_Memblock() {}

} Memblock;

// Linked list of memory blocks
typedef class _Scan {
public:
	Memblock *head;

	_Scan() {
		head = NULL;
	}

	// Initialize the linked list with memory blocks of the specified process
	_Scan(unsigned int pid, int data_size) {
		head = NULL;
		MEMORY_BASIC_INFORMATION meminfo;
		unsigned char *addr = 0;
		
		// Gets the handle of the process with a request for all access
		HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, false, pid);

		if(hProc) {
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			while(addr < si.lpMaximumApplicationAddress) {
				// Retrievs the BASIC_MEMORY_INFORMATION object of the process of interest
				if(VirtualQueryEx(hProc, addr, &meminfo, sizeof(meminfo)) == 0) {
					break;
				}
				// Check for flags to ensure it isn't empty reserved memory and it has write permissions
				if((meminfo.State & MEM_COMMIT) && (meminfo.Protect & WRITABLE)) {
					Memblock *mb = new Memblock(hProc, &meminfo, data_size);
					if(mb) {
						mb->next = head;
						head = mb;
					}
				}
				addr = (unsigned char*) meminfo.BaseAddress + meminfo.RegionSize;
			}
		} else {
			cout << "PID is not available or valid" << endl;
			head = NULL;
		}
	}

	// Update the linked list with new conditions
	void update(Search_Condition condition, unsigned int val) {
		Memblock *temp_head = this->head;
		while(temp_head) {
			// If the condition is unconditional, the searhmask is updated with a match for each piece of data in the buffer
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

	// Write a value to a specified address in a process's memory
	void poke(HANDLE hProc, unsigned char *addr, int data_size, unsigned int val) {
		if(WriteProcessMemory(hProc, addr, &val, data_size, NULL) == 0) {
			cout << "Failed to poke" << endl;
		}
	}

	// Reads the value from a specified address in a process's memory
	unsigned int peek(HANDLE hProc, unsigned char *addr, int data_size) {
		unsigned int val = 0;
		if(ReadProcessMemory(hProc, addr, &val, data_size, NULL) == 0) {
			cout << "Failed to peek" << endl;
		}
		return val;
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

	// Print the addresses of every match
	void print_matches() {
		Memblock *temp_head = this->head;
		int list_number = 0;
		while(temp_head) {
			for(unsigned int offset = 0; offset < temp_head->size; offset += temp_head->data_size) {
				if(temp_head->is_in_search(offset)) {
					unsigned int val = peek(temp_head->hProc, temp_head->addr + offset, temp_head->data_size);
					printf("%d: Address - 0x%08x: Value - (Hex) 0x%08x, (Dec) %d\r\n", list_number++, temp_head->addr + offset, val, val);
				}
			}
			temp_head = temp_head->next;
		}
	}

	// Get matches through counting search mask values
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

	// Get matches through summing the match counts
	unsigned int get_matches2() {
		Memblock *temp_head = this->head;
		unsigned int count = 0;
		while(temp_head) {
			count += temp_head->matches;
			temp_head = temp_head->next;
		}
		return count;
	}

	// Get first match's address
	unsigned char* get_match() {
		Memblock *temp_head = this->head;
		unsigned int count = 0;
		while(temp_head) {
			for(unsigned int offset = 0; offset < temp_head->size; offset += temp_head->data_size) {
				if(temp_head->is_in_search(offset)) {
					return temp_head->addr + offset;
				}
			}
			temp_head = temp_head->next;
		}
		return NULL;
	}

	// Get the size of the linked list in bytes
	unsigned int get_size() {
		Memblock *temp_head = this->head;
		unsigned int size = 0;
		while(temp_head) {
			size += temp_head->size;
			temp_head = temp_head->next;
		}
		return size;
	}

	// Get how many nodes are in the linked list
	int get_blocks() {
		Memblock *temp_head = this->head;
		int count = 0;
		while(temp_head) {
			count++;
			temp_head = temp_head->next;
		}
		return count;
	}

	// Free up the memory used to create memblocks when done and close the handle to the process
	~_Scan() {
		if(head) {
			CloseHandle(head->hProc);
		}
		while(head) {
			Memblock *temp_head = head;
			head = head->next;
			delete temp_head;
		}
	}

} Scan;

// Retrieve the local list of processes
void view_tasklist() {
	system("tasklist | awk '{printf \"%-30s %-15s \\n \", $1, $2}'");
}

// Retrieve the pid of interest from the user
unsigned int get_pid() {
	cout << "Enter the process you were interested in (Enter pid):" << endl;
	string choice_string;
	cin >> choice_string;
	int choice = atoi(choice_string.c_str());
	return (unsigned int) choice;
}

// Create a new scan and segment it by specific data type
Scan* create_scan(Scan *current_scan, unsigned int &pid) {
	while(1) {
		cout << endl << "===================================" << endl
			<< "How do you want to segment the scan (Enter number)?" << endl
			<< "1. Char (1 byte)" << endl
			<< "2. Short (2 bytes)" << endl
			<< "3. Int (4 bytes)" << endl
			<< "4. Go back" << endl;
		int choice_1;
		cin >> choice_1;
		unsigned int new_pid;
		switch(choice_1) {
			case 1:
				new_pid = get_pid();
				{
					Scan *new_scan_1 = new Scan(new_pid, 1); 
					if(new_scan_1->head) {
						if(current_scan) {
							delete current_scan;
						}
						pid = new_pid;
						return new_scan_1;
					} else {
						cout << "Scan was invalid" << endl;
					}
				}
				break;
			case 2:
				new_pid = get_pid();
				{
					Scan *new_scan_2 = new Scan(new_pid, 2); 
					if(new_scan_2->head) {
						if(current_scan) {
							delete current_scan;
						}
						pid = new_pid;
						return new_scan_2;
					} else {
						cout << "Scan was invalid" << endl;
					}
				}
				break;
			case 3:
				new_pid = get_pid();
				{
					Scan *new_scan_3 = new Scan(new_pid, 4); 
					if(new_scan_3->head) {
						if(current_scan) {
							delete current_scan;
						}
						pid = new_pid;
						return new_scan_3;
					} else {
						cout << "Scan was invalid" << endl;
					}
				}
				break;
			case 4:
				return current_scan;
			default:
				cout << "Invalid choice. Try again." << endl;
				break;
		}
	}
}

// Filter for equivalent value
void equal_filter(Scan *current_scan) {
	unsigned int val = 0;
	cout << "What value do you want to look for?" << endl;
	cin >> val;
	cout << "Filtering for " << val << endl;
	current_scan->update(COND_EQUALS, val);
}

// Filter for increased value
void inc_filter(Scan *current_scan) {
	cout << "Filtering for an increased value" << endl;
	current_scan->update(COND_INCREASED, 0);
}

// Filter for decreased value
void dec_filter(Scan *current_scan) {
	cout << "Filtering for a decreased value" << endl;
	current_scan->update(COND_DECREASED, 0);
}

// Resets all matches
void uncond_filter(Scan *current_scan) {
	cout << "Resetting all conditions" << endl;
	current_scan->update(COND_UNCONDITIONAL, 0);
}

// Overwrites a value at a specified address
void overwrite(Scan *current_scan) {
	unsigned int current_matches = current_scan->get_matches();
	unsigned int match_wanted = 0;
	unsigned int val = 0;
	while(1) {
		cout << "Current list of matches and their values:" << endl;
		current_scan->print_matches();
		cout << endl;
		cout << "Enter list position of the value you want to overwrite:" << endl;
		cin >> match_wanted;
		if(match_wanted >= current_matches) {
			cout << "Invalid input. Try again." << endl;
		} else {
			Memblock *temp_head = current_scan->head;
			unsigned int list_number = 0;
			while(temp_head) {
				for(unsigned int offset = 0; offset < temp_head->size; offset += temp_head->data_size) {
					if(temp_head->is_in_search(offset) && list_number < match_wanted) {
						list_number++;
					} else if(temp_head->is_in_search(offset) && list_number == match_wanted) {
						unsigned int current_val = current_scan->peek(temp_head->hProc, temp_head->addr + offset, temp_head->data_size);
						cout << "Current value is: " << current_val << endl;
						cout << "Enter value to overwrite with:" << endl;
						cin >> val;

						current_scan->poke(temp_head->hProc, temp_head->addr + offset, temp_head->data_size, val);
						cout << "Value has been overwritten." << endl;
						return;
					}
				}
				temp_head = temp_head->next;
			}
		}
	}
}

int main(int argc, char *argv[]) {
	Scan *current_scan;
	unsigned int current_pid;
	while(1) {
		cout << endl << "===================================" << endl
			<< "Memory Scan Menu (Enter number): " << endl
			<< "1. View processes" << endl
			<< "2. Scan new process" << endl
			<< "3. Filter for equal value" << endl
			<< "4. Filter for increased value" << endl
			<< "5. Filter for decreased value" << endl
			<< "6. Look at all current matches" << endl
			<< "7. Reset to original matches" << endl
			<< "8. Overwrite value" << endl
			<< "9. Exit" << endl;

		char choice;
		cin >> choice;
		switch(choice) {
			case '1':
				cout << "List of current processes with their PIDs:" << endl;
				view_tasklist();
				break;
			case '2':
				cout << "Creating new scan:" << endl;
				current_scan = create_scan(current_scan, current_pid);
				break;
			case '3':
				equal_filter(current_scan);
				break;
			case '4':
				inc_filter(current_scan);
				break;
			case '5':
				dec_filter(current_scan);
				break;
			case '6':
				current_scan->print_matches();
				break;
			case '7':
				uncond_filter(current_scan);
				break;
			case '8':
				overwrite(current_scan);
				break;
			case '9':
				cout << "Exiting." << endl;
				return 0;
				break;
			default:
				cout << "Not a valid choice." << endl;
				break;
		}
	}
	if(current_scan) {
		delete current_scan;
	}
	return 0;
}
















