/*****
*  Stay resident program three: usb control component
*
*  Author: Yun Wang
*
*  I implemented three stay resident components in user-level to actually controls
*  the weaving machine through our developed device driver. This is the usb control 
*  component and it is responsible for:
*  1) Download weaving data from a USB once user plugin the drive and hit the button
*  2) Notify user errors by control LED lights. 
*  
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*
******/

/* 
*头文件 
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

/* 
*宏定义 
*/
/* 编织机状态 */
#define STAT_START	1
#define STAT_DOWNLOAD	2
#define STAT_WEAVE	3
#define STAT_CARRY	4
#define STAT_QUIT	5
/* 结果字 */
#define ACTIVE		1
#define PASSIVE		0
#define SUCCESS		1
#define FAILED		0
#define PENDING		2
/* U盘读取参数 */
#define FILE_NAME "/home/fa/log.txt"
#define MAX_DISKNAME_LEN 64
#define MAX_CMDSTR_LEN 200
#define COPY_FROM	"/mnt/file"
#define COPY_TO		"/home/fa/file"
#define CHK_LOG		"/home/fa/chklog"
/* 设备I/O操作指令 */
#define USB_LIGHT	1
#define ARM_RD_ACK	2
#define ARM_WR_ACK	3
#define THJ_STOP		4
#define USB_RD		5
#define THJ_START	6
#define CMD_WRITE	7
#define CMD_READ	8
#define GET_CMD		9
#define CLR_CMD		10
/* 从编织机接收到的指令 */
#define CMD_RESET	0x01AA0000
#define CMD_HEAD	0x02AA0000
#define CMD_WEAVE	0x03AA0000
#define CMD_BACK	0x0D0A0000
/* ARM应答字 */
#define ASW_RDY		0xAA
#define ASW_ERR		0xE1H
#define ASW_OTH		0xE0H

/* 
*全局变量 
*/
static int chk_allow=1;
static int state; 
static int thj_fd;

/* 函数申明 */
int  check_download(struct itimerval * dither);
void init_itimerval(struct itimerval * timer,int sec,int usec,int isec,int iusec);
void signalroutine(int sig);
void check_cmd();
int  check_sys(int retval,char cmd[]);
int copy_from_usb(struct itimerval * usblight,struct itimerval * old);
int  carry_data(int *pfd);
int  check_copy();

int main()
{
	struct itimerval dither,usblight,old;
	int flw_fd=-1;
	int rst;

	/* 注册信号到处理函数 */
	signal(SIGALRM,signalroutine);			// SIGALRM 内核时间信号
	signal(SIGVTALRM,signalroutine);      // SIGVTALRM 系统时间信号

    /* 初始化定时器用结构体 */
	init_itimerval(&dither,0,20000,0,0);
	init_itimerval(&usblight,0,5000000,1,5000000);
    /* 系统初始化状态 */
	state=STAT_START;
    /* 打开驱动设备文件 */
	thj_fd=open("/dev/thj_dev",0);
    if(!thj_fd )
    {
	    printf("can't open driver device file!\n");
    }
    /* 系统状态机:循环直到状态为退出 */
	while(state!=STAT_QUIT)
	{
		switch(state)
		{
             /* 开机状态 */
			case STAT_START:	                                           
				/* 判断是否需要读取U盘数据 */
				if(check_download(&dither)==ACTIVE)	
					state=STAT_DOWNLOAD;
				/* 否则尝试读取外部指令 */				
				else
					check_cmd();
				break;
             /* 下载状态 */
			case STAT_DOWNLOAD:			
				if(copy_from_usb(&usblight,&old)==FAILED)
				{
					//printf("copy failed system return start state\n");
					setitimer(ITIMER_REAL,&old,NULL);	
					ioctl(thj_fd,USB_LIGHT,0);
					state=STAT_START;
				}
				else
				{
					//printf("copy finished!!\n");
					setitimer(ITIMER_REAL,&old,NULL);
					ioctl(thj_fd,USB_LIGHT,1);
					state=STAT_START;
				}
				break;
             /* 片头传送状态 */
			case STAT_CARRY:
				//printf("enter carry state\n");				
				if(flw_fd == -1)
				{
					flw_fd= open(COPY_TO,O_RDONLY);
					if(flw_fd==-1)
					{
						printf("can't open flower file\n");
						return FAILED;
					}
				}
                 /* 发送一个字节数据 */
				rst=carry_data(&flw_fd);	
				if(rst==SUCCESS)
				{
					state=STAT_START;
				}
				else if(rst==FAILED) 
				{
					printf("error when carry flower head\n");
					exit(1);
				}
				break;
             /* 编织状态(过程与片头传送类似) */
			case STAT_WEAVE:
				//printf("enter weave state\n");
				if(flw_fd == -1)
				{
					flw_fd= open(COPY_TO,O_RDONLY);
					if(flw_fd==-1)
					{
						printf("can't open flower file\n");
						return FAILED;
					}
				}
				rst=carry_data(&flw_fd);			
				if(rst==SUCCESS)
				{
					state=STAT_START;
				}
				else if(rst==FAILED) 
				{
					printf("error when weave\n");
					exit(1);
				}
				break;
		}

	}
    /* 还原信号处理 */
	signal(SIGALRM,SIG_DFL);
	signal(SIGVTALRM,SIG_DFL);
	return 0;
}

