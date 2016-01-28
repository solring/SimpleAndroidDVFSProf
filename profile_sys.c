#include<stdio.h>
#include<stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
//#include <time.h>

#ifndef CPU_NUM
#define CPU_NUM	4
#endif

#define SAMPLE_UNIT_UTIME 100000  /* 10Hz */
//#define SAMPLE_UNIT_UTIME 0 
#define SAMPLE_UNIT_SECOND 0
//#define SAMPLE_UNIT_SECOND 3

//#define CONFIG_HZ 100
#define BUFF_SIZE 513
#define PID_MAX 32768
#define DEFAULT_MAX_FREQ 1555200

/** For thread profiling **/
typedef struct threadConf{
	int pid;
	char name[65];
	char state;
	int gid;
	int uid;
	unsigned long int utime;
	unsigned long int stime;
	unsigned long int old_utime;
	unsigned long int old_stime;
	long int prio;
	long int nice;
	int last_cpu;
}ThreadConf;

ThreadConf thread;
int target_pid;
/************************/

struct timeval oldTime;
struct timeval now;
struct itimerval tick;
int stopFlag = 0;

unsigned long long CPUInfo[CPU_NUM][10];

char buff[129];
char outfile_name[129];
int curFreq, maxFreq;
char cpu_on[5];
double util[CPU_NUM];
unsigned long long lastwork[CPU_NUM];
unsigned long long workload[CPU_NUM];
unsigned long long curwork[CPU_NUM];
unsigned long long idle[CPU_NUM];
unsigned long long lastidle[CPU_NUM];
//double util0, util1, util2, util3; 
//unsigned long long lastwork0,lastwork1,lastwork2,lastwork3, workload0, workload1, workload2, workload3;
//unsigned long long curwork0, curwork1, curwork2, curwork3;
//unsigned long long idle0, idle1, idle2, idle3, lastidle0, lastidle1, lastidle2, lastidle3;
unsigned long long processR, ctxt, lastCtxt;

/**** For timer  ****/
long duration;
time_t starttime;

/**************** non-blocking input *************/
struct termios orig_termios;

int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(0, &fds); //STDIN_FILENO is 0
    select(1, &fds, NULL, NULL, &tv);
    return FD_ISSET(0, &fds);
}

