#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/statfs.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define __USE_GNU 1
#include <fcntl.h>


char controlling_host_name[100];
int page_size;
pid_t mypid;
long test_time = 7*24*60*60;	// default 1 hour test time
long startup_time;
long current_time;

unsigned int max_write_time = 0, max_read_time = 0;
unsigned int min_write_time = 0xffffffff, min_read_time=0xffffffff;
long long total_sector_write, total_sector_read;

long test_file_max_sector;
long test_file_sector;		// sector unit
int test_chunk_count = 0;
long test_chunk_sector[32];	// sector unit
int test_case;
int test_file_open_flag = O_RDWR|O_SYNC|O_DIRECT|O_CREAT;
//int test_file_open_flag = O_RDWR|O_SYNC|O_CREAT;

char test_file_name[128];

/* test result used */
char test_result_name[128];
float w_speed[100];
unsigned int w_speed_unit[100];
float r_speed[100];
unsigned int r_speed_unit[100];


const char *speed_unit_str_list[] = {"KB", "MB", "GB", "TB"};

#ifdef DEBUG
#define DBGMSG(fmt,...)	printf(fmt, ##__VA_ARGS__)
#else
#define DBGMSG(fmt...)
#endif

typedef void (*sighandler_t)(int);
typedef int (*test_function_t)(void);

#define SECTOR_SIZE		512
#define TEST_FILE_NAME  	"data.tmp"
#define TEST_RESULT_NAME  	"result"
#define TEST_RESULT_SUBFIX	".csv"
#define DUMP_ERR_INFO()		dump_err_info(__FILE__, __FUNCTION__, __LINE__)


/* Test function error return value */
#define FUNCTION_PARAMETER_ERROR		0x1
#define FUNCTION_FILE_OP_ERROR			0x2
#define FUNCTION_INTERRUPT_ERROR		0x3

void init_global_var(void)
{
	struct statfs diskInfo;

	page_size=getpagesize();
	gethostname(controlling_host_name,100);
	mypid=getpid(); 	/* save the master's PID */
	startup_time = time(0);

	statfs("./", &diskInfo);
	unsigned long long blocksize = diskInfo.f_bsize;    // block size  
	unsigned long long totalsize = blocksize * diskInfo.f_blocks;   // total size
	unsigned long long freeDisk = diskInfo.f_bfree * blocksize;	// free size
	unsigned long long availableDisk = diskInfo.f_bavail * blocksize; // avail size
	test_file_sector = availableDisk/SECTOR_SIZE*9/10;
	test_file_max_sector = availableDisk/SECTOR_SIZE;

	sprintf(test_file_name, "%s", TEST_FILE_NAME);

	memset(test_chunk_sector, 0x0, sizeof(test_chunk_sector));
	
	total_sector_write = 0;
	total_sector_read = 0;

	DBGMSG("Hostname: %s\n", controlling_host_name);
	DBGMSG("Pagesize: %d\n", page_size);
	DBGMSG("Pid: %ld\n", (long)mypid);
	DBGMSG("Startup test time: %ld\n", startup_time);
	DBGMSG("Disk status: %lld//%lld, %d%% free\n", 
		totalsize/SECTOR_SIZE, availableDisk/SECTOR_SIZE, (int)(availableDisk*100/totalsize));
	DBGMSG("Test file sector: %ld\n", test_file_sector);
	DBGMSG("Size information: \n\tunsigned char *: [%ld]\n", sizeof(unsigned char *));
	DBGMSG("\tvoid *[%ld]\n", sizeof(void *));
	DBGMSG("\tlong unsigned int[%ld]\n", sizeof(long unsigned int));
}

void dump_err_info(const char *file, const char *fun_name, const int line)
{
	printf("[ERROR]file: %s, function: %s, line: %d, errno: %d[%s]\n", 
		file, fun_name, line, errno, strerror(errno));
}

void dump_test_info(void)
{
	printf("Summay of write status:\n");
	printf("\tWrite response time max: %d ns, min: %d ns\n", max_write_time, min_write_time);
	printf("\tRead response time max: %d ns, min: %d ns\n", max_read_time, min_read_time);
	printf("\tTotal sector write: %lld\n", total_sector_write);
	printf("\tTotal sector read: %lld\n", total_sector_read);
}

