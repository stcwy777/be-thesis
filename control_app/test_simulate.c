/*****
*  Simulate user input to test the driver and stay resident programs
*
*  Author: Yun Wang
*
*  Simulate user input from QT to start a weaving test observe output from
*  stay resident programs to check if programs work well. We also used a
*  MAX 7219 to test the weaving data.
*  
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*
******/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "thj.h"
#include "iniparser.h"
#include "iniparser.c"
#define BUFSIZE 400
#define LDebug

static int P_ShmId;          
ShareMem *P_ShareMem; 


/*测试程序功能*/
/*1.挂载共享存储区,需要QT初始化的变量模拟输入*/
/*2.给出编织命令, 模拟工作逻辑*/
static int f_init_test()
{
    dictionary * ini;
    char * shmbuf;
    char buf[256];
    char *buf_test;
    int k;

    /*挂载共享存储区*/
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

    /*加载 ShareMem*/
    if((shmbuf=shmat(P_ShmId,0,0)) <(char *)0)
    {
        perror("shmat, in shmwrite ");
        return -1;
    }
	printf("test_program has matched share memory segment :%d\n",P_Shmld);	
    P_ShareMem = (ShareMem*)shmbuf;
    
    /*写入文件*/
    sprintf (P_ShareMem->ExAll.path, "targ.thj");
    sprintf(buf, "%s", P_ShareMem->ExAll.path);

    if((k=iniparser_set(ini, "IntWine:Filename",buf)) !=0)
    {
       printf("Set IntWine:Filename Error in f_Run_Ready!!!\n");
    }

     buf_test = iniparser_getstring(ini, "IntWine:Filename", NULL);
#ifdef LDebug
     printf("IntWine:Filename readed form ini file is:%s\n",buf_test);
#endif
    iniparser_dump_ini(ini, stderr);
    fclose(stderr);
    iniparser_freedict(ini);   
    return 1;
}
/*
    模拟QT输入
	 P_ShareMem->ExAll.Wine_Count
	 P_ShareMem->Qs.Q_Command
	 P_ShareMem->ExAll.Wine_End
	 P_ShareMem->ExAll.Color_Num 
	 P_ShareMem->ExAll.path
    写入共享存储区
	 IntWine:Filename
*/
/*给出命令*/
void f_command(int count,int num)
{
    printf("Send QT_Command:%d Wine_Count:%d Color_Num:%d\n",IntWine_Start,count,num);
	P_ShareMem->ExAll.Wine_Count =count;
    P_ShareMem->ExAll.Color_Num = num;
    P_ShareMem->Qs.Q_Command =  IntWine_Start;
}

int main(int argc, char *argv[])
{
    int count,num;
	if (f_init_test())
    {
        f_command(13,1);
    }
    else
    {
        printf("erro in execute test\n");
    }
    return 0;
}