/* 下载状态检测函数(加入按键去抖) */
int check_download(struct itimerval * dither)
{
	static int times;
    /* 标志位允许检测 */	
	if(1==chk_allow)
	{
        /* 检测到按钮按下 */
		if(ioctl(thj_fd,USB_RD,0)==0)
		{
             /* 停止检测直到20ms去抖时间结束 */
			chk_allow=0;		
			times++;
             /* 如果是第一次检测到按钮按下则启动20ms的定时器 */
			if(1==times)
				setitimer(ITIMER_VIRTUAL,dither,NULL);
             /* 如果是第二次检测到按钮按下则确定为按下 */
			if(2==times)
			{
				times=0;
				/* 保持直到按键释放 */
				while(ioctl(thj_fd,USB_RD,0)==0);
				chk_allow=1;
				return ACTIVE;
			}
		}
        /* 其他情况则重置计数 */
		else
			times=0;
	}
	return PASSIVE;
}

/* 设置定时器时间 */
void init_itimerval(struct itimerval * timer,int sec,int usec,int isec,int iusec)
{
    /* 第一次间隔时间(秒,微妙) */
	timer->it_value.tv_sec = sec;
	timer->it_value.tv_usec = usec;

	/* 循环间隔时间 */
	timer->it_interval.tv_sec = isec;
	timer->it_interval.tv_usec = iusec;
}

/* 信号处理函数 */
void signalroutine(int sig)
{
	static int onoff=1;
	switch(sig)
	{
        /* 处理去抖 */
		case SIGVTALRM:				//real timer 
			chk_allow=1;
			break;
        /* 处理LED亮灭 */
		case SIGALRM:				//virtual timer make light on/off
			if(0==onoff)
			{
				onoff=1;
				ioctl(thj_fd,USB_LIGHT,0);
			}
			else
			{
				onoff=0;
				ioctl(thj_fd,USB_LIGHT,1);
			}
			break;
		default:
	}
}

/* 从缓冲区中读出指令 */
void check_cmd()
{
	int command;
    /* 调用驱动程序读缓冲区指令 */
	command=ioctl(thj_fd,GET_CMD,0);

    /* 传送片头指令 */
	if(command==CMD_HEAD)
	{
		printf("\nCommand:%x,%x",command>>0x10,command &0xffff);
		printf("\nenter head carry state\n");
        /* 向编织机写入ARM应答字:就绪 */
		ioctl(thj_fd,CMD_WRITE,ASW_RDY);
        /*改变系统状态为片头传送 */
		state=STAT_CARRY;
	}
    /* 花型编织指令 */
	else if(command==CMD_WEAVE)
	{
		printf("\nCommand:%x,%x",command>>0x10,command &0xffff);
		printf("\nenter head weave state\n");
	    /* 向编织机写入ARM应答字:就绪 */
		ioctl(thj_fd,CMD_WRITE,ASW_RDY);
        /* 改变系统状态为花型数据传送 */
		state=STAT_WEAVE;
	}
}

/* 系统调用错误检测:三种错误如下 */
int check_sys(int retval,char cmd[])
{
    /* 找不到/bin/sh */
	if(127==retval)
	{
		printf("can't use /bin/sh\n");
		return FAILED;
	}
    /* 无法使用系统调用 */
	else if(-1==retval)
	{
		printf("error using system call\n");
		return FAILED;
	}
    /* 找不到指定的命令 */
	else if(0!=retval)
	{
		printf("error using current command:%s\n",cmd);
		return FAILED;
	}
	else
		return SUCCESS;
}