void dump_excel(char *name)
{
	int i, j;
	int fd = open(name, O_CREAT|O_RDWR|O_TRUNC, S_IRWXU);
	char buff[512];
	ssize_t bytes_write;
	int max_unit;
	int read_unit;
	int write_unit;

	if(fd == -1){
		printf("Open %s file fail\n", name);
		DUMP_ERR_INFO();
		return;
	}

	max_unit = 0;
	for(i=0; i<sizeof(w_speed)/sizeof(float); i++){
		//__assert(w_speed_unit[i] <=3);
		if(w_speed_unit[i] > max_unit){
			max_unit = w_speed_unit[i];
		}
	}
	for(i=0; i<sizeof(w_speed)/sizeof(float); i++){
		while(w_speed_unit[i] < max_unit){
			w_speed_unit[i]++;
			w_speed[i] /= 1024;
		}
	}
	write_unit = max_unit;

	max_unit = 0;
	for(i=0; i<sizeof(r_speed)/sizeof(float); i++){
		//__assert(r_speed_unit[i] <= 3);
		if(r_speed_unit[i] > max_unit){
			max_unit = r_speed_unit[i];
		}
	}
	for(i=0; i<sizeof(r_speed)/sizeof(float); i++){
		while(r_speed_unit[i] < max_unit){
			r_speed_unit[i]++;
			r_speed[i] /= 1024;
		}
	}
	read_unit = max_unit;

	sprintf(buff, "%ldK write(%s)\n", test_chunk_sector[0]/2, speed_unit_str_list[write_unit]);
	bytes_write = write(fd, buff, strlen(buff));
	sprintf(buff, "%ldK,", test_chunk_sector[0]/2);	
	bytes_write = write(fd, buff, strlen(buff));
	for(i=0; i<sizeof(w_speed)/sizeof(float); i++){
		sprintf(buff, "%f,", w_speed[i]);
		bytes_write = write(fd, buff, strlen(buff));
	}

	sprintf(buff, "\n%ldK read(%s)\n", test_chunk_sector[0]/2, speed_unit_str_list[read_unit]);
	bytes_write = write(fd, buff, strlen(buff));
	sprintf(buff, "%ldK,", test_chunk_sector[0]/2);
	bytes_write = write(fd, buff, strlen(buff));
	for(i=0; i<sizeof(r_speed)/sizeof(float); i++){
		sprintf(buff, "%f,", r_speed[i]);
		bytes_write = write(fd, buff, strlen(buff));
	}

	sprintf(buff, "\n");
	bytes_write = write(fd, buff, strlen(buff));

	close(fd);
}

sighandler_t signal_handler(void)
{
	int err;
	printf("Receive interrupt or term signal\n");

	dump_test_info();

	err = unlink(test_file_name);
	if(err == -1){
		DUMP_ERR_INFO();
	}
	_exit(FUNCTION_INTERRUPT_ERROR);
	return NULL;
}

int usage(void)
{
	const char *help[] = {
		"./burnintest [-f file_size][-c chunk_size][-t test_type]",
		"	      [-d enabled][-s enabled][-t time][-h]",
		"	-f set the test file size, default 90% of free space",
		"	-c set one shot write size, default 4K size",
		"	-t set test type, 0: infinite sequence write full disk test",
		"			  1: infinite random write full disk test",
		"			  2: infinite write one address test",
		"			  3: infinite read one address test",
		"			  4: dump sequence write speed",
		"	-d direct io operation, default enable",
		"	-s sync operation, default enable",
		"	-l last test time, default 1h",
		"	-h print help information"
	};

	int i;
	for(i=0; i<sizeof(help)/sizeof(char **); i++){
		printf("%s\n", help[i]);
	}
	return 0;
}

int set_buffer_pattern(void *address, unsigned int len, unsigned int pattern)
{
	unsigned int *addr = (unsigned int *)address;
	unsigned int i;
	for (i = 0; i < len; i++) {
		addr[i] = pattern;
	}
	return 0;
}

void dump_memory(unsigned char *memory, unsigned int length, unsigned int num_perline)
{
	unsigned int i, j;
	for(i=0; i<length; i+=num_perline){
		for(j=0; j<num_perline; j++){
			if(i+j>=length){
				break;
			}
			printf("0x%-5x", memory[i+j]);
		}
		printf("\r\n");
	}
}

int check_buffer_pattern(void *address, unsigned int len, unsigned int pattern)
{
	unsigned int *addr = (unsigned int *)address;
	unsigned int i;
	for (i = 0; i < len; i++) {
		if (addr[i] != pattern) {
			/* Dump buffer pattern, and quit */
			printf("Data pattern check fail, len: 0x%x, pattern: 0%x, position: 0x%x\n", 
				len, pattern, i);
			dump_memory((unsigned char *)addr, len * 4, 16);
			return 1;
		}
	}
	return 0;
}

unsigned int get_speed(float *speed, unsigned long size_sector, unsigned long time_ms)
{
#if 1
	*speed = size_sector*1000000./2/time_ms/1024;
	return 1;
#else
	unsigned int unit = 0;
	*speed = size_sector*100*1000000/2/time_ms;
	do{		
		if(*speed/100 < 1024){			
			break;
		}
		*speed /= 1024;
		unit++;
	}while(unit<3);

	return unit;
#endif
}

