#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <cutils/properties.h>
#include <time.h>
#include <stdbool.h>

#define VER	"v1.4"

#define SCNx64    "llx"

enum {
    HEAP_UNKNOWN,		// map name begin of '[anon:'
    HEAP_DALVIK,
    HEAP_NATIVE,

    HEAP_DALVIK_OTHER,
    HEAP_STACK,			// map name begin of '[stack'
    HEAP_CURSOR,		// map name is '/dev/ashmem/CursorWindow'
    HEAP_ASHMEM,
    HEAP_GL_DEV,		// map name is '/dev/kgsl-3d0'
    HEAP_UNKNOWN_DEV,		// map name begin of '/dev/'
    HEAP_SO,			// map name end with '.so'
    HEAP_JAR,			// map name end with '.jar'
    HEAP_APK,			// map name end with '.apk'
    HEAP_TTF,			// map name end with '.ttf'
    HEAP_DEX,			// map name end with '.dex'
    HEAP_OAT,			// map name end with '.oat'
    HEAP_ART,			// map name end with '.art'
    HEAP_UNKNOWN_MAP,		// unknown map
    HEAP_GRAPHICS,
    HEAP_GL,
    HEAP_OTHER_MEMTRACK,

    HEAP_DALVIK_NORMAL,
    HEAP_DALVIK_LARGE,
    HEAP_DALVIK_LINEARALLOC,
    HEAP_DALVIK_ACCOUNTING,
    HEAP_DALVIK_CODE_CACHE,
    HEAP_DALVIK_ZYGOTE,
    HEAP_DALVIK_NON_MOVING,
    HEAP_DALVIK_INDIRECT_REFERENCE_TABLE,

    _NUM_HEAP,
    _NUM_EXCLUSIVE_HEAP = HEAP_OTHER_MEMTRACK+1,
    _NUM_CORE_HEAP = HEAP_NATIVE+1
};

struct stats_t {
    int pss;
    int swappablePss;
    int privateDirty;
    int sharedDirty;
    int privateClean;
    int sharedClean;
    int swappedOut;
};

