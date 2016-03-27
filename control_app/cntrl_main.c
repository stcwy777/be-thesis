/*****
*  Stay resident program one: main control component
*
*  Author: Yun Wang
*
*  I implemented three stay resident components in user-level to actually controls
*  the weaving machine through our developed device driver. This is main control 
*  component and it is responsible for:
*  1)Create a shared memory space for the two components. I used file lock for 
*     synchronization
*  2)Read and manage weaving machine status: start, stop, weaving, maintenance
*     Read cached data from shared memoery and send it to 6264(via the device 
*     drive)for weaving.
*  3)Handle user inputs from button or keyboard.
*  4)Using ini. file to support weaving after power outage
*  Used N. Devillard's codes for parsing ini file. 
*  
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*
******/

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include "thj.h"
#include "iniparser.h"
#include "iniparser.c"
#define BUFSIZE 400
#define LDebug    

/*状态 IntWine
0:就绪
1:等待
2:织布
3:维护
*/
#define State_Ready 0
#define State_Stay 1
#define State_IntWine 2
#define State_RePair 3
static int P_State;
static int P_Line_Num;      /*行号*/
static int P_ShmId;         /*共享内存ID*/
ShareMem *P_ShareMem;
static int lock_fd;         /*文件锁ID*/
static int thj_fd;

/*
    1.初始化
    1.1建立共享存储区,将分配的ID写入配置文件
    1.2读取执行机构的状态,进行检测
    1.3读取配置文件判断是否断电续织,如果是则提取上次断电时位置
*/
static int f_MkShm()
{
    int shmid,k;
    dictionary * ini ;
    char id_buf[20],*shmbuf;

    /*1.1建立共享存储区,将分配的ID写入配置文件*/
    /*shmget():进程标识,大小,权限*/
    if((shmid=shmget(IPC_PRIVATE,BUFSIZE,0666))<0 )
    {
        perror("shmget" );
        return -1;
    }
    P_ShmId=shmid;
#ifdef LDebug
    printf("Segment has been created ID :  %d\n",shmid );
#endif

    /*为驻留一挂载存储区*/
    /*shmat():共享内存ID,起始地址,操作模式*/
    if((shmbuf=shmat(shmid,0,0))<(char *)0 )
    {
        perror("shmat, in shmwrite " );
        return -1;
    }
    P_ShareMem=(ShareMem *)shmbuf;

    /*按照共享内存结构初始化*/
    P_ShareMem->Stay.Start_Pos=0;
    P_ShareMem->Stay.Buffer_Read=0;
    P_ShareMem->Stay.Buffer_Write=0;
    P_ShareMem->Stay.Buffer_Full=FALSE;
    P_ShareMem->Stay.IntWine_File_Pos=0;


    P_ShareMem->Qs.Q_Command=Command_None;;
    P_ShareMem->Qs.re_value=-1;

    P_ShareMem->ExAll.Wine_Start=0;
    P_ShareMem->ExAll.Wine_Now=0;
    P_ShareMem->ExAll.Wine_End=TRUE;
    P_ShareMem->ExAll.Color_Num=0;


    /*把共享区的id写到ini文件中*/
    ini = iniparser_load("thj.ini" );
    stderr=fopen("thj.ini", "w" );
    if(ini==NULL )
    {
        fprintf(stderr, "cannot parse file: thj.ini in f_MkShm, Please Check File!!!!\n" );
        return -1;
    }

    sprintf(id_buf, "%d", shmid );   /*格式转换字符串写*/


    if((k=iniparser_set(ini, "Temp:ShareMem",id_buf))!=0 )
    {
        printf("Set ShareMemory ID Error!!!\n" );
        return -1;
    }
    iniparser_dump_ini(ini, stderr );
#ifdef LDebug
    k=iniparser_getint(ini, "Temp:ShareMem", -1 );
    printf("Share Memory ID readed form ini file is :%d\n",k );
#endif
    fclose(stderr );
    iniparser_freedict(ini );
    return 1;
}

/*1.2读取执行机构的状态,进行检测*/
static int f_Machine_State()
{
    /*引脚定义不完全还不能完成*/
    /*读取设备状态*/
    /*写入共享存储区*/
    return 1;
}