int burnin_sequence_write(int type)
{
	int i, j;
	unsigned int *data_pattern;
	unsigned char *w_buffer, *r_buffer;
	unsigned long w_pos_sector = 0;
	unsigned char last_complete_percent = -1;
	unsigned char complete_percent = 0;
	int fd = open(test_file_name, test_file_open_flag, S_IRWXU);
	int status;
	unsigned int w_length_sector;
	unsigned int operation = 0;
	unsigned int tmp;

	unsigned long w_sector_sum;
	unsigned long w_time_sum;
	
	max_write_time = 0, max_read_time = 0;
	min_write_time = 0xffffffff, min_read_time=0xffffffff;

	struct timeval start_timestamp, end_timestamp;

	memset(r_speed, 0x0, sizeof(r_speed));
	memset(w_speed, 0x0, sizeof(w_speed));

	if(fd == -1){
		printf("Open %s file fail\n", test_file_name);
		DUMP_ERR_INFO();
		_exit(FUNCTION_FILE_OP_ERROR);
	}

	data_pattern = (unsigned int *)malloc(test_file_sector*sizeof(unsigned int));
	if(data_pattern == NULL){
		printf("malloc data pattern fail\n");
		goto BURNIN_SEQUENCE_WRITE_QUIT;
	}
	w_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	r_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	if((w_buffer == NULL) || (r_buffer == NULL)){
		printf("malloc read/write buffer fail\n");
		goto MALLOC_RW_BUFFER;
	}

	srand(time(NULL));
	for(i=0; i<test_file_sector; i++){
		data_pattern[i] = rand() * rand();
	}

	startup_time = time(NULL);
	current_time = startup_time;
	w_pos_sector = 0;
	w_length_sector = test_chunk_sector[0];
	w_sector_sum = 0;
	w_time_sum = 0;
	while(current_time - startup_time < test_time){
		if((w_pos_sector + w_length_sector) > test_file_sector){
			w_length_sector = test_file_sector - w_pos_sector;
			if(w_length_sector == 0){
				w_length_sector = test_chunk_sector[0];
				w_pos_sector = 0;
				status = lseek(fd, 0x0, SEEK_SET);
				if(status == -1){
					printf("Seek file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				operation++;
				if(((operation&0x1) == 0) && (type == 1)){
					break;
				}
				w_sector_sum = 0;
				w_time_sum = 0;
			}
		}

		if((operation&0x1) == 0){
			for(i=0; i<w_length_sector; i++){
				set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
			}

			
			gettimeofday(&start_timestamp, NULL);

			status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Write file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(test_file_open_flag & O_SYNC){
				status = fsync(fd);
				if(status == -1){
					printf("Sync write file fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
			}
			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_write_time < tmp){
				max_write_time = tmp;
			}
			if(min_write_time > tmp){
				min_write_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_write += w_length_sector;
		}else{
			
			gettimeofday(&start_timestamp, NULL);
			
			status = read(fd, r_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Read file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Read length less than excepted, read %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}

			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec*1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_read_time < tmp){
				max_read_time = tmp;
			}
			if(min_read_time > tmp){
				min_read_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_read += w_length_sector;
			
			for(i=0; i<w_length_sector; i++){
				status = check_buffer_pattern(r_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				if(status != 0){
					/* data verify fail */
					goto BURNIN_QUIT;
				}
			}
		}

		w_pos_sector += w_length_sector;
		w_sector_sum += w_length_sector;
		current_time = time(NULL);

		complete_percent = w_pos_sector*100/test_file_sector;
		if(complete_percent != last_complete_percent){			
			unsigned int speed_unit = 0;
			float speed;
			last_complete_percent = complete_percent;

			speed_unit = get_speed(&speed, w_sector_sum, w_time_sum);			
			printf("%s %3d complete, %8.3lf %s/s, elapse time(s): %-10ld\n", 
				((operation&0x1)==0)?"write":"read", last_complete_percent, 
				speed, speed_unit_str_list[speed_unit], current_time-startup_time);

			if((complete_percent >= 0) && (complete_percent < 100)){
				if(operation == 0){
					w_speed[complete_percent] = speed;
					w_speed_unit[complete_percent] = speed_unit;
				}else if(operation == 1){
					r_speed[complete_percent] = speed;
					r_speed_unit[complete_percent] = speed_unit;
				}
			}

			w_sector_sum = 0;
			w_time_sum = 0;
		}
	}

	sprintf(test_result_name, "%s_f_%ld(sector)_c_%ld(sector)%s", TEST_RESULT_NAME, 
		test_file_sector, test_chunk_sector[0], TEST_RESULT_SUBFIX);
	dump_excel(test_result_name);

BURNIN_QUIT:
	printf("\n");
MALLOC_RW_BUFFER:
	free(w_buffer);
	free(r_buffer);
	free(data_pattern);
BURNIN_SEQUENCE_WRITE_QUIT:
	close(fd);
	return status;
}

int burnin_infinited_write_addr(int type)
{
	int i, j;
	unsigned int *data_pattern;
	unsigned char *w_buffer, *r_buffer;
	unsigned long w_pos_sector = 0;
	unsigned char last_complete_percent = -1;
	unsigned char complete_percent = 0;
	int fd = open(test_file_name, test_file_open_flag, S_IRWXU);
	int status;
	unsigned int w_length_sector;
	unsigned int operation = 0;
	unsigned int tmp;

	unsigned long w_sector_sum;
	unsigned long w_time_sum;
	unsigned int w_random_count = 0;
	unsigned int w_infinited_write_addr = 0x0;
	
	max_write_time = 0, max_read_time = 0;
	min_write_time = 0xffffffff, min_read_time=0xffffffff;

	struct timeval start_timestamp, end_timestamp;

	memset(r_speed, 0x0, sizeof(r_speed));
	memset(w_speed, 0x0, sizeof(w_speed));

	if(fd == -1){
		printf("Open %s file fail\n", test_file_name);
		DUMP_ERR_INFO();
		_exit(FUNCTION_FILE_OP_ERROR);
	}

	data_pattern = (unsigned int *)malloc(test_file_sector*sizeof(unsigned int));
	if(data_pattern == NULL){
		printf("malloc data pattern fail\n");
		goto BURNIN_SEQUENCE_WRITE_QUIT;
	}
	w_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	r_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	if((w_buffer == NULL) || (r_buffer == NULL)){
		printf("malloc read/write buffer fail\n");
		goto MALLOC_RW_BUFFER;
	}

	srand(time(NULL));
	for(i=0; i<test_file_sector; i++){
		data_pattern[i] = rand() * rand();
	}

	startup_time = time(NULL);
	current_time = startup_time;
	w_pos_sector = 0;
	w_length_sector = test_chunk_sector[0];
	w_sector_sum = 0;
	w_time_sum = 0;
	while(current_time - startup_time < test_time){
		if((w_pos_sector + w_length_sector) >= test_file_sector){
			w_length_sector = test_file_sector - w_pos_sector;
			if(w_length_sector == 0){
				w_length_sector = test_chunk_sector[0];
				w_pos_sector = 0;
				status = lseek(fd, 0x0, SEEK_SET);
				if(status == -1){
					printf("Seek file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				operation++;
				if(((operation&0x1) == 0) && (type == 1)){
					break;
				}
				w_sector_sum = 0;
				w_time_sum = 0;
			}
		}

		if(operation == 0){
			for(i=0; i<w_length_sector; i++){
				set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
			}

			
			gettimeofday(&start_timestamp, NULL);

			status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Write file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(test_file_open_flag & O_SYNC){
				status = fsync(fd);
				if(status == -1){
					printf("Sync write file fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
			}
			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_write_time < tmp){
				max_write_time = tmp;
			}
			if(min_write_time > tmp){
				min_write_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_write += w_length_sector;
			
		}else if((operation&0x01) == 0){
			if(w_random_count < test_file_sector/test_chunk_sector[0]){
				w_random_count++;
				if(w_infinited_write_addr == 0){
					w_infinited_write_addr = (unsigned long)(rand()*rand()) % (test_file_sector-test_chunk_sector[0]);
				}
				w_pos_sector = w_infinited_write_addr; 
				w_length_sector = test_chunk_sector[0];

				//DBGMSG("Write %ld, len %d\n", w_pos_sector, w_length_sector);

				for(i=0; i<w_length_sector; i++){
					data_pattern[i+w_pos_sector] = rand() * rand();
					set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				}
			
				gettimeofday(&start_timestamp, NULL);

				status = lseek(fd, w_pos_sector*SECTOR_SIZE, SEEK_SET);
				if(status == -1){
					printf("Seek fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
	
				status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
				if(status == -1){
					printf("Write file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				if(status < w_length_sector*SECTOR_SIZE){
					printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
						status, w_length_sector * SECTOR_SIZE);
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				if(test_file_open_flag & O_SYNC){
					status = fsync(fd);
					if(status == -1){
						printf("Sync write file fail\n");
						DUMP_ERR_INFO();
						_exit(FUNCTION_FILE_OP_ERROR);
					}
				}
				gettimeofday(&end_timestamp, NULL);

				tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
				tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
				if(max_write_time < tmp){
					max_write_time = tmp;
				}
				if(min_write_time > tmp){
					min_write_time = tmp;
				}
				w_time_sum += tmp;
				
				total_sector_write += w_length_sector;
			}else{
				printf("Random write reach end\n");
				w_pos_sector = test_file_sector;
				w_length_sector = test_chunk_sector[0];
				w_random_count  = 0;
				continue;
			}
		}else{
			gettimeofday(&start_timestamp, NULL);
			
			status = read(fd, r_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Read file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Read length less than excepted, read %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}

			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec*1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_read_time < tmp){
				max_read_time = tmp;
			}
			if(min_read_time > tmp){
				min_read_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_read += w_length_sector;
			
			for(i=0; i<w_length_sector; i++){
				status = check_buffer_pattern(r_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				if(status != 0){
					/* data verify fail */
					goto BURNIN_QUIT;
				}
			}
		}

		w_pos_sector += w_length_sector;
		w_sector_sum += w_length_sector;
		current_time = time(NULL);

		if((operation == 0) || (operation & 0x01)){
			complete_percent = w_pos_sector*100/test_file_sector;
		}else{
			complete_percent = w_random_count * 100 / (test_file_sector/test_chunk_sector[0]);
		}
		if(complete_percent != last_complete_percent){
			unsigned int speed_unit = 0;
			float speed;
			last_complete_percent = complete_percent;

			speed_unit = get_speed(&speed, w_sector_sum, w_time_sum);			
			printf("%s %3d complete, %8.3lf %s/s, elapse time(s): %-10ld\n", 
				((operation&0x1)==0)?"write":"read", last_complete_percent, 
				speed, speed_unit_str_list[speed_unit], current_time-startup_time);

			if((complete_percent >= 0) && (complete_percent < 100)){
				if(operation == 0){
					w_speed[complete_percent] = speed;
					w_speed_unit[complete_percent] = speed_unit;
				}else if(operation == 1){
					r_speed[complete_percent] = speed;
					r_speed_unit[complete_percent] = speed_unit;
				}
			}

			w_sector_sum = 0;
			w_time_sum = 0;
		}
	}

	sprintf(test_result_name, "%s_f_%ld(sector)_c_%ld(sector)%s", TEST_RESULT_NAME, 
		test_file_sector, test_chunk_sector[0], TEST_RESULT_SUBFIX);
	dump_excel(test_result_name);

BURNIN_QUIT:
	printf("\n");
MALLOC_RW_BUFFER:
	free(w_buffer);
	free(r_buffer);
	free(data_pattern);
BURNIN_SEQUENCE_WRITE_QUIT:
	close(fd);
	return status;
}

int burnin_infinited_read_addr(int type)
{
	int i, j;
	unsigned int *data_pattern;
	unsigned char *w_buffer, *r_buffer;
	unsigned long w_pos_sector = 0;
	unsigned char last_complete_percent = -1;
	unsigned char complete_percent = 0;
	int fd = open(test_file_name, test_file_open_flag, S_IRWXU);
	int status;
	unsigned int w_length_sector;
	unsigned int operation = 0;
	unsigned int tmp;

	unsigned long w_sector_sum;
	unsigned long w_time_sum;
	unsigned int w_random_count = 0;
	unsigned long w_infinited_addr = 0;
	
	max_write_time = 0, max_read_time = 0;
	min_write_time = 0xffffffff, min_read_time=0xffffffff;

	struct timeval start_timestamp, end_timestamp;

	memset(r_speed, 0x0, sizeof(r_speed));
	memset(w_speed, 0x0, sizeof(w_speed));

	if(fd == -1){
		printf("Open %s file fail\n", test_file_name);
		DUMP_ERR_INFO();
		_exit(FUNCTION_FILE_OP_ERROR);
	}

	data_pattern = (unsigned int *)malloc(test_file_sector*sizeof(unsigned int));
	if(data_pattern == NULL){
		printf("malloc data pattern fail\n");
		goto BURNIN_SEQUENCE_WRITE_QUIT;
	}
	w_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	r_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	if((w_buffer == NULL) || (r_buffer == NULL)){
		printf("malloc read/write buffer fail\n");
		goto MALLOC_RW_BUFFER;
	}

	srand(time(NULL));
	for(i=0; i<test_file_sector; i++){
		data_pattern[i] = rand() * rand();
	}

	startup_time = time(NULL);
	current_time = startup_time;
	w_pos_sector = 0;
	w_length_sector = test_chunk_sector[0];
	w_sector_sum = 0;
	w_time_sum = 0;
	while(current_time - startup_time < test_time){
		if((w_pos_sector + w_length_sector) >= test_file_sector){
			w_length_sector = test_file_sector - w_pos_sector;
			if(w_length_sector == 0){
				w_length_sector = test_chunk_sector[0];
				w_pos_sector = 0;
				status = lseek(fd, 0x0, SEEK_SET);
				if(status == -1){
					printf("Seek file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				operation++;
				if(((operation&0x1) == 0) && (type == 1)){
					break;
				}
				w_sector_sum = 0;
				w_time_sum = 0;
			}
		}

		if(operation == 0){
			for(i=0; i<w_length_sector; i++){
				set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
			}

			
			gettimeofday(&start_timestamp, NULL);

			status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Write file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(test_file_open_flag & O_SYNC){
				status = fsync(fd);
				if(status == -1){
					printf("Sync write file fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
			}
			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_write_time < tmp){
				max_write_time = tmp;
			}
			if(min_write_time > tmp){
				min_write_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_write += w_length_sector;
		}else if((operation&0x01) == 0){
			if(w_random_count < test_file_sector/test_chunk_sector[0]){
				w_random_count++;

				if(w_infinited_addr == 0){
					w_infinited_addr = (unsigned long)(rand()*rand()) % (test_file_sector-test_chunk_sector[0]);
				}
				w_pos_sector = w_infinited_addr;
				w_length_sector = test_chunk_sector[0];

				gettimeofday(&start_timestamp, NULL);

				status = lseek(fd, w_pos_sector*SECTOR_SIZE, SEEK_SET);
				if(status == -1){
					printf("Seek fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
	
				status = read(fd, r_buffer, w_length_sector*SECTOR_SIZE);
				if(status == -1){
					printf("Read file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				if(status < w_length_sector*SECTOR_SIZE){
					printf("Read length less than excepted, write %d bytes, except: 0x%d\n", 
						status, w_length_sector * SECTOR_SIZE);
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				gettimeofday(&end_timestamp, NULL);

				tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
				tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
				w_time_sum += tmp;
				
				total_sector_read += w_length_sector;

				for(i=0; i<w_length_sector; i++){
					status = check_buffer_pattern(r_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
					if(status != 0){
						/* data verify fail */
						goto BURNIN_QUIT;
					}
				}
			}else{
				printf("Random read reach end\n");
				w_pos_sector = test_file_sector;
				w_length_sector = test_chunk_sector[0];
				w_random_count  = 0;
				continue;
			}
		}else{
			gettimeofday(&start_timestamp, NULL);
			
			status = read(fd, r_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Read file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Read length less than excepted, read %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}

			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec*1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_read_time < tmp){
				max_read_time = tmp;
			}
			if(min_read_time > tmp){
				min_read_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_read += w_length_sector;
			
			for(i=0; i<w_length_sector; i++){
				status = check_buffer_pattern(r_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				if(status != 0){
					/* data verify fail */
					goto BURNIN_QUIT;
				}
			}
		}

		w_pos_sector += w_length_sector;
		w_sector_sum += w_length_sector;
		current_time = time(NULL);

		if((operation == 0) || (operation & 0x01)){
			complete_percent = w_pos_sector*100/test_file_sector;
		}else{
			complete_percent = w_random_count * 100 / (test_file_sector/test_chunk_sector[0]);
		}
		if(complete_percent != last_complete_percent){
			last_complete_percent = complete_percent;
			if(w_time_sum == 0){				
				w_time_sum = 1;
			}
			unsigned int speed_unit = 0;
			float speed;
			last_complete_percent = complete_percent;

			speed_unit = get_speed(&speed, w_sector_sum, w_time_sum);			
			printf("%s %3d complete, %8.3lf %s/s, elapse time(s): %-10ld\n", 
				((operation&0x1)==0)?"write":"read", last_complete_percent, 
				speed, speed_unit_str_list[speed_unit], current_time-startup_time);

			if((complete_percent >= 0) && (complete_percent < 100)){
				if(operation == 0){
					w_speed[complete_percent] = speed;
					w_speed_unit[complete_percent] = speed_unit;
				}else if(operation == 1){
					r_speed[complete_percent] = speed;
					r_speed_unit[complete_percent] = speed_unit;
				}
			}

			w_sector_sum = 0;
			w_time_sum = 0;
		}
	}

	sprintf(test_result_name, "%s_f_%ld(sector)_c_%ld(sector)%s", TEST_RESULT_NAME, 
		test_file_sector, test_chunk_sector[0], TEST_RESULT_SUBFIX);
	dump_excel(test_result_name);

BURNIN_QUIT:
	printf("\n");
MALLOC_RW_BUFFER:
	free(w_buffer);
	free(r_buffer);
	free(data_pattern);
BURNIN_SEQUENCE_WRITE_QUIT:
	close(fd);
	return status;
}
int burnin_random_write(int type)
{
	int i, j;
	unsigned int *data_pattern;
	unsigned char *w_buffer, *r_buffer;
	unsigned long w_pos_sector = 0;
	unsigned char last_complete_percent = -1;
	unsigned char complete_percent = 0;
	int fd = open(test_file_name, test_file_open_flag, S_IRWXU);
	int status;
	unsigned int w_length_sector;
	unsigned int operation = 0;
	unsigned int tmp;

	unsigned long w_sector_sum;
	unsigned long w_time_sum;
	unsigned int w_random_count = 0;
	
	max_write_time = 0, max_read_time = 0;
	min_write_time = 0xffffffff, min_read_time=0xffffffff;

	struct timeval start_timestamp, end_timestamp;

	memset(r_speed, 0x0, sizeof(r_speed));
	memset(w_speed, 0x0, sizeof(w_speed));

	if(fd == -1){
		printf("Open %s file fail\n", test_file_name);
		DUMP_ERR_INFO();
		_exit(FUNCTION_FILE_OP_ERROR);
	}

	data_pattern = (unsigned int *)malloc(test_file_sector*sizeof(unsigned int));
	if(data_pattern == NULL){
		printf("malloc data pattern fail\n");
		goto BURNIN_SEQUENCE_WRITE_QUIT;
	}
	w_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	r_buffer = (unsigned char *)memalign(512, test_chunk_sector[0]*SECTOR_SIZE);
	if((w_buffer == NULL) || (r_buffer == NULL)){
		printf("malloc read/write buffer fail\n");
		goto MALLOC_RW_BUFFER;
	}

	srand(time(NULL));
	for(i=0; i<test_file_sector; i++){
		data_pattern[i] = rand() * rand();
	}

	startup_time = time(NULL);
	current_time = startup_time;
	w_pos_sector = 0;
	w_length_sector = test_chunk_sector[0];
	w_sector_sum = 0;
	w_time_sum = 0;
	while(current_time - startup_time < test_time){
		if((w_pos_sector + w_length_sector) >= test_file_sector){
			w_length_sector = test_file_sector - w_pos_sector;
			if(w_length_sector == 0){
				w_length_sector = test_chunk_sector[0];
				w_pos_sector = 0;
				status = lseek(fd, 0x0, SEEK_SET);
				if(status == -1){
					printf("Seek file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				operation++;
				if(((operation&0x1) == 0) && (type == 1)){
					break;
				}
				w_sector_sum = 0;
				w_time_sum = 0;
			}
		}

		if(operation == 0){
			for(i=0; i<w_length_sector; i++){
				set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
			}

			
			gettimeofday(&start_timestamp, NULL);

			status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Write file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(test_file_open_flag & O_SYNC){
				status = fsync(fd);
				if(status == -1){
					printf("Sync write file fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
			}
			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_write_time < tmp){
				max_write_time = tmp;
			}
			if(min_write_time > tmp){
				min_write_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_write += w_length_sector;
			
		}else if((operation&0x01) == 0){
			if(w_random_count < test_file_sector/test_chunk_sector[0]){
				w_random_count++;
				w_pos_sector = (unsigned long)(rand()*rand()) % (test_file_sector-test_chunk_sector[0]);
				w_length_sector = test_chunk_sector[0];

				//DBGMSG("Write %ld, len %d\n", w_pos_sector, w_length_sector);

				for(i=0; i<w_length_sector; i++){
					data_pattern[i+w_pos_sector] = rand() * rand();
					set_buffer_pattern(w_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				}
			
				gettimeofday(&start_timestamp, NULL);

				status = lseek(fd, w_pos_sector*SECTOR_SIZE, SEEK_SET);
				if(status == -1){
					printf("Seek fail\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
	
				status = write(fd, w_buffer, w_length_sector*SECTOR_SIZE);
				if(status == -1){
					printf("Write file error\n");
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				if(status < w_length_sector*SECTOR_SIZE){
					printf("Write length less than excepted, write %d bytes, except: 0x%d\n", 
						status, w_length_sector * SECTOR_SIZE);
					DUMP_ERR_INFO();
					_exit(FUNCTION_FILE_OP_ERROR);
				}
				if(test_file_open_flag & O_SYNC){
					status = fsync(fd);
					if(status == -1){
						printf("Sync write file fail\n");
						DUMP_ERR_INFO();
						_exit(FUNCTION_FILE_OP_ERROR);
					}
				}
				gettimeofday(&end_timestamp, NULL);

				tmp = start_timestamp.tv_sec * 1000000 +  start_timestamp.tv_usec;
				tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
				if(max_write_time < tmp){
					max_write_time = tmp;
				}
				if(min_write_time > tmp){
					min_write_time = tmp;
				}
				w_time_sum += tmp;
				
				total_sector_write += w_length_sector;
			}else{
				printf("Random write reach end\n");
				w_pos_sector = test_file_sector;
				w_length_sector = test_chunk_sector[0];
				w_random_count  = 0;
				continue;
			}
		}else{
			gettimeofday(&start_timestamp, NULL);
			
			status = read(fd, r_buffer, w_length_sector*SECTOR_SIZE);
			if(status == -1){
				printf("Read file error\n");
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}
			if(status < w_length_sector*SECTOR_SIZE){
				printf("Read length less than excepted, read %d bytes, except: 0x%d\n", 
					status, w_length_sector * SECTOR_SIZE);
				DUMP_ERR_INFO();
				_exit(FUNCTION_FILE_OP_ERROR);
			}

			gettimeofday(&end_timestamp, NULL);

			tmp = start_timestamp.tv_sec*1000000 +  start_timestamp.tv_usec;
			tmp = end_timestamp.tv_sec * 1000000 + end_timestamp.tv_usec - tmp;
			if(max_read_time < tmp){
				max_read_time = tmp;
			}
			if(min_read_time > tmp){
				min_read_time = tmp;
			}
			w_time_sum += tmp;
			
			total_sector_read += w_length_sector;
			
			for(i=0; i<w_length_sector; i++){
				status = check_buffer_pattern(r_buffer+SECTOR_SIZE*i, SECTOR_SIZE/4, data_pattern[i+w_pos_sector]);
				if(status != 0){
					/* data verify fail */
					goto BURNIN_QUIT;
				}
			}
		}

		w_pos_sector += w_length_sector;
		w_sector_sum += w_length_sector;
		current_time = time(NULL);

		if((operation == 0) || (operation & 0x01)){
			complete_percent = w_pos_sector*100/test_file_sector;
		}else{
			complete_percent = w_random_count * 100 / (test_file_sector/test_chunk_sector[0]);
		}
		if(complete_percent != last_complete_percent){
			unsigned int speed_unit = 0;
			float speed;
			last_complete_percent = complete_percent;

			speed_unit = get_speed(&speed, w_sector_sum, w_time_sum);			
			printf("%s %3d complete, %8.3lf %s/s, elapse time(s): %-10ld\n", 
				((operation&0x1)==0)?"write":"read", last_complete_percent, 
				speed, speed_unit_str_list[speed_unit], current_time-startup_time);

			if((complete_percent >= 0) && (complete_percent < 100)){
				if(operation == 0){
					w_speed[complete_percent] = speed;
					w_speed_unit[complete_percent] = speed_unit;
				}else if(operation == 1){
					r_speed[complete_percent] = speed;
					r_speed_unit[complete_percent] = speed_unit;
				}
			}

			w_sector_sum = 0;
			w_time_sum = 0;
		}
	}

	sprintf(test_result_name, "%s_f_%ld(sector)_c_%ld(sector)%s", TEST_RESULT_NAME, 
		test_file_sector, test_chunk_sector[0], TEST_RESULT_SUBFIX);
	dump_excel(test_result_name);

BURNIN_QUIT:
	printf("\n");
MALLOC_RW_BUFFER:
	free(w_buffer);
	free(r_buffer);
	free(data_pattern);
BURNIN_SEQUENCE_WRITE_QUIT:
	close(fd);
	return status;
}

int main(int argc, char **argv)
{
	int cret;
	int err;
	int enable;
	int i;
	init_global_var();
	signal((int) SIGINT, (sighandler_t) signal_handler); /* handle user interrupt */
    	signal((int) SIGTERM, (sighandler_t) signal_handler);	/* handle kill from shell */
	
	// get test param or default
	while((cret = getopt(argc,argv,"f:c:t:d:s:l:h")) != EOF){
		switch(cret){
			case 'f':
			test_file_sector = atol(optarg);
			if(optarg[strlen(optarg)-1]=='k' || optarg[strlen(optarg)-1]=='K'){
				test_file_sector *= 2;
			}
			if(optarg[strlen(optarg)-1]=='m' || optarg[strlen(optarg)-1]=='M'){
				test_file_sector *= 2*1024;
			}
			if(optarg[strlen(optarg)-1]=='g' || optarg[strlen(optarg)-1]=='G'){
				test_file_sector *= 2*1024*1024;
			}
			if(test_file_sector > test_file_max_sector){
				test_file_sector = test_file_max_sector;
			}
			DBGMSG("test_file_sector: %ld\n", test_file_sector);
			break;

			case 'c':
			test_chunk_sector[test_chunk_count] = atoi(optarg);
			if(optarg[strlen(optarg)-1]=='k' || optarg[strlen(optarg)-1]=='K'){
				test_chunk_sector[test_chunk_count] *= 2;
			}
			if(optarg[strlen(optarg)-1]=='m' || optarg[strlen(optarg)-1]=='M'){
				test_chunk_sector[test_chunk_count] *= 2*1024;
			}
			if(optarg[strlen(optarg)-1]=='g' || optarg[strlen(optarg)-1]=='G'){
				test_chunk_sector[test_chunk_count] *= 2*1024*1024;
			}
			DBGMSG("test_chunk_sector[%d]: %ld\n", 
				test_chunk_count, test_chunk_sector[test_chunk_count]);
			test_chunk_count++;
			break;
			
			case 'l':
			test_time = atoi(optarg);
			if(optarg[strlen(optarg)-1]=='h' || optarg[strlen(optarg)-1]=='H'){
				test_chunk_sector[test_chunk_count] *= 60*60;
			}
			if(optarg[strlen(optarg)-1]=='m' || optarg[strlen(optarg)-1]=='M'){
				test_chunk_sector[test_chunk_count] *= 60;
			}
			if(optarg[strlen(optarg)-1]=='s' || optarg[strlen(optarg)-1]=='S'){
				test_chunk_sector[test_chunk_count] *= 1;
			}
			DBGMSG("test_time: %ld\n", test_time);
			break;
			
			case 't':
			test_case = atoi(optarg);
			DBGMSG("Testcase: %d\n", test_case);
			break;
			
			case 'd':
			enable = atoi(optarg);
			if(enable){
				test_file_open_flag |= O_DIRECT;
			}else{
				test_file_open_flag &= ~O_DIRECT;
			}
			DBGMSG("Direct: %s\n", (test_file_open_flag & O_DIRECT)?"Enable":"Disable");
			break;
			
			case 's':
			enable = atoi(optarg);
			if(enable){
				test_file_open_flag |= O_SYNC;
			}else{
				test_file_open_flag &= ~O_SYNC;
			}
			DBGMSG("Sync: %s\n", (test_file_open_flag & O_SYNC)?"Enable":"Disable");
			break;
			
			case 'h':
			case '?':
			defult:
			usage();
			_exit(FUNCTION_PARAMETER_ERROR);
			break;
		}
	}

	/* parameter verify */
	if(test_chunk_count == 0){
		test_chunk_count++;
		test_chunk_sector[0] = 8;	// default 4K
	}
#if 0
	for(i=0; i<6; i++){
		test_chunk_sector[0] = 2<<(3+i);
		burnin_sequence_write(1);
	}
	//burnin_random_write(0);
	//burnin_infinited_write_addr(0);
	burnin_infinited_read_addr(0);
#else
	switch(test_case){
		case 0:
		burnin_sequence_write(0);
		break;

		case 1:
		burnin_random_write(0);
		break;

		case 2:
		burnin_infinited_write_addr(0);
		break;

		case 3:
		burnin_infinited_read_addr(0);
		break;

		case 4:
		default:
		for(i=0; i<7; i++){
			test_chunk_sector[0] = 2<<(2+i);
			err = burnin_sequence_write(1);
			if(err){
				break;
			}
		}
		break;
	}
#endif
	dump_test_info();
	err = unlink(test_file_name);
	if(err == -1){
		DUMP_ERR_INFO();
	}

	return 0;
}