static void read_mapinfo(FILE *fp, struct stats_t* stats)
{
    char line[1024];
    int len, nameLen;
    bool skip, done = false;

    unsigned size = 0, resident = 0, pss = 0, swappable_pss = 0;
    float sharing_proportion = 0.0;
    unsigned shared_clean = 0, shared_dirty = 0;
    unsigned private_clean = 0, private_dirty = 0;
    unsigned swapped_out = 0;
    bool is_swappable = false;
    unsigned referenced = 0;
    unsigned temp;

    uint64_t start;
    uint64_t end = 0;
    uint64_t prevEnd = 0;
    char* name;
    int name_pos;

    int whichHeap = HEAP_UNKNOWN;
    int subHeap = HEAP_UNKNOWN;
    int prevHeap = HEAP_UNKNOWN;

    if(fgets(line, sizeof(line), fp) == 0) return;

    while (!done) {
        prevHeap = whichHeap;
        prevEnd = end;
        whichHeap = HEAP_UNKNOWN;
        subHeap = HEAP_UNKNOWN;
        skip = false;
        is_swappable = false;

        len = strlen(line);
        if (len < 1) return;
        line[--len] = 0;

        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %*s %*x %*x:%*x %*d%n", &start, &end, &name_pos) != 2) {
            skip = true;
        } else {
            while (isspace(line[name_pos])) {
                name_pos += 1;
            }
            name = line + name_pos;
            nameLen = strlen(name);

            if ((strstr(name, "[heap]") == name)) {
                whichHeap = HEAP_NATIVE;
            } else if (strncmp(name, "/dev/ashmem", 11) == 0) {
                if (strncmp(name, "/dev/ashmem/dalvik-", 19) == 0) {
                    whichHeap = HEAP_DALVIK_OTHER;
                    if (strstr(name, "/dev/ashmem/dalvik-LinearAlloc") == name) {
                        subHeap = HEAP_DALVIK_LINEARALLOC;
                    } else if ((strstr(name, "/dev/ashmem/dalvik-alloc space") == name) ||
                               (strstr(name, "/dev/ashmem/dalvik-main space") == name)) {
                        // This is the regular Dalvik heap.
                        whichHeap = HEAP_DALVIK;
                        subHeap = HEAP_DALVIK_NORMAL;
                    } else if (strstr(name, "/dev/ashmem/dalvik-large object space") == name) {
                        whichHeap = HEAP_DALVIK;
                        subHeap = HEAP_DALVIK_LARGE;
                    } else if (strstr(name, "/dev/ashmem/dalvik-non moving space") == name) {
                        whichHeap = HEAP_DALVIK;
                        subHeap = HEAP_DALVIK_NON_MOVING;
                    } else if (strstr(name, "/dev/ashmem/dalvik-zygote space") == name) {
                        whichHeap = HEAP_DALVIK;
                        subHeap = HEAP_DALVIK_ZYGOTE;
                    } else if (strstr(name, "/dev/ashmem/dalvik-indirect ref") == name) {
                        subHeap = HEAP_DALVIK_INDIRECT_REFERENCE_TABLE;
                    } else if (strstr(name, "/dev/ashmem/dalvik-jit-code-cache") == name) {
                        subHeap = HEAP_DALVIK_CODE_CACHE;
                    } else {
                        subHeap = HEAP_DALVIK_ACCOUNTING;  // Default to accounting.
                    }
                } else if (strncmp(name, "/dev/ashmem/CursorWindow", 24) == 0) {
                    whichHeap = HEAP_CURSOR;
                } else if (strncmp(name, "/dev/ashmem/libc malloc", 23) == 0) {
                    whichHeap = HEAP_NATIVE;
                } else {
                    whichHeap = HEAP_ASHMEM;
                }
            } else if (strncmp(name, "[anon:libc_malloc]", 18) == 0) {
                whichHeap = HEAP_NATIVE;
            } else if (strncmp(name, "[stack", 6) == 0) {
                whichHeap = HEAP_STACK;
            } else if (strncmp(name, "/dev/", 5) == 0) {
                if (strncmp(name, "/dev/kgsl-3d0", 13) == 0) {
                    whichHeap = HEAP_GL_DEV;
                } else {
                    whichHeap = HEAP_UNKNOWN_DEV;
                }
            } else if (nameLen > 3 && strcmp(name+nameLen-3, ".so") == 0) {
                whichHeap = HEAP_SO;
                is_swappable = true;
            } else if (nameLen > 4 && strcmp(name+nameLen-4, ".jar") == 0) {
                whichHeap = HEAP_JAR;
                is_swappable = true;
            } else if (nameLen > 4 && strcmp(name+nameLen-4, ".apk") == 0) {
                whichHeap = HEAP_APK;
                is_swappable = true;
            } else if (nameLen > 4 && strcmp(name+nameLen-4, ".ttf") == 0) {
                whichHeap = HEAP_TTF;
                is_swappable = true;
            } else if ((nameLen > 4 && strcmp(name+nameLen-4, ".dex") == 0) ||
                       (nameLen > 5 && strcmp(name+nameLen-5, ".odex") == 0)) {
                whichHeap = HEAP_DEX;
                is_swappable = true;
            } else if (nameLen > 4 && strcmp(name+nameLen-4, ".oat") == 0) {
                whichHeap = HEAP_OAT;
                is_swappable = true;
            } else if (nameLen > 4 && strcmp(name+nameLen-4, ".art") == 0) {
                whichHeap = HEAP_ART;
                is_swappable = true;
            } else if (strncmp(name, "[anon:", 6) == 0) {
                whichHeap = HEAP_UNKNOWN;
            } else if (nameLen > 0) {
                whichHeap = HEAP_UNKNOWN_MAP;
            } else if (start == prevEnd && prevHeap == HEAP_SO) {
                // bss section of a shared library.
                whichHeap = HEAP_SO;
            }
        }

        //ALOGI("native=%d dalvik=%d sqlite=%d: %s\n", isNativeHeap, isDalvikHeap,
        //    isSqliteHeap, line);

        shared_clean = 0;
        shared_dirty = 0;
        private_clean = 0;
        private_dirty = 0;
        swapped_out = 0;

        while (true) {
            if (fgets(line, 1024, fp) == 0) {
                done = true;
                break;
            }

            if (line[0] == 'S' && sscanf(line, "Size: %d kB", &temp) == 1) {
                size = temp;
            } else if (line[0] == 'R' && sscanf(line, "Rss: %d kB", &temp) == 1) {
                resident = temp;
            } else if (line[0] == 'P' && sscanf(line, "Pss: %d kB", &temp) == 1) {
                pss = temp;
            } else if (line[0] == 'S' && sscanf(line, "Shared_Clean: %d kB", &temp) == 1) {
                shared_clean = temp;
            } else if (line[0] == 'S' && sscanf(line, "Shared_Dirty: %d kB", &temp) == 1) {
                shared_dirty = temp;
            } else if (line[0] == 'P' && sscanf(line, "Private_Clean: %d kB", &temp) == 1) {
                private_clean = temp;
            } else if (line[0] == 'P' && sscanf(line, "Private_Dirty: %d kB", &temp) == 1) {
                private_dirty = temp;
            } else if (line[0] == 'R' && sscanf(line, "Referenced: %d kB", &temp) == 1) {
                referenced = temp;
            } else if (line[0] == 'S' && sscanf(line, "Swap: %d kB", &temp) == 1) {
                swapped_out = temp;
            } else if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %*s %*x %*x:%*x %*d", &start, &end) == 2) {
                // looks like a new mapping
                // example: "10000000-10001000 ---p 10000000 00:00 0"
                break;
            }
        }

        if (!skip) {
            if (is_swappable && (pss > 0)) {
                sharing_proportion = 0.0;
                if ((shared_clean > 0) || (shared_dirty > 0)) {
                    sharing_proportion = (pss - private_clean
                            - private_dirty)/(shared_clean+shared_dirty);
                }
                swappable_pss = (sharing_proportion*shared_clean) + private_clean;
            } else
                swappable_pss = 0;

            stats[whichHeap].pss += pss;
            stats[whichHeap].swappablePss += swappable_pss;
            stats[whichHeap].privateDirty += private_dirty;
            stats[whichHeap].sharedDirty += shared_dirty;
            stats[whichHeap].privateClean += private_clean;
            stats[whichHeap].sharedClean += shared_clean;
            stats[whichHeap].swappedOut += swapped_out;
            if (whichHeap == HEAP_DALVIK || whichHeap == HEAP_DALVIK_OTHER) {
                stats[subHeap].pss += pss;
                stats[subHeap].swappablePss += swappable_pss;
                stats[subHeap].privateDirty += private_dirty;
                stats[subHeap].sharedDirty += shared_dirty;
                stats[subHeap].privateClean += private_clean;
                stats[subHeap].sharedClean += shared_clean;
                stats[subHeap].swappedOut += swapped_out;
            }
        }
    }
}

