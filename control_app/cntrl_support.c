/*****
*  Stay resident program two: supporting componenet
*
*  Author: Yun Wang
*
*  I implemented three stay resident components in user-level to actually controls
*  the weaving machine through our developed device driver. This is compenent TWO
*  and it is responsible for:
*  1)Read .ini file to get the share memoery ID input from component ONE
*  2)Wait every 500ms(during weaving)or 2 seconds(other status)to check
*     if it is need to cache weaving data in the share memoery
*  Used N. Devillard's codes for parsing ini file.
*  
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*
******/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>
#include "thj.h"
#include "iniparser.h"
#include "iniparser.c"
#define LDebug

static int lock_fd;         /*文件锁*/
static int thj_fd;
static int P_ShmId;
static int NeedFetch;
ShareMem *P_ShareMem;       /*6264共享内存单元*/
Write6264 FlwData;
static int count=0;

/*功能*/
/*1.挂载共享内存*/
/*2.查询是否要提取花型—定时器 clear*/
/*3.读取花型文件送入6264-调用驱动写6264 */
/*挂载共享内存*/
static int f_attashm()
{
    dictionary * ini;
    char * shmbuf;
    /*读取配置文件*/
    ini = iniparser_load("thj.ini");
    stderr=fopen("thj.ini", "w");
    
    if(ini==NULL)
    {
        fprintf(stderr, "cannot parse file: thj.ini in f_MkShm, Please Check File!!!!\n");
        return -1;
    }
    P_ShmId = iniparser_getint(ini, "Temp:ShareMem", -1);
    if(-1==P_ShmId)
    {
        printf("cannot find ShareMemoryID from thj.ini!\n");
        return -1;
    }

    /*挂载到HM6264*/
    if(( shmbuf=shmat(P_ShmId,0,0))<(char *)0)
    {
        perror("shmat, in shmwrite ");
        return -1;
    } 
	printf("support component has matched share memory segment :%d\n",P_Shmld);
    P_ShareMem =(ShareMem*)shmbuf;
    iniparser_dump_ini(ini, stderr);
    fclose(stderr);
    iniparser_freedict(ini);
    return 1;
}

/*根据IntWine state设置不同间隔时间*/
static void f_set_interval(struct itimerval *timer)
{
    if(P_ShareMem->ExAll.Wine_End == TRUE && timer->it_value.tv_sec == 2)
    {
        /*设置500ms*/
        f_init_itimerval(timer,0,500000,0,500000);
    }
    else if(P_ShareMem->ExAll.Wine_End == FALSE && timer->it_value.tv_sec == 0)
    {
        /*设置2-3秒*/
        f_init_itimerval(timer,2,0,2,0);
    }
}

/*怕判断是否需要预取花型数据*/
static void f_check_fetch()
{	 
    /*不在编织中或缓冲区满,不需要预取*/
    if(P_ShareMem->ExAll.Wine_End  ==TRUE || P_ShareMem->Stay.Buffer_Full == TRUE)
        NeedFetch = FALSE;
    else
        NeedFetch = TRUE;
}

/*设置定时器时间*/
static void f_init_itimerval(struct itimerval * timer,int sec,int usec,int isec,int iusec)
{
    timer->it_value.tv_sec = sec;
    timer->it_value.tv_usec = usec;

    timer->it_interval.tv_sec = isec; 
    timer->it_interval.tv_usec = iusec;
    setitimer(ITIMER_REAL,timer,NULL);
}

/*预取数据到6264*/
static void f_preload()
{    
    int flw_fd,width,height,clrcount,flw_offset=80,lock_result;
    char buf[6];
    /*---test---*/
    int i;
    int prt_ok = FALSE;
    /*---test---*/    
    /*打开花型文件 从前80字节取出必要信息*/
    flw_fd=open(P_ShareMem->ExAll.path,O_RDONLY);
    /*宽度/4 长度x色线数  BCD码*/
    read(flw_fd,buf,6);
    width = buf[1]*256+buf[0];
    width /= 8;
    height = buf[3]*256+buf[2];
    clrcount = buf[5]*256+buf[4];
    
    /*文件指针从上一次控制文件所取的位置开始*/
    if(P_ShareMem->Stay.IntWine_File_Pos!=0)
        flw_offset+=P_ShareMem->Stay.IntWine_File_Pos*P_ShareMem->ExAll.Color_Num*width;   
    lseek(flw_fd,flw_offset,SEEK_SET);
    /*发送6264结构体单元置0*/
    memset(&FlwData,0,sizeof(struct write6264));
    /*共享存储区未满则发送数据到6264 调用驱动*/
  
    while((FlwData.leng=read(flw_fd,FlwData.data,width))!=0)
    {
        FlwData.buf=P_ShareMem->Stay.Buffer_Write;
        write(thj_fd,(unsigned char*)&FlwData,sizeof(struct write6264));
        FlwData.line++;      
        /*发送完clrcount行,完成实际的一行即一个缓冲区的预取*/        
		if(FlwData.line==P_ShareMem->ExAll.Color_Num)
        {
            
			memset(&FlwData,0,sizeof(struct write6264));
            
            prt_ok = FALSE;
            /*移进下一缓冲区*/
            P_ShareMem->Stay.Buffer_Write=(P_ShareMem->Stay.Buffer_Write+1)%5;
            P_ShareMem->Stay.IntWine_File_Pos=(P_ShareMem->Stay.IntWine_File_Pos+1)%height;
            if(P_ShareMem->Stay.Buffer_Write==P_ShareMem->Stay.Buffer_Read)
            {
                P_ShareMem->Stay.Buffer_Full=TRUE;
                
                NeedFetch = FALSE;
                return;
            }
        }        
    }
}

/*
	文件锁函数,共享存储区同步
*/
int file_lock(int fd)
{ 
    struct flock s_flock;
    s_flock.l_type = F_WRLCK;        /*写锁*/
    s_flock.l_whence = SEEK_SET;     
    s_flock.l_start = 0;             
    s_flock.l_len = 0;              
    s_flock.l_pid = getpid();       /*进程id*/
 
    /*F_SETLKW对加锁操作进行阻塞*/
    /*F_SETLK不对加锁操作进行阻塞，立即返回*/
    return fcntl(fd, F_SETLKW,&s_flock); 
} 

int file_unlock(int fd)
{ 
    struct flock s_flock;
    s_flock.l_type = F_UNLCK;        /*施放锁*/
    s_flock.l_whence = SEEK_SET; 
    s_flock.l_start = 0;            
    s_flock.l_len = 0;              
    s_flock.l_pid = getpid();       
 
    return fcntl(fd, F_SETLKW,&s_flock); 
} 

int main(int argc, char *argv[])
{
    struct itimerval check,old;
    unsigned char path[256];
    int flw_fd;
    
	printf("Stay_2 is running\n");
    /*挂载共享存储区*/
    if(f_attashm()== -1)
        return;
    /*设置定时器,初始化设置为500毫秒*/
    
    check.it_value.tv_sec = 2;
    f_set_interval(&check);
    signal(SIGALRM,f_check_fetch);
    /*打开文件锁*/

    lock_fd=open("lock",O_RDWR);
    /*打开设备驱动*/
    thj_fd=open("/dev/thj",O_RDWR);
    while(1)
    {
        if(NeedFetch == TRUE)
        {
		    file_lock(lock_fd);
            f_preload();
            file_unlock(lock_fd);
        }
        f_set_interval(&check);
    }
    return 0;
}