/*1.3读取配置文件判断是否断电续织,如果是则提取上次断电时位置*/
static int f_Read_Last()
{
    dictionary * ini ;
    int k;
    char *buf;
    ini = iniparser_load("thj.ini" );
    stderr=fopen("thj.ini", "w" );
    if(ini==NULL )
    {
        fprintf(stderr, "cannot parse file: thj.ini in f_Read_Last, Please Check File!!!!\n" );
        return -1;
    }

    if((k=iniparser_getint(ini, "IntWine:Done", -1))==-1 )
    {
        printf("Ini file is exist, but i can't find IntWine:Done in f_Read_Last, please check it\n" );
        return -1;
    }
    else if(k==0)      // Done = 0 续织
    {
        buf=iniparser_getstring(ini, "IntWine:Filename", NULL );
        if(buf ==NULL )
        {
            printf("Ini file is exist, but i can't fine IntWine:Filename in f_Read_Last, please check it\n" );
            return -1;
        }
        else
        {
            strcpy((P_ShareMem->ExAll.path ),buf );
        }
        if((P_ShareMem->ExAll.Wine_Start=iniparser_getint(ini, "IntWine:Pos", -1))==-1 )
        {
            printf("Ini file is exist, but i can't fine IntWine:Pos in f_Read_Last, please check it\n" );
            return -1;
        }
        #ifdef LDebug
            printf("Continuation  Misson(position: %d file:%s)\n",P_ShareMem->ExAll.Wine_Start,buf);
        #endif
    }
    else
    {
	    P_ShareMem->ExAll.Wine_Start = 0;
	    printf("New misson\n");
    }
    /*驻留二读控制序列的开始位置,如果续织,从继续位置开始读取*/
	P_ShareMem->Stay.IntWine_File_Pos = P_ShareMem->ExAll.Wine_Start;
    iniparser_dump_ini(ini, stderr );
    iniparser_freedict(ini );
    fclose(stderr );
    return 0;
}

/*初始化函数*/
static int f_init()
{
    #ifdef LDebug
            printf("\nMachine Start Initializing Enviroment............\n");
    #endif
    if(f_MkShm()!=1 )
    {
        printf("Make sharememory Error!!!\n" );
        return -1;
    }
    f_Machine_State();
    f_Read_Last();
    P_Line_Num=0;
    P_State=State_Ready;
}

/*
    2.  就绪态
    2.1读取执行机构状态,写入共享内存供QT调用
    2.2从共享内存中读取QT命令,根据命令调整状态机
	2.3由就绪态到准备态,将文件名写入配置文件
*/
static void f_Run_Ready()
{
    int command;
    #ifdef LDebug
    static int first =0;
    if(first ==0)
    {
        printf("Machine Status: Ready----waiting command from QT...........\n");
        first = 1;
    }
    #endif
    /*2.1 读取执行机构状态*/
    f_Machine_State();

    /*2.2 从共享内存中读取QT命令*/
    command=P_ShareMem->Qs.Q_Command;
    P_ShareMem->Qs.Q_Command=Command_None;
    switch(command )
    {
         /*开始编织,进入等待状态,等待外部请求*/
		case IntWine_Start:
            P_State=State_Stay;
            break;
		/*保持就绪状态*/
        case IntWine_Stop:
            P_State=State_Ready;
            break;
		/*自我检测,设备定义不完全*/
        case Self_Test:            
            P_State=State_RePair;
            break;
	    /*清洗磁盘,设备定义不完全*/
        case Clean_Magnetism:
            P_State=State_RePair;
            break;
        default:
            break;
    }
}

/*
    3.  待机态
    3.1读取执行机构状态,写入共享内存供QT调用
    3.2从共享内存中读取QT命令,是否停止编织
    3.3读取外部请求,准备发送数据
*/
static void f_Run_Stay()
{
    int command;
#ifdef LDebug
    static int first =0;
    if(first ==0)
    {
        printf("\nMachine Status: Stay----waiting request from Excute Device...........\n");
        first = 1;
    }
#endif
    /*3.1读取执行机构状态*/
    f_Machine_State();

    /*3.2从共享内存中读取QT命令,是否停止编织*/
    command=P_ShareMem->Qs.Q_Command;
    P_ShareMem->Qs.Q_Command=Command_None;
    switch(command )
    {
        case IntWine_Stop:
            P_State=State_Ready;
            break;
        default:
            break;
    }
    /*获取外部执行机构请求*/
    /*外部命令暂时没有定义*/
    /*如果接收到发送数据的请求,则开始发送数据*/
    /*发送数据*/
#ifdef LDebug
    printf("Surppose we recieve  IntWine requestfrom Excute Device\n");
    P_State=State_IntWine;
    P_ShareMem->ExAll.Wine_End =  FALSE;
#endif
}

/*
    4.  编织态
    4.1读取执行机构状态,写入共享内存供QT调用
    4.2同步读缓冲区,发送一行控制序列
    4.3如果发送完毕,修改完成标识,修改配置文件
    4.4没有发送完,等待执行机构命令(停止 继续)
*/

static void f_Run_IntWine()
{
    dictionary * ini;
    int k;
#ifdef LDebug
    static int first =0;
    if(first ==0)
    {
        printf("Machine Status: IntWine----sending data by line until meeting the counts...........\n");
        first = 1;
    }
#endif
    /*4.1读取执行机构状态,写入共享内存供QT调用*/
    f_Machine_State();

    /*4.2同步读缓冲区,发送一行控制序列*/
    file_lock(lock_fd);
    f_Send_One_Line();
    file_unlock(lock_fd);
    /*4.3如果发送完毕,修改完成标识,修改配置文件 */
    if(P_ShareMem->ExAll.Wine_Count==P_ShareMem->ExAll.Wine_Now)
    {
        P_ShareMem->ExAll.Wine_End=TRUE;
        P_State=State_Ready;
	
        //write Done to inifile
        printf("\nMisson complete\n");
	    ini = iniparser_load("thj.ini" );
        stderr=fopen("thj.ini", "w" );
        if(ini==NULL )
        {
            fprintf(stderr, "cannot parse file: thj.ini in f_Read_Last, Please Check File!!!!\n" );
        }

        if((k=iniparser_set(ini, "IntWine:Done","1"))!=0 )
        {
            printf("Set IntWine:Pos Error in f_Run_IntWine!!!\n" );
        }
        /*流写回文件*/
        iniparser_dump_ini(ini, stderr );
        fclose(stderr );
        iniparser_freedict(ini );
    }
}

/*
    5.  发送一行数据
    5.1判断缓冲区是否满:
	   (1)read/write同位且标志位为满
	   (2)read/write不同位
    5.2ioctl发送数据,判断一行的线色数是否到位
*/
static int f_Send_One_Line()
{
    dictionary * ini ;
    int k,lock_result;
    /*param低8位行号,剩余高位区号*/
    int param;
    char buf[10];
 
    /*6264有预取:可读可写同一位置且存满,或者读写不同位置 都表示有内容在6264*/
    if(((P_ShareMem->Stay.Buffer_Read==P_ShareMem->Stay.Buffer_Write)&&(P_ShareMem->Stay.Buffer_Full==TRUE))||(P_ShareMem->Stay.Buffer_Read!=P_ShareMem->Stay.Buffer_Write))
    {
	    /*发送一行数据需要指明缓冲区号,缓冲区中行号*/
        if(P_ShareMem->Stay.Buffer_Full == TRUE)
            P_ShareMem->Stay.Buffer_Full = FALSE;
        param  =(P_ShareMem->Stay.Buffer_Read << 8)+ P_Line_Num;
        P_Line_Num++;
        ioctl(thj_fd,CMD_SEND_LINE,param);

        /*发完实际的一行*/
        if(P_Line_Num==P_ShareMem->ExAll.Color_Num)    
        {
            P_ShareMem->Stay.Buffer_Read=(P_ShareMem->Stay.Buffer_Read+1)%5;
            P_ShareMem->ExAll.Wine_Now++;
            P_Line_Num=0;
            ini = iniparser_load("thj.ini" );
            stderr=fopen("thj.ini", "w" );
            if(ini==NULL )
            {
                fprintf(stderr, "cannot parse file: thj.ini in f_Read_Last, Please Check File!!!!\n" );
            }

            sprintf(buf, "%d", P_ShareMem->ExAll.Wine_Now );

            if((k=iniparser_set(ini, "IntWine:Pos",buf))!=0 )
            {
                printf("Set IntWine:Pos Error in f_Run_IntWine!!!\n" );
            }
            /*流写回文件*/
            iniparser_dump_ini(ini, stderr );
            fclose(stderr );
            iniparser_freedict(ini );
        }
    }
}
/*等到执行机构发送命令*/
static int f_Wait_Execute()
{
    //暂时无法完成
    return 1;
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

int main()
{
    thj_fd=open("/dev/thj",0);
    lock_fd=open("lock",O_RDWR);
    f_init();                      
    system("./cntrl_support &");        /*启动驻留程序二*/
    while(1)
    {
        switch(P_State)
        {
            case State_Ready:        /*就绪态*/
                f_Run_Ready();
                break;
            case State_Stay:         /*等待态*/
                f_Run_Stay();
                break;
            case State_IntWine:      /*编织态*/
                f_Run_IntWine();
                break;
            case State_RePair:       /*维护态*/
                //工作未定义
                break;
            default:
                break;
        }
    }
}