static void load_maps(int pid, struct stats_t* stats)
{
    char tmp[128];
    FILE *fp;

    sprintf(tmp, "/proc/%d/smaps", pid);
    fp = fopen(tmp, "r");
    if (fp == 0) return;

    read_mapinfo(fp, stats);
    fclose(fp);
}

static void show_maps(int pid)
{
	struct stats_t stats[_NUM_HEAP];
	time_t now; 
	struct tm *timenow;
	time(&now);

	timenow = localtime(&now);

	memset(&stats, 0, sizeof(stats));

	load_maps(pid, stats);
/*
 printf("Time\t\tTotal\tNative\tDalvik\tDalvik_Other\tStack\t"
	"File\tUnknown_Map\tUnknown\tCursor\tAshmem\n");
 */
 	int file_map = 0;
	int total_map = 0;
	int i=0;

	for (i=HEAP_GL_DEV; i<HEAP_UNKNOWN_MAP; i++)
		file_map += stats[i].pss;
	for (i=0; i<_NUM_EXCLUSIVE_HEAP; i++) {
		total_map += stats[i].pss;
	}

	printf("%02d/%02d-%02d:%02d:%02d\t%d\t%d\t%d\t%d\t%d\t%d\t\t%d\t%d\t%d\n",
		timenow->tm_mon, timenow->tm_mday, timenow->tm_hour, timenow->tm_min, timenow->tm_sec,
		total_map, stats[HEAP_NATIVE].pss, stats[HEAP_DALVIK].pss+stats[HEAP_DALVIK_OTHER].pss,
		stats[HEAP_STACK].pss, file_map, stats[HEAP_UNKNOWN_MAP].pss, stats[HEAP_UNKNOWN].pss,
		stats[HEAP_CURSOR].pss, stats[HEAP_ASHMEM].pss);
}

