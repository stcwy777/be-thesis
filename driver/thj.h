/*****
*  Header file of THJ weaving machine controller device driver 
*
*  The weaving machine controller is equiped with S3C2410 miscroprocessor.
*  We installed Linux 2.4 as the OS. My work in this driver development include
*   (1) Define ARM-GPIO registers’ address in the driver. Since GPIO pins are 
*       used to transmit signals between devices, the driver can send and receive 
*       signals by accessing such registers.
*   (2) Register handlers for interrupts during the weaving. In these handlers, the 
*       driver will process the error status, sync signals for next move
*   (3) Set system call functions (read/write/ioctl…) for user-level programs to 
*       call the kernel driver
*       
*  Author: Yun Wang
*
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*  
******/

#include <stdlib.h>
#include <stdio.h>

typedef struct Between_Stay
{
    int Start_Pos;              /*编织开始位置*/
    int Buffer_Read;            /*可读缓冲区头*/
    int Buffer_Write;           /*可写缓冲区头*/
    int Buffer_Full;     
    int IntWine_File_Pos;       /*花型文件预取的位置*/
} Between_Stay;

typedef struct QT_Stay
{
    int Q_Command;              /*从QT输入的控制命令，具体见表一*/
    unsigned char argv[2];
    int argc;
    int re_value;               /*返回值*/
    int S_State;                /*驻留程序读出的设备状态，具体见表二*/
} QT_Stay;

typedef struct Ex_All
{
    unsigned char path[256];    /*花型文件路径*/
    int Wine_Start;             /*编织的起始位置，在续织时有用*/
    int Wine_Now;               /*当前编织位置*/
        int Wine_Count;         /*共编织多少行*/
        int Wine_End;           /*编织完成*/
        int Color_Num;
} Ex_All;

typedef struct signal
{
    struct Between_Stay Stay;
    struct QT_Stay   Qs;
    struct Ex_All   ExAll;
} ShareMem;


/*6264 写结构体*/
typedef struct write6264
{
    int buf;                     /*缓冲区编号*/
    int line;                    /*缓冲区行*/
    int leng;                    /*实际长度*/
    unsigned char data[150];   
}Write6264;

/*QT 接收命令*/
#define Command_None 100
#define IntWine_Start 101
#define IntWine_Stop  102
#define Self_Test     103
#define Clean_Magnetism 104

/*QT 执行命令*/
#define Exec_Ok 901
#define Exec_Falt 902

/*驻留程序一读入状态*/
#define Press_Low 301
#define Press_Ok 302
#define IntWine_End 303
#define FALSE 0
#define TRUE 1