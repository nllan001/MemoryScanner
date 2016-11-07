#include <stdio.h>
#include <windows.h>

#define WRITABLE ( PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY )
#define IS_IN_SEARCH(mb, offset) (mb->searchmask[(offset)/8] & (1<<((offset) % 8)))
#define REMOVE_FROM_SEARCH(mb, offset) mb->searchmask[(offset)/8] &= ~(1<<(offset % 8))

// Memory block data structure
// -hproc: Handle of the process the memory block belongs to.
// -addr: Base address of the memory block in the process's virtual address space.
// -size: Size of the page region of pages with similar attributes.
// -buffer: Buffer to hold 
// -searchmask:
// -matches:
// -data_size:
// -next: Next memory block. Acts as a linked list.
typedef struct _MEMBLOCK {
	HANDLE hProc;
	unsigned char *addr;
	int size;
	unsigned char *buffer;

	unsigned char *searchmask;
	int matches;
	int data_size;

	struct _MEMBLOCK *next;
} MEMBLOCK;

// Search criteria.
// -UNCONDITIONAL: Search everything. Consider every byte.
// -EQUALS: Search those that equal a specified value.
// -INCREASED: Search bytes that increased in value.
// -DECREASED: Search bytes that decreased in value.
typedef enum {
	COND_UNCONDITIONAL,
	COND_EQUALS,
	COND_INCREASED,
	COND_DECREASED
} SEARCH_CONDITION;

MEMBLOCK* create_memblock(HANDLE hProc, MEMORY_BASIC_INFORMATION *meminfo, int data_size) {
	MEMBLOCK* mb = (MEMBLOCK*) malloc(sizeof(MEMBLOCK));
	if(mb) {
		mb->hProc = hProc;
		mb->addr = (unsigned char*) meminfo->BaseAddress;
		mb->size = meminfo->RegionSize;
		mb->buffer = (unsigned char*) malloc(meminfo->RegionSize);
		mb->searchmask = (unsigned char*) malloc(meminfo->RegionSize/8);
		memset(mb->searchmask, 0xff, meminfo->RegionSize/8);
		mb->matches = meminfo->RegionSize;
		mb->data_size = data_size;
		mb->next = NULL;
	}
	return mb;
}

void free_memblock(MEMBLOCK *mb) {
	if(mb) {
		if(mb->buffer) {
			free(mb->buffer);
		}
		if(mb->searchmask) {
			free(mb->searchmask);
		}
		free(mb);
	}
}

void update_memblock(MEMBLOCK *mb, SEARCH_CONDITION condition, unsigned int val) {
	static unsigned char tempbuf[128*1024];
	SIZE_T bytes_left;
	SIZE_T total_read;
	SIZE_T bytes_to_read;
	SIZE_T bytes_read;
	unsigned int temp_size = mb->size;

	if(mb->matches > 0) {

		bytes_left = mb->size;
		total_read = 0;
		mb->matches = 0;

		while(bytes_left > 0) {
			bytes_to_read = (bytes_left > sizeof(tempbuf)) ? sizeof(tempbuf) : bytes_left;
			ReadProcessMemory(mb->hProc, mb->addr + total_read, tempbuf, bytes_to_read, &bytes_read);
			if(bytes_read != bytes_to_read) {
				break;
			}
			
			if(condition == COND_UNCONDITIONAL) {
				memset(mb->searchmask + (total_read/8), 0xff, bytes_read/8);
				mb->matches += bytes_read;
			} else {
				unsigned int offset;
				for(offset = 0; offset < bytes_read; offset += mb->data_size) {
					if(IS_IN_SEARCH(mb, (total_read+offset))) {
						bool is_match = false;
						unsigned int temp_val;
						switch(mb->data_size) {
							case 1:
								temp_val = tempbuf[offset];
								break;
							case 2:
								temp_val = *((unsigned short*) &tempbuf[offset]);
								break;
							case 4:
							default:
								temp_val = *((unsigned int*) &tempbuf[offset]);
								break;
						}

						switch(condition) {
							case COND_EQUALS:
								is_match = (temp_val == val);
								break;
							default:
								break;
						}

						if(is_match) {
							mb->matches++;
						} else {
							REMOVE_FROM_SEARCH(mb, (total_read+offset));
						}
					}
				}
			}
			memcpy(mb->buffer + total_read, tempbuf, bytes_read);
			bytes_left -= bytes_read;
			total_read += bytes_read;
			if(total_read > temp_size) {
				break;
			}
		}
	}
	mb->size = total_read;
}

MEMBLOCK* create_scan(unsigned int pid, int data_size) {
	MEMBLOCK *mb_list = NULL;
	MEMORY_BASIC_INFORMATION meminfo;
	unsigned char *addr = 0;

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	
	if(hProc) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		while(addr < si.lpMaximumApplicationAddress) {
			if(VirtualQueryEx(hProc, addr, &meminfo, sizeof(meminfo)) == 0) {
				break;
			}
			if((meminfo.State & MEM_COMMIT) && (meminfo.Protect & WRITABLE)) {
				MEMBLOCK *mb = create_memblock(hProc, &meminfo, data_size);
				if(mb) {
					mb->next = mb_list;
					mb_list = mb;
				}
			}
			addr = (unsigned char*) meminfo.BaseAddress + meminfo.RegionSize;
		}
	}

	return mb_list;
}

void free_scan(MEMBLOCK *mb_list) {
	CloseHandle(mb_list->hProc);
	while(mb_list) {
		MEMBLOCK *mb = mb_list;
		mb_list = mb_list->next;
		free_memblock(mb);
	}
}

void update_scan(MEMBLOCK *mb_list, SEARCH_CONDITION condition, unsigned int val) {
	MEMBLOCK *mb = mb_list;
	while(mb) {
		update_memblock(mb, condition, val);
		mb = mb->next;
	}
}

void dump_scan_info(MEMBLOCK *mb_list) {
	MEMBLOCK *mb = mb_list;
	while(mb) {
		printf("0x%08x %d\r\n", mb->addr, mb->size);
		for(unsigned int i = 0; i < mb->size; i++) {
			printf("%02x", mb->buffer[i]);
		}
		mb = mb->next;
	}
}

void print_matches(MEMBLOCK *mb_list) {
	unsigned int offset;
	MEMBLOCK *mb = mb_list;
	while(mb) {
		for(offset = 0; offset < mb->size; offset += mb->data_size) {
			if(IS_IN_SEARCH(mb, offset)) {
				printf("0x%08x\r\n", mb->addr + offset);
			}
		}
		mb = mb->next;
	}
}

int get_match_count(MEMBLOCK *mb_list) {
	MEMBLOCK *mb = mb_list;
	unsigned int count = 0;
	while(mb) {
		count += mb->matches;
		mb = mb->next;
	}
	return count;
}

int main(int argc, char *argv[]) {
	MEMBLOCK *scan = create_scan(atoi(argv[1]), 4);
	if(scan) {
		//update_scan(scan, COND_UNCONDITIONAL, 0);
		//dump_scan_info(scan);

		printf("searching for 1000\r\n");
		update_scan(scan, COND_EQUALS, 1000);
		print_matches(scan);

		{
			char s[10];
			gets(s);
		}

		printf("searching for 2000\r\n");
		update_scan(scan, COND_EQUALS, 2000);
		print_matches(scan);

		free(scan);
	}
	return 0;
}