enum {
    MEMINFO_TOTAL,
    MEMINFO_FREE,
    MEMINFO_BUFFERS,
    MEMINFO_CACHED,
    MEMINFO_SHMEM,
    MEMINFO_SLAB,
    MEMINFO_SWAP_TOTAL,
    MEMINFO_SWAP_FREE,
    MEMINFO_ZRAM_TOTAL,
    MEMINFO_MAPPED,
    MEMINFO_VMALLOC_USED,
    MEMINFO_PAGE_TABLES,
    MEMINFO_KERNEL_STACK,
    MEMINFO_COUNT
};

static const char* const tags[] = {
	"MemTotal:",
	"MemFree:",
	"Buffers:",
	"Cached:",
	"Shmem:",
	"Slab:",
	"SwapTotal:",
	"SwapFree:",
	"ZRam:",
	"Mapped:",
	"VmallocUsed:",
	"PageTables:",
	"KernelStack:",
	NULL
};

long meminfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int get_meminfo()
{
	char buffer[1024];
	int len = 0;
	int fd = -1;
	char* p = NULL;
	int numFound = 0;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0) {
	    printf("Unable to open /proc/meminfo: %s\n", strerror(errno));
	    return fd;
	}
	
	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	
	if (len < 0) {
	    printf("Empty /proc/meminfo\n");
	    return len;
	}
	buffer[len] = 0;

	p = buffer;
	while (*p && numFound < MEMINFO_COUNT) {
	    int i = 0;
	    while (tags[i]) {
	    	int tagsLen = strlen(tags[i]);
		if (strncmp(p, tags[i], tagsLen) == 0) {
		    p += tagsLen;
		    while (*p == ' ') p++;
		    char* num = p;
		    while (*p >= '0' && *p <= '9') p++;
		    if (*p != 0) {
			*p = 0;
			p++;
		    }
		    meminfo[i] = atoll(num);
		    numFound++;
		    break;
		}
		i++;
	    }
	    while (*p && *p != '\n') {
		p++;
	    }
	    if (*p) p++;
	}

	return 0;
}

#define MAX_MEMINFO	8192
void executeCMD(const char *cmd, char *result)   
{   
    char buf_ps[1024];
    char ps[1024]={0};
    FILE *ptr;
    strcpy(ps, cmd);
    if((ptr=popen(ps, "r"))!=NULL)
    {
        while(fgets(buf_ps, 1024, ptr)!=NULL)
        {
           strcat(result, buf_ps);
           if(strlen(result)>=MAX_MEMINFO)
               break;
        }
        pclose(ptr);
        ptr = NULL;
    }
    else
    {
        printf("popen %s error\n", ps);
    }
}

char meminfo_result[MAX_MEMINFO+1]="\0";

void get_pss(long* cached, long* used)
{
	char *p = NULL;

	memset(meminfo_result, 0, MAX_MEMINFO+1);
	executeCMD("dumpsys meminfo", meminfo_result);

	p=strstr(meminfo_result, "cached pss +");
	if (p) {
		while(*(--p)!='(')
			;
		*cached = atoll(p+1);
	}

	p=strstr(meminfo_result, "used pss +");
	if (p) {
		while(*(--p)!='(')
			;
		*used = atoll(p+1);
	}

	p=strstr(meminfo_result, "Total RAM:");
	if (p)
		*p = '\0';
}

long get_shrinker_kb()
{
	char buffer[4096];
	int len = 0;
	int fd = -1;
	char* p = NULL;
	char name[64];
	int pages = 0;
	long shrinker_pages = 0;

	fd = open("/sys/kernel/debug/shrinker", O_RDONLY);
	if (fd < 0) {
		printf("Unable to open /sys/kernel/debug/shrinker: %s\n",
			strerror(errno));
		return fd;
	}
	
	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	
	if (len < 0) {
		printf("Empty /sys/kernel/debug/shrinker\n");
		return len;
	}
	buffer[len] = 0;

	p = buffer;
	while (p && *p) {
		char* s;
		int f = sscanf(p, "%s %d", name, &pages);
		if (f!=2) break;
//		printf("%d== %s %d\n", f, name, pages);
		if (strcmp("lowmem_shrink", name))
			shrinker_pages += pages;
		p = strchr(p, '\n');
		if (p) ++p;
	}

//	printf("Shrinkable: %ld KB\n", shrinker_pages*4);

	return shrinker_pages*4;
}