/* U盘拷贝数据校验 */
int check_copy()
{
	int src_fd,tar_fd;
	char src_buf,tar_buf;
	int src_size,tar_size;

    /* 打开U盘源文件 */
	src_fd=open(COPY_FROM,O_RDONLY);
	if(-1==src_fd)
	{
		printf("open source file error\n");
		return FAILED;
	}
    /* 打开复制目标文件 */	
	tar_fd=open(COPY_TO,O_RDONLY);
	if(-1==tar_fd)
	{
		printf("open target file error\n");
		return FAILED;
	}	
    /* 开始逐字节比较直到源文件读完 */
	do
	{
		src_size=read(src_fd,&src_buf,1);
		if(-1==src_size)
		{
			printf("read source file error\n");
			return FAILED;
		}
		tar_size=read(tar_fd,&tar_buf,1);
		if(-1==tar_size)
		{
			printf("read target file error\n");
			return FAILED;
		}
        /* 情况一:源文件大于目标文件 */
		if(src_size!=0&&tar_size==0)
		{
			printf("file size error:source larger\n");
			return FAILED;
		}
        /* 情况二:存在不一致字节 */
		else if(src_size!=0&&tar_size!=0)
		{
			if(src_buf!=tar_buf)
			{
				printf("file byte error\n");
				return FAILED;
			}
		}
	}while(src_size!=0);	
	
	close(src_fd);
	close(tar_fd);
    /* 情况三:源文件小于目标文件 */
	if(tar_size!=0)
	{
		printf("file size error:target larger\n");
		return FAILED;
	}	
	else
		return SUCCESS;
}

/* 从U盘拷贝数据 */
int copy_from_usb(struct itimerval * usblight,struct itimerval * old)
{
	int	retval;
	int	chk_flag;
	FILE 	*file;
	char	cmdstr[MAX_CMDSTR_LEN];
	static char target[MAX_DISKNAME_LEN];
	struct stat f_stat;

    /* 设置定时器控制LED灯状态 */
	setitimer(ITIMER_REAL,usblight,old);		
    /* 从日志文件读出U盘的设备文件名 */
	file=fopen(FILE_NAME,"rb");
	fread(&target,MAX_DISKNAME_LEN,1,file);
	sprintf(cmdstr,"mount %s/part1 /mnt",target);
	
    /* 使用系统调用mount设备文件到系统目录 */
	retval=system(cmdstr);
    /* 检测系统调用是否成功,下同 */
	if(check_sys(retval,"mount")==FAILED)
	{
		return FAILED;
	}

    /* 使用系统调用拷贝文件 */
	sprintf(cmdstr,"cp %s %s",COPY_FROM,COPY_TO);
	retval=system(cmdstr);
	if(check_sys(retval,"cp")==FAILED)
	{
		return FAILED;
	}
    /* 数据校验 */
	chk_flag=check_copy();
	
    /* 使用系统调用umount目录 */
	sprintf(cmdstr,"umount /mnt");
	retval=system(cmdstr);
	if(check_sys(retval,"umount")==FAILED)
	{
		return FAILED;
	}

	return chk_flag;
}

/* 传递数据(片头和花型数据均调用) */
int  carry_data(int * pfd)
{
	int command;
	int off_set;
	char buff;

    /* 从驱动缓冲区中读出一指令 */
	command = ioctl(thj_fd,GET_CMD,0);				
	
	//if(command != 0)
		//printf("command: %x %x\n",command>>0x10,command&0xFFFF);
    /* 判断指令格式,高8位是0A则为数据请求指令 */		
	if((command & 0xFF000000) == 0x0A000000)
	{
        /* 获取指令后24位指定的文件偏移量 */
		off_set=command & 0x00FFFFFF;
		lseek(*pfd,off_set,SEEK_SET);
        /* 从文件读出一个字节 */
		if(read(*pfd,&buff,1))
		{
			printf("the byte to trans:0x%x(offset:%d)\n",buff&0xFFFF,off_set);
            /* 调用驱动将花型数据送到编织机 */
			ioctl(thj_fd,CMD_WRITE,buff);
		}
		else
		{
			printf("no more data to carry\n");
			/* 读取文件错误,将全1送至编织机 */
			ioctl(thj_fd,CMD_WRITE,0xFF);
		}
	}
    /* 状态返回指令 */		
	else if(command ==CMD_BACK)	
	{
		close(*pfd);
		*pfd = -1;
		printf("return start state\n");
        /* 成功返回,退出编织态 */				
		return SUCCESS;
	}
    /* 错误格式的指令 */		
	else if(command !=0)
	{
		//close(*pfd);	
		printf("bad command\n");
		printf("command: %x %x\n",command>>0x10,command&0xFFFF);
		buff =(command >> 0x18);
		ioctl(thj_fd,CMD_WRITE,buff);
	}
    /* 编织进行中,保持编织态 */		
	return PENDING;
}