void reset_terminal_mode()
{
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    //atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}


/****************************************************/


void initThreadConf(){
	thread.pid = thread.utime = thread.stime = thread.prio = thread.nice = 0;
	thread.old_stime = thread.old_utime = 0;
	thread.gid = thread.uid = -1;
	thread.state = '\0';
	thread.name[0] = '\0';
	thread.last_cpu = -1;
}

void getCPUMaxFreq(int cpu_num){

	FILE *fp;
	char buff[128];
	
	sprintf(buff, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu_num);
	fp = fopen(buff, "r");
	if(fp == NULL){
		printf("error: can't open scaling_max_freq\n");
		maxFreq = DEFAULT_MAX_FREQ;
		return;
	}else{
		printf("get max frequency\n");
		fscanf(fp, "%d", &maxFreq);
	}
	fclose(fp);
} 

void parse(){

	int i, j;
	long int tmpl;
	char buff[128], buff2[32], cpuname[5], filter[128];
	struct timeval t;
	struct timezone timez;
	float elapseTime;
	unsigned long tmp;
	

	
	/////////////////////////////////////////////
	
	/* profile cpu frequency */
	FILE *fp_freq = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
	fscanf(fp_freq, "%d", &curFreq);
	fclose(fp_freq);
	
	/* profile # of cpu on */
	FILE *fp_cpuon = fopen("/sys/devices/system/cpu/online", "r");
	fscanf(fp_cpuon, "%s", cpu_on);
	fclose(fp_cpuon);
		
	FILE *fp_out = fopen(outfile_name, "a");
	FILE *fp = fopen("/proc/stat","r");
	if(fp == NULL){
		printf("/proc/stat open fail\n");
	}

	/* profile cpu util. */
	while(fgets(buff, 128, fp))
	{
		for(i=0; i<CPU_NUM; i++){
			workload[i] = 0;
			sprintf(cpuname, "cpu%d ", i);
			if(strstr(buff, cpuname)){
				
				sprintf(filter, "cpu%d %%llu %%llu %%llu %%llu %%llu %%llu %%llu %%llu %%llu %%llu", i);
				sscanf(buff, filter, &CPUInfo[i][0], &CPUInfo[i][1], &CPUInfo[i][2], &CPUInfo[i][3], &CPUInfo[i][4], &CPUInfo[i][5], &CPUInfo[i][6], &CPUInfo[i][7], &CPUInfo[i][8], &CPUInfo[i][9]); // time(unit: jiffies) spent of all cpus for: user nice system idle iowait irq softirq stead guest
				workload[i] = CPUInfo[i][0]+CPUInfo[i][1]+CPUInfo[i][2]+CPUInfo[i][4]+CPUInfo[i][5]+CPUInfo[i][6]+CPUInfo[i][7];
				idle[i] = CPUInfo[i][3] - lastidle[i];
				curwork[i] = workload[i] - lastwork[i];
				util[i] = (double)curwork[i] /(double)(curwork[i]+idle[i]);
#ifdef DEBUG				
				printf("workload: %llu, lastwork: %llu, idle: %lld, idle diff: %llu, curwork: %llu, util: %.2f\n", workload[i], lastwork[i], CPUInfo[i][3], idle[i], curwork[i], util[i]);
#endif
				lastwork[i] = workload[i];
				lastidle[i] = CPUInfo[i][3];
			}
		}

		if(strstr(buff, "procs_running"))
		{
			sscanf(buff, "procs_running %llu", &processR); // # of processes running
		}
		if(strstr(buff, "ctxt"))
		{
			sscanf(buff, "ctxt %llu", &ctxt); // # of processes running
		}
	}
	
	for(i=0; i<CPU_NUM; i++){
		/* normalization */
		util[i]= util[i] * curFreq / maxFreq;
	}
	
	/* output */
	fprintf(fp_out, "%s, %d", cpu_on, curFreq);
	for(i=0; i<CPU_NUM; i++){
		fprintf(fp_out, ", %llu, %.2f", curwork[i], util[i]);
	}
	fprintf(fp_out, ", %llu, %llu\n", processR, ctxt-lastCtxt);

#ifdef DEBUG
	printf( "%s, %d", cpu_on, curFreq);
	for(i=0; i<CPU_NUM; i++)
		printf( ", %llu, %.2f", curwork[i], util[i]);
	printf(", %llu, %llu\n", processR, ctxt-lastCtxt);
#endif
	
	/* update data */
	lastCtxt = ctxt;

	fclose(fp_out);
	fclose(fp);


    /* For timer */
    gettimeofday(&now, NULL);
    long diff = (now.tv_sec-oldTime.tv_sec)*1000000 + now.tv_usec - oldTime.tv_usec;

    if(duration!=0 && diff > duration*1000000){
        printf("Times up!");
        stopFlag = 1;
    }

	if(stopFlag == 1)
	{
		tick.it_value.tv_sec = 0;
		tick.it_value.tv_usec = 0;
		tick.it_interval.tv_sec = 0;
		tick.it_interval.tv_usec = 0;
		setitimer(ITIMER_REAL, &tick, NULL);
		
		FILE *fp_out = fopen(outfile_name, "a");
		fprintf(fp_out, "end\n");
		fclose(fp_out);
		
		printf("End!!!\n");
		stopFlag = 2;
		exit(0);
	}
}


int main(int argc, char **argv)
{
	int i, res;
	char c;
	struct timezone timez;
    duration = 0;
//#ifdef TARGET_ONLY

    if(argc < 4){
		printf("usage: %s [second] [utime] [output file] [duration(optional)]\n", argv[0]);
		return;
	}else{
		sscanf(argv[1], "%d", &tick.it_value.tv_sec);
		sscanf(argv[2], "%d", &tick.it_value.tv_usec);
		strcpy(outfile_name, argv[3]);	
		tick.it_interval.tv_sec = tick.it_value.tv_sec;
		tick.it_interval.tv_usec = tick.it_value.tv_usec;
	}
	if(argc ==5){
		sscanf(argv[4], "%ld", &duration);
	}

//#endif

	/* initialize */
	for(i=0; i<CPU_NUM; i++){
		lastwork[i] = workload[i] = idle[i] = lastidle[i] = 0;
	}
	ctxt = lastCtxt = 0;
	
	
	FILE *fp_out = fopen(outfile_name, "a");
	fprintf(fp_out, "cpu_on, curFreq");
	for(i=0; i<CPU_NUM; i++){
		fprintf(fp_out, ", work_cycles%d, util%d", i, i);
	}
		
	fprintf(fp_out, ", proc_running, ctxt\n");
	fclose(fp_out);
/*	
	FILE *fp_thout = fopen("/sdcard/cuckoo_thprof.csv", "a");
	fprintf(fp_thout, "pid, gid, state, utime, stime, util, priority, nice value, last_cpu\n");
	fclose(fp_thout);
*/	
	
	getCPUMaxFreq(0);

	/**
	 * Timer interrupt handler
	 */
	signal(SIGALRM, parse);             /* SIGALRM handeler */
	
	res = setitimer(ITIMER_REAL, &tick, NULL);
	gettimeofday(&oldTime, &timez);
	printf("start!\n");


	//reset_terminal_mode();
	if(duration == 0){
		//set_conio_terminal_mode();
		while(1)
		{   	
			//while(!kbhit()){} //non-blocking check input

			c = fgetc(stdin);
			if(c == 'p') 
			{   
				stopFlag = 1;
				break;
			}   
		}
		//reset_terminal_mode();
	}else{
        printf("profile for %ld seconds...\n", duration);
		//sleep(duration);
        //stopFlag = 1;
	}
    
    while(stopFlag != 2){
        sleep(duration);
    }
	
	return 0;
}