long get_ion_memory_kb(char* heapname)
{
	char buffer[4096];
	int len = 0;
	int fd = -1;
	char* p = NULL;
	long size=0;
	char filename[128];

	sprintf(filename, "/sys/kernel/debug/ion/heaps/%s", heapname);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open %s: %s\n", filename, strerror(errno));
		return fd;
	}
	
	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	
	if (len < 0) {
		printf("Empty %s\n", filename);
		return len;
	}
	buffer[len] = 0;

	p=strstr(buffer, "total    ");
	if (p) {
		size = atoll(p+6);
	}

	return size/1024;
}

const char* gpu_pvr_mem = "/sys/kernel/debug/pvr/driver_stats";
const char* gpu_mali_mem = "/sys/kernel/debug/mali/gpu_memory";
const char* gpu_mali400_mem = "/sys/kernel/debug/mali/memory_usage";

long get_gpu_memory_kb()
{
	char buffer[1024];
	int len = 0;
	int fd = -1;
	char* p = NULL;
	long size=0;
	const char* filename = NULL;

	if (access(gpu_mali_mem, F_OK)==0)
		filename = gpu_mali_mem;
	else if (access(gpu_pvr_mem, F_OK)==0)
		filename = gpu_pvr_mem;
	else if (access(gpu_mali400_mem, F_OK)==0)
		filename = gpu_mali400_mem;

	if (filename==NULL) {
		printf("Can't find gpu memory node\n");
		return -1;
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open %s: %s\n", filename, strerror(errno));
		return fd;
	}

	while ((len = read(fd, buffer, sizeof(buffer)-1))>0) {
		if (filename == gpu_mali_mem) {
			if ((p=strstr(buffer, "mali0 "))!=NULL) {
				size = atoll(p+6)*4;
				break;
			} else if ((p=strstr(buffer, "Mali mem usage:"))!=NULL) {
				size = atoll(p+15)/1024;
				break;
			}
		} else if (filename == gpu_pvr_mem) {
			p=strstr(buffer, "MemoryUsageTotalAlloc");
			if (p) {
				size = atoll(p+22)/1024;
				break;
			}
		} else if (filename == gpu_mali400_mem) {
			size = atoll(buffer)/1024;
			break;
		}
	}
	close(fd);

	return size;
}

long get_vmallocinfo()
{
/*
  vmalloc
  vmap, but exclued ion_heap_map_kernel
*/
	char line[256];
	long vmalloc_size = 0;
	FILE* fp;

	fp = fopen("/proc/vmallocinfo", "r");
	if (fp == NULL) {
		printf("Unable to open /proc/vmallocinfo: %s\n",
			strerror(errno));
		return -1;
	}

	while(fgets(line, 256, fp) != NULL) {
		if (strstr(line, "vmalloc\n") ||
		    (strstr(line, "vmap\n") && !strstr(line, "ion_heap_map_kernel")))
		{
			long size;
			sscanf(line, "%*x-%*x %ld %*s", &size);
			vmalloc_size += size;
		}
	}

	fclose(fp);

	return vmalloc_size/1024;
}

long get_ddr_capacity()
{
	char line[256];
	long ddr_capacity = 0;
	FILE* fp;
	char* p;

	fp = fopen("/proc/zoneinfo", "r");
	if (fp == NULL) {
		printf("Unable to open /proc/zoneinfo: %s\n",
			strerror(errno));
		return -1;
	}

	while(fgets(line, 256, fp) != NULL) {
		if ((p=strstr(line, "present ")))
		{
			long size = atoll(p+7);
			ddr_capacity += size;
		}
	}

	fclose(fp);

	return ddr_capacity*4;
}

void print_usage(const char* app)
{
	printf("%s [-d sec] [-p] [-x] [-h] [-m] [-f output file]\n", app);
	exit(0);
}

long ion_cma;
long ion_vmalloc;
long shrinker;
long gpu_mem;
long used_pss;
long cached_pss;

