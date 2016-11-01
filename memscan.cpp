#include <stdio.h>
#include <windows.h>

#define WRITABLE ( PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY )

typedef struct _MEMBLOCK {
	HANDLE hProc;
	unsigned char *addr;
	int size;
	unsigned char *buffer;
	struct _MEMBLOCK *next;
} MEMBLOCK;

MEMBLOCK* create_memblock(HANDLE hProc, MEMORY_BASIC_INFORMATION *meminfo) {
	MEMBLOCK* mb = (MEMBLOCK*) malloc(sizeof(MEMBLOCK));
	if(mb) {
		mb->hProc = hProc;
		mb->addr = (unsigned char*) meminfo->BaseAddress;
		mb->size = meminfo->RegionSize;
		mb->buffer = (unsigned char*) malloc(meminfo->RegionSize);
		mb->next = NULL;
	}
	return mb;
}

void free_memblock(MEMBLOCK *mb) {
	if(mb) {
		if(mb->buffer) {
			free(mb->buffer);
		}
		free(mb);
	}
}

void update_memblock(MEMBLOCK *mb) {
	static unsigned char tempbuf[128*1024];
	SIZE_T bytes_left;
	SIZE_T total_read;
	SIZE_T bytes_to_read;
	SIZE_T bytes_read;

	bytes_left = mb->size;
	total_read = 0;

	while(bytes_left > 0) {
		bytes_to_read = (bytes_left > sizeof(tempbuf)) ? sizeof(tempbuf) : bytes_left;
		ReadProcessMemory(mb->hProc, mb->addr + total_read, tempbuf, bytes_to_read, &bytes_read);
		printf("%u %u %u %u\n", bytes_left, sizeof(tempbuf), bytes_to_read, bytes_read);
		if(bytes_read != bytes_to_read) break;
		memcpy(mb->buffer + total_read, tempbuf, bytes_read);
		bytes_left -= bytes_read;
		total_read += bytes_read;
	}
	mb->size = total_read;
}

MEMBLOCK* create_scan(unsigned int pid) {
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
				MEMBLOCK *mb = create_memblock(hProc, &meminfo);
				if(mb) {
					update_memblock(mb);
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

void update_scan(MEMBLOCK *mb_list) {
	MEMBLOCK *mb = mb_list;
	while(mb) {
		update_memblock(mb);
		mb = mb->next;
	}
}

void dump_scan_info(MEMBLOCK *mb_list) {
	MEMBLOCK *mb = mb_list;
	while(mb) {
		printf("0x%08x %d\r\n", mb->addr, mb->size);
		for(int i = 0; i < mb->size; i++) {
			printf("%02x", mb->buffer[i]);
		}
		mb = mb->next;
	}
}

int main(int argc, char *argv[]) {
	MEMBLOCK *scan = create_scan(atoi(argv[1]));
	if(scan) {
		dump_scan_info(scan);
		free(scan);
	}
	return 0;
}