long ion_mem;
long pss;
long cached_kernel;
long used_kernel;
long free_ram;
long used_ram;
long lost_ram;
long vmalloc_size;
long total_ram; // how many memory can management by kernel after initialize.
long ddr_capacity; // loader tell kernel that how many memory can use.
char model[PROPERTY_VALUE_MAX];

int main(int argc, char* argv[])
{
	int excel = 0;
	int delay = 0;
	int monitor_mode = 0;
	char out_file[260];
	FILE* fp = stdout;
	int pid = 0;

	printf("# Memory report %s\n", VER);
	printf("# \n");

	while (1) {
		int c;
		char *p;
		static struct option opts[] = {
			{"excel", no_argument, 0, 'x'},
			{"help", no_argument, 0, 'h'},
			{"delay", required_argument, 0, 'd'},
			{"monitor", no_argument, 0, 'm'},
			{"file", required_argument, 0, 'f'},
			{"pid", required_argument, 0, 'p'},
		};
		int i = 0;
		c = getopt_long(argc, argv, "p:d:f:xhm", opts, &i);
		if (c == -1)
			break;

		switch (c) {
		case 'x':
			excel = 1;
			break;
		case 'd':
			delay = atol(optarg);
			break;
		case 'm':
			monitor_mode = 1;
			if (delay==0) delay = 1;
			break;
		case 'f':
			sprintf(out_file, "%s", optarg);
			fp = fopen(out_file, "w");
			break;
		case 'p':
			pid = atol(optarg);
			if (delay==0) delay = 1;
			break;
		case 'h':
		default:
			print_usage(argv[0]);
			break;
		}
	}

	if (fp == NULL) {
		fprintf(stderr, "File %s is not exist and create failed! use stdout\n", out_file);
		fp = stdout;
	}

	do {
		if (pid) {
			static int show_head = 1;

			if (show_head) {
				printf("Time\t\tTotal\tNative\tDalvik\tStack\t"
				       "File\tOther_Map\tUnknown\tCursor\tAshmem\n");
				show_head = 0;
			}

			show_maps(pid);
			sleep(delay);
			continue;
		}

		if (!monitor_mode)
			get_pss(&cached_pss, &used_pss);
		get_meminfo();
		shrinker = get_shrinker_kb();
		ion_cma = get_ion_memory_kb("cma");
		ion_vmalloc = get_ion_memory_kb("vmalloc");
		gpu_mem = get_gpu_memory_kb();
		vmalloc_size = get_vmallocinfo();
		ddr_capacity = get_ddr_capacity();
		total_ram = meminfo[MEMINFO_TOTAL];
		property_get("ro.product.model", model, "????????");

		cached_kernel = meminfo[MEMINFO_BUFFERS]+meminfo[MEMINFO_CACHED]-meminfo[MEMINFO_MAPPED]-meminfo[MEMINFO_SHMEM];
		used_kernel = meminfo[MEMINFO_SHMEM]+meminfo[MEMINFO_SLAB]+meminfo[MEMINFO_KERNEL_STACK]+meminfo[MEMINFO_PAGE_TABLES]+vmalloc_size;
		pss = used_pss+cached_pss;
		ion_mem = ion_cma+ion_vmalloc;

		free_ram = meminfo[MEMINFO_FREE]+cached_kernel+shrinker;
		used_ram = pss+used_kernel+ion_mem+gpu_mem;

		if (delay) {
			static int show_head = 1;
			time_t now; 
			struct tm *timenow;
			time(&now);
			timenow = localtime(&now);

			if (show_head) {
				fprintf(fp, "# %13s: %s\n", "Model", model);
				fprintf(fp, "# %13s: %ld KB\n", "DDR Capacity", ddr_capacity);
				fprintf(fp, "# %13s: %ld KB\n", "Total RAM", total_ram);
				fprintf(fp, "# \n");
				if (monitor_mode) {
					fprintf(fp, "Time\t\tTotalFree\tFree\tCached\tBuffers\tShrinker\tMapped\tShmem\tSlab\tVmalloc\tStack\tPageTables\tION\tGPU\tOTHER\n");
				} else {
					fprintf(fp, "Time\t\tFree RAM(KB)\t\t\tUsed RAM(KB)\t\t\t\tLost RAM(KB)\n");
					fprintf(fp, "\t\ttotal\tfree\tcached\tshrink\ttotal\tpss\tkernel\tion\tgpu\n");
				}
				show_head = 0;
			}
			if (monitor_mode) {
				fprintf(fp, "%02d/%02d-%02d:%02d:%02d\t%ld\t\t%ld\t%ld\t%ld\t%ld\t\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t\t%ld\t%ld\t%ld\n",
					timenow->tm_mon, timenow->tm_mday, timenow->tm_hour, timenow->tm_min, timenow->tm_sec,
					free_ram, meminfo[MEMINFO_FREE], meminfo[MEMINFO_CACHED], meminfo[MEMINFO_BUFFERS], shrinker,
					meminfo[MEMINFO_MAPPED], meminfo[MEMINFO_SHMEM], meminfo[MEMINFO_SLAB], vmalloc_size,
					meminfo[MEMINFO_KERNEL_STACK], meminfo[MEMINFO_PAGE_TABLES], ion_mem, gpu_mem,
					total_ram-free_ram-used_ram);
			} else {
				fprintf(fp, "%02d:%02d:%02d\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
					timenow->tm_hour, timenow->tm_min, timenow->tm_sec,
					free_ram, meminfo[MEMINFO_FREE], cached_kernel, shrinker,
					used_ram, pss, used_kernel, ion_mem, gpu_mem,
					total_ram-free_ram-used_ram);
			}
			fflush(fp);
			sleep(delay);
			continue;
		}

		if (meminfo_result[0])
			fprintf(fp, "%s", meminfo_result);

		fprintf(fp, "%13s: %ld KB (%ld buffers + %ld cached - %ld mapped - %ld shmem)\n",
		       "cached kernel", cached_kernel,
		       meminfo[MEMINFO_BUFFERS], meminfo[MEMINFO_CACHED],
		       meminfo[MEMINFO_MAPPED], meminfo[MEMINFO_SHMEM]);
		fprintf(fp, "%13s: %ld KB (%ld vmalloc + %ld shmem + %ld slab + %ld stack + %ld pagetables)\n",
		       "used kernel", used_kernel,
		       vmalloc_size, meminfo[MEMINFO_SHMEM], meminfo[MEMINFO_SLAB],
		       meminfo[MEMINFO_KERNEL_STACK], meminfo[MEMINFO_PAGE_TABLES]);
		fprintf(fp, "%13s: %ld KB (%ld used pss + %ld cached pss)\n", "pss",
		       pss, used_pss, cached_pss);
		fprintf(fp, "\n");
		fprintf(fp, "%13s: %s\n", "Model", model);
		fprintf(fp, "%13s: %ld KB\n", "DDR Capacity", ddr_capacity);
		fprintf(fp, "%13s: %ld KB\n", "Total RAM", total_ram);
		fprintf(fp, "%13s: %ld KB (%ld free + %ld cached kernel + %ld shrinker)\n",
		       "Free RAM", free_ram, meminfo[MEMINFO_FREE], cached_kernel, shrinker);
		fprintf(fp, "%13s: %ld KB (%ld pss + %ld used kernel + %ld ion + %ld gpu)\n",
		       "Used RAM", used_ram, pss, used_kernel, ion_mem, gpu_mem);
		fprintf(fp, "%13s: %ld KB\n", "Lost RAM", total_ram-free_ram-used_ram);

		if (excel) {
			fprintf(fp, "\nOutput for excel:\n\n");
			fprintf(fp, "Model\tDDR Capacity(KB)\tTotal RAM(KB)\tFree RAM(KB)\t\t\t\tUsed RAM(KB)\t\t\t\t\tLost RAM(KB)\n");
			fprintf(fp, "\t\t\ttotal\tfree\tcached\tshrinker\ttotal\tpss\tkernel\tion\tgpu\n");
			fprintf(fp, "%s\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
				model, ddr_capacity, total_ram,
				free_ram, meminfo[MEMINFO_FREE], cached_kernel, shrinker,
				used_ram, pss, used_kernel, ion_mem, gpu_mem,
				total_ram-free_ram-used_ram);
		}
	} while(delay);

	if (fp > stderr)
		fclose(fp);
	return 0;
}

