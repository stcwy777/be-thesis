/*****
*  Implementation file of THJ weaving machine controller driver 
*
*  Author: Yun Wang
*
*  Important GPIO configurations:
*  Parrallel Port-8255: provide access to LEDs and a buzzer
*  HM 6264: was partitioned into 5 pieces, each of which contained weaving data 
*           for one movement of execute device. Set system call functions (read
*           /write/ioctl…) for user-level programs to call
*  Refer to Samsung S3C2410 for more details of GPIO pins and singals.
*
*  Codes were written in 2008 for my undergraduate thesis. Original comments 
*  were in Chinese. Archive the codes for my own records.
*
******/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <stdio.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/mach/map.h>
#include <asm-arm/arch-s3c2410/S3C2410.h>
#include <asm-arm/arch-s3c2410/smdk.h>
#include <linux/time.h>

#define DEVICE_NAME "thj"
#define DEVICE_MAJOR 235

/*PWM Timer 寄存器*/
#define rTCFG0    0x51000000
#define rTCFG1    0x51000004
#define rTCON      0x51000008
#define rTCNTB0    0x5100000c
#define rTCMPB0    0x51000010
#define rTCNTB1    0x51000018
#define rTCMPB1    0x5100001c    
#define rGPHDAT    0x56000074
#define rINTMSK    0x4a000008
#define rINTPND    0x4a000010
#ifndef NULL
#define NULL  0
#endif
#define TIM "3"

/*8255 GPIO*/
#define ADD_8255    0x11000000
#define ADD_IO      0x21000000
#define ADD_MEM    0x20000000
#define OFF_DataLen 0x1D50

/* 8055 
  0: A:I B:I
  1: A:I B:O
  2: A:O B:I
  3: A:O B:O
*/
#define PAIPBI 0
#define PAIPBO 1
#define PAOPBI 2
#define PAOPBO 3
#define SHAKE_TIMER_DELAY  (100*HZ)
static struct timer_list shake_timer;
u32 io_8255_base;
u32 io_base;
u32 io_mem;
unsigned char State_Relay;
static int temp;

/*往地址写数据*/
static void Mem_Write_Data(u32 addr,unsigned char data)
{
    outb(data,addr);
}

/*从地址读数据*/
static unsigned char Mem_Get_Data(u32 addr)
{
    unsigned char tmp;
    tmp=inb(addr);
    return tmp;
}

/*
*将一行数据的数据长度写入6264
*/
static void write_data_length(int leng)
{
    unsigned char tmp;
    tmp=leng;
    Mem_Write_Data(io_mem+OFF_DataLen,tmp);
    tmp=leng>>8;
    Mem_Write_Data(io_mem+OFF_DataLen+1,tmp);
}

/*
*从6264中读出一行数据的数据长度
*/
static int read_data_length()
{
    int leng;
    unsigned char tmp;

    leng=0;
    tmp=Mem_Get_Data(io_mem+OFF_DataLen+1);

    printk(DEVICE_NAME TIM "1 here readed tmp is :0X%4X %4X\n ",(int)((tmp)>>0x10),(int)((tmp) & 0xffff));
    
    leng=tmp;
    leng=leng<<8;
    printk(DEVICE_NAME TIM "1 data length is :%d\n",leng);
    tmp=Mem_Get_Data(io_mem+OFF_DataLen);

    printk(DEVICE_NAME TIM "1 here readed  tmp is :0X%4X %4X\n ",(int)((tmp)>>0x10),(int)((tmp) & 0xffff));

    leng+=tmp;

    printk(DEVICE_NAME TIM "1 data length is :%d\n",leng);
    return leng;
}

/**********************************************************************************************************/
/*设置定时器*/
static void timer_config(int prescaler)  
{
    
    __REG(rTCFG0) = prescaler;    //设置 prescaler
    __REG(rTCFG1) = 0;            
    __REG(rTCNTB0) = 0xFFFF;
    __REG(rTCMPB0) = 0;
    __REG(rTCNTB1) = 1;
    __REG(rTCMPB1) = 0;
}

/*启动定时器*/
static void start_timer(int num)
{
    if(0==num)
    {
        __REG(rTCON) = __REG(rTCON) | 1<<1;                 /*更新 timer0*/
        __REG(rTCON) = __REG(rTCON) | (1<<3);               /*设置 timer0 auto reload*/
        __REG(rTCON) = __REG(rTCON) &(~(1<<2));             /*设置 timer0 inverter off*/
        __REG(rTCON) = __REG(rTCON) &(~(1<<1));             /*设置 timer0 no operation*/
        __REG(rTCON) = __REG(rTCON) | (1);                  /*启动 timer0*/
    
        __REG(rINTMSK) = __REG(rINTMSK) & (~(1<<10));
    }
    else if(1==num)
    {
        __REG(rTCON) = __REG(rTCON) | 1<<9;                  
        __REG(rTCON) = __REG(rTCON) | (1<<11);            
        __REG(rTCON) = __REG(rTCON) &(~(1<<10));      
        __REG(rTCON) = __REG(rTCON) &(~(1<<9));   
        __REG(rTCON) = __REG(rTCON) | (1<<8); 
    
        __REG(rINTMSK) = __REG(rINTMSK) & (~(1<<11));
    }
}

/*停止定时器*/
static void close_timer(int num)
{
    if(0==num)
    {
        __REG(rTCON) &= 0xfffffff0;
    }
    else if(1==num)
    {
        __REG(rTCON) &= 0xfffff0ff;
    }
}

/*定时器0中断IRQ*/
static void TIMER0_irq(int irq, void *dev_id, struct pt_regs *reg)
{
    printk("Enter TIMER0 interrupt!\n");    
    __REG(rGPHDAT)=__REG(rGPHDAT) | (1<<7);
    close_timer(0);
}
/*定时器1中断IRQ*/
static void TIMER1_irq(int irq, void *dev_id, struct pt_regs *reg)
{
    printk("Enter TIMER1 interrupt!\n");
    __REG(rGPHDAT)=__REG(rGPHDAT) | (1<<6);
    close_timer(1);
}

/**********************************************************************************************************/
/*以下代码设置8255 端口ABC 读写*/
static void Set_8255_portc(int bit)
{
    if(bit>7)
        return;
    else
    {
        outb(0x01|(bit<<1),io_8255_base+0x03);
    }
}

static void Set_out_con(int bit)
{
    Set_8255_portc(bit);
}

static void Clr_8255_portc(int bit)
{
    if(bit>7)
       return;
    else
    {
        outb(0x00|(bit<<1),io_8255_base+0x03);
    }
}

static void Clr_out_con(int bit)
{
    Clr_8255_portc(bit);
}

static void write_8255_porta(unsigned char data)
{
    outb(data,io_8255_base);
}

static void write_8255_portb(unsigned char data)
{
    outb(data,io_8255_base+0x01);
    printk(DEVICE_NAME  "Port B Address is :0X%4X %4X\n    in write_8255\n",(int)((iobase+0x01)>>0x10),(int)((iobase+0x01) & 0xffff));
}

static void write_8255_portc(unsigned char data)
{
    outb(data,io_8255_base+0x02);
}

static unsigned char read_8255_porta()
{
    return inb(io_8255_base);
}

static unsigned char read_8255_portb()
{
    printk(DEVICE_NAME  "Port B Address is :0X%4X %4X\n    in read_8255\n",(int)((iobase+0x01)>>0x10),(int)((iobase+0x01) & 0xffff));
    return inb(io_8255_base+0x01);
}

/*操作蜂鸣器*/
static void Beep_On()
{
    Set_8255_portc(4);
}

static void Beep_Close()
{
    Clr_8255_portc(4);
}

/*操作x9318*/
static void Set_x9318_cs()
{
    Clr_8255_portc(7);
}

static void Clr_x9318_cs()
{
    Set_8255_portc(7);
}

/* 8255 设置
0:PAIPBI A:I B:I
1:PAIPBO A:I B:O
2:PAOPBI A:O B:I
3:PAOPBO A:O B:O
*/
static void Set_8255_portab(int optype)
{
    switch(optype)
    {
    case PAIPBI:
        printk(DEVICE_NAME  "Port A set to Input && Port B set to Input \n");
        outb(0x92,io_8255_base+0x03);
        break;
    case PAIPBO:
        printk(DEVICE_NAME  "Port A set to Input && Port B set to Output \n");
        outb(0x90,io_8255_base+0x03);
        break;
    case PAOPBI:
        printk(DEVICE_NAME  "Port A set to Output && Port B set to Input \n");
        outb(0x82,io_8255_base+0x03);
        break;
    case PAOPBO:
        printk(DEVICE_NAME  "Port A set to Output && Port B set to Output \n");
        outb(0x80,io_8255_base+0x03);
        break;
    default: 
        break;
    }
}

/* 启动 x9318 */
static void Set_x9318_up()
{
    Set_8255_portc(6);
    Clr_8255_portc(5);    
    Set_x9318_cs();
    udelay(1000000);
    Clr_x9318_cs();
    Set_8255_portc(5);
}

/* 关闭 x9318 */
static void Set_x9318_down()
{
    Clr_8255_portc(6);
    Clr_8255_portc(5); 
    Set_x9318_cs();
    udelay(1000000);
    Clr_x9318_cs();
    Set_8255_portc(5);
}

/**********************************************************************************************************/
static void shake_timer_handler(unsigned long data)
{
    printk(DEVICE_NAME " signal number is :%d\n",(HZ));
    del_timer(&shake_timer);
    shake_timer.expires = jiffies+SHAKE_TIMER_DELAY;
    add_timer(&shake_timer);
}

/**********************************************************************************************************/
/* 转换LCD 电源状态 */
static void LCD_light_onoff(int flag)
{
    static char steate;;
    if(flag)    /*LCD 由开到关*/
    {
        steate=__REG(0X4D000000);
        steate=steate|0x00000001;
        __REG(0X4D000000)=steate;
    }
    else    /*LCD 由关到开*/
    {
        steate=__REG(0X4D000000);
        steate=steate&0xFFFFFFE;
        __REG(0X4D000000)=steate;
    }
}

/**********************************************************************************************************/
/*nCLEAR:0x2100_0000 移位输出使能*/
static void signal_nclear()
{
     outb(0x00,io_base);
}
/*OE_TRIG:0x2100_0001 行输出使能*/
static void signal_oe_trig()
{
    outb(0x00,io_base+0x01);
}

static void out_nsreg1(unsigned char data)
{
    outb(data,io_base+0x05);
}

static void out_nsreg2(unsigned char data)
{
    outb(data,io_base+0x06);
}

static void out_outport1(unsigned char data)
{
   outb(data,io_base+0x04);
}

static unsigned char in_ninpot1()
{
   return inb(io_base+0x02);
}

static unsigned char in_ninpot2()
{
   return inb(io_base+0x03);
}

static void send_data(unsigned char OUT_H,unsigned char OUT_L)
{
    out_nsreg2(OUT_H);
    out_nsreg1(OUT_L);
    signal_nclear();
    while((in_ninpot1() & 0x01) == 0)   
    {
        udelay(10);
    }

}

/*
 * 向提花机发送一行数据
 */
static void send_line(int num,int count)
{
    int i;
    u32 add;
    add=io_mem+num*1500+150*count; 
    for(i=0;i<read_data_length();i+=2)
    {
        send_data(Mem_Get_Data(add+i),Mem_Get_Data(add+i+1));
        printk("%x %x ",Mem_Get_Data(add+i),Mem_Get_Data(add+i+1));
    }
    if(i<count) 
    {
        send_data(Mem_Get_Data(add+i),Mem_Get_Data(0x00));
		printk("%x ",Mem_Get_Data(add+i));
    }
	printk("\n");
    signal_oe_trig();

    while(in_ninpot1()  & 0x08)
    {
        udelay(10);
    }
}

static void Set_Relay(int num)
{
  if(num==6)
  {
    State_Relay=State_Relay|(0x01);
    out_outport1(State_Relay);
  }
  else
  {
    State_Relay=State_Relay|(0x01<<num);
    out_outport1(State_Relay);
  }
}

static void Clr_Relay(int num)
{
  if(num==6)
  {
    State_Relay=~( (~State_Relay) | (0x01));
    out_outport1(State_Relay);
  }
  else
  {
    State_Relay=~( (~State_Relay) | (0x01<<num));
    out_outport1(State_Relay);
  }
}

/*从提花机读取数据*/
static unsigned char  Get_Down()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>4)&0x01;
    return tmp;
}

static unsigned char  Get_Enter()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>5)&0x01;
    return tmp;
}

static unsigned char Get_Press_Sw()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>6)&0x01;
    return tmp;
}

static unsigned char Get_PF_Inp()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>7)&0x01;
    return tmp;
}

static unsigned char Get_Reset_Inp()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp)&0x01;
    return tmp;
}

static unsigned char Get_Left()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>1)&0x01;
    return tmp;
}

static unsigned char Get_Right()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>2)&0x01;
    return tmp;
}

static unsigned char Get_Up()
{
    unsigned char tmp;
    tmp=in_ninpot2();
    tmp=(tmp>>3)&0x01;
    return tmp;
}

static unsigned char Get_Cylin()
{
    unsigned char tmp;
    tmp=in_ninpot1();
    tmp=(tmp>>4)&0x01;
    return tmp;
}

static unsigned char Get_Cylout()
{
    unsigned char tmp;
    tmp=in_ninpot1();
    tmp=(tmp>>5)&0x01;
    return tmp;
}

static unsigned char Get_Sensor_1()
{
    unsigned char tmp;
    tmp=in_ninpot1();
    tmp=(tmp>>1)&0x01;
    return tmp;
}

static unsigned char Get_Sensor_2()
{
    unsigned char tmp;
    tmp=in_ninpot1();
    tmp=(tmp>>2)&0x01;
    return tmp;
}

/*测试用读写MAX 7219*/
static void test_max()
{
    send_data(0x0C,0x01);
    send_data(0x0F,0x00); 
    send_data(0x0B,0x07); 
    send_data(0x09,0xFF); 
    send_data(0x0A,0x0F); 

    /*开始显示*/
    send_data(0x01,0x01); 
    send_data(0x02,0x02); 
    send_data(0x03,0x03); 
    send_data(0x04,0x04); 
    send_data(0x05,0x05); 
    send_data(0x06,0x06); 
    send_data(0x07,0x07); 
    send_data(0x08,0x08); 
    printk(DEVICE_NAME "Max show is setted \n");
}

static void test_max1()
{
    send_data(0x0C,0x01); 
    send_data(0x0F,0x00); 
    send_data(0x0B,0x07); 
    send_data(0x09,0xFF); 
    send_data(0x0A,0x0F); 

    /*开始显示*/
    send_data(0x01,0x00);
    send_data(0x02,0x00); 
    send_data(0x03,0x00); 
    send_data(0x04,0x00); 
    send_data(0x05,0x00); 
    send_data(0x06,0x00); 
    send_data(0x07,0x00); 
    send_data(0x08,0x00); 
    printk(DEVICE_NAME "Max show is setted \n");
}

/*测试 6264读写*/
static void test_6264_one()
{
    unsigned char tmp;
    printk(DEVICE_NAME "Address is  :0X%4X %4X\n", (int)(io_mem>>0x10),(int)(io_mem & 0xffff) );
    Mem_Write_Data(io_mem+100,0x55);
    udelay(100000000);
    tmp=0x00;
    tmp=Mem_Get_Data(io_mem+100);

    printk(DEVICE_NAME " test_6264_one Read form mem is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
  
    Mem_Write_Data(io_mem+101,0xAA);
    tmp=0x00;
    tmp=Mem_Get_Data(io_mem+101);
    printk(DEVICE_NAME " test_6264_one Read form mem is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
}

static void test_read()
{
    unsigned char tmp;

    tmp=0x01;
    tmp=Mem_Get_Data(io_mem+100);

    printk(DEVICE_NAME " test_read Read form mem is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
  
    tmp=0x01;
    tmp=Mem_Get_Data(io_mem+101);
    printk(DEVICE_NAME " test_read Read form mem is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
}

static void test_6264()
{
    unsigned char tmp;
    int i,num;
    for(num=0;num<0x1FFF;num++)
    {
        Mem_Write_Data(io_mem+num,0xAA);
    }
    
	printk(DEVICE_NAME " 6264 is writeed, next please run test_check_6264() \n");
}


static void test_check_6264()
{ 
    int num;
    unsigned char tmp;
    for(num=0;num<0x1FFF;num++)
    {
        tmp=Mem_Get_Data(io_mem+num);
        if(tmp!= 0xAA)
        {
            printk(DEVICE_NAME "Read form mem is error 55   :0X%4X %4X  && and the num is :%d\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) ,num);
        }
        tmp=0x00;
    }
 
    printk(DEVICE_NAME " 6264 checked and it's ok \n");
}

/***********************************************************/
/*case 10发送数据*/
static int thj_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned char tmp;
    int i;
    //缓冲区编号,区内行号
    int read_no,line_no;
    //对应的在6264中的地址
    u32 addr_buffer;
    switch(cmd) {
    case 0:    
        //test  nclear
        /*  for(i=0;i<100000;i++)
        {
        out_nsreg1(0x55);
        out_nsreg2(0xAA);
        signal_nclear();
        udelay(1000000);
        out_nsreg1(0xAA);
        out_nsreg2(0x55);
        signal_nclear();
        udelay(1000000);
        }
       */
        //Beep_On();
        //Set_Relay(6);
        if (arg==1)
        {
            Set_out_con(2);
        }
        else
        {
            Clr_out_con(2);
        }
        break;
    case 1:
        test_max();
        //test OUT_EN
        /*for(i=0;i<1000000;i++)
        {
         signal_oe_trig();
         udelay(100000);
        }
        */
        //test_check_6264();
        //test_max();
        //Beep_Close();
        //Clr_Relay(6);
        break;
    case 2: 
        //out_outport1(0x55);
        //write_data_length(1500);
        //Set_Relay(3);
        test_max1();
        break;
    case 3:
        //out_outport1(0xAA);
        //i=read_data_length();
        //printk(DEVICE_NAME "Data Length is : %d\n", i);
        Clr_Relay(3);
        break;
    case 4:
        test_6264();
       
		test_check_6264();
        //tmp=in_ninpot1();
        //printk(DEVICE_NAME "Read form inport 1 is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
        //Set_8255_portc(0);
        //Set_Relay(4);
        break;
    case 5:
        // test_6264_one();
        tmp=in_ninpot2();
        printk(DEVICE_NAME "Read forminport 2 is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
        //Clr_8255_portc(0);
        Clr_Relay(4);
        break;
    case 6:
        //Set_8255_portab(PAOPBO);
        //write_8255_porta(0xAA);
        //write_8255_portb(0x55);
        //write_8255_portc(0xAA);
        Set_Relay(5);
        break;
    case 7:
        Set_8255_portab(PAOPBO);
        write_8255_porta(0x55);
        write_8255_portb(0xAA);
        write_8255_portc(0x55);
        Clr_Relay(5);
        break;
    case 8:
        Set_8255_portab(PAOPBI);
        tmp=read_8255_portb();
        printk(DEVICE_NAME "Read form port B is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
        break;
    case 9:
        Set_8255_portab(PAIPBO);
        tmp=read_8255_porta();
        printk(DEVICE_NAME "Read form port A is:0X%4X %4X\n", (int)(tmp>>0x10),(int)(tmp & 0xffff) );
        break;
    case 10:
        read_no = (arg & 0xFFFFFF00) >>8;
        line_no = (arg & 0xFF);
        printk("\nread data from buf_%d line_%d:\n",read_no,line_no);
        send_line(read_no,line_no);
    default:
        return -EINVAL;
    }
}
/*OS 读取thj 设备驱动*/
ssize_t thj_read (struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    unsigned char data[30];
    int i,j;
    u32 base_addr;
    
    base_addr = io_mem+1500*count;
    /*一行读入30个字节*/
    for (i=0;i<3;i++ )
    {
        base_addr=io_mem+1500*count+150*i;
        for (j=0;j<10;j++ )
        {
            data[i*10+j] = Mem_Get_Data(base_addr+i);
         }
    }
    copy_to_user(buf,data,30);
}

/*OS 写入thj 设备驱动*/
ssize_t   thj_write (struct file *filp, const char *buf,size_t count ,loff_t f_pos)
{
    struct write6264 data;
    int i;
    u32 base_addr;
    /*从用户区拷贝发送单元*/
    copy_from_user((void *)&data,buf,sizeof(struct write6264));
    /*把一行控制序列的长度送到0x20000000+0x1D50*/
    write_data_length(data.leng);
    /*data.buf 缓冲区编号,data.line线色数编号(实际一行中控制序列编号)*/
    base_addr=io_mem+data.buf*1500+data.line*150;
    printk("\nwrite data to buf_%d line_%d:\n",data.buf,data.line);
    
    for(i=0;i<data.leng;i++)
    {
        Mem_Write_Data(base_addr+i, data.data[i]);
        printk("%x ",(char)(data.data[i]));
    }
}

static struct file_operations thj_fops = {
    owner:  THIS_MODULE,
    ioctl:    thj_ioctl,
    read:    thj_read,
    write:   thj_write,
};

/*初始化设备地址映射*/ 
static void thj_init_set()
{
    /*设置 LCD  PORTG*/ 
    __REG(0x56000060)=0xFFFFFFFB;
    io_8255_base=ioremap(ADD_8255,0x400);
    outb(0x80,io_8255_base+0x03);

    io_base=ioremap(ADD_IO,0x400);      
    io_mem=ioremap(ADD_MEM,8*1024); 
    
    State_Relay=0x00;
    temp=0;
}

static devfs_handle_t devfs_handle;
/*使用insmod后调用的初始化函数*/
static int __init thj_init(void)
{
    int ret;
    int i;
    unsigned char tmp;
    
    
    ret = register_chrdev(DEVICE_MAJOR, DEVICE_NAME, &thj_fops);
    if (ret < 0) {
        printk(DEVICE_NAME " can't register major number\n");
        return ret;
    }
    devfs_handle = devfs_register(NULL, DEVICE_NAME, DEVFS_FL_DEFAULT,
    DEVICE_MAJOR, 0, S_IFCHR | S_IRUSR | S_IWUSR, &thj_fops, NULL);
    
    printk(DEVICE_NAME " registered \n");
    
    __REG(0x56000000)=0x007fffff;    
    /*printk(DEVICE_NAME " GPACON seted\n");*/
    __REG(0x48000000)=0x2220D010;    
    BWSCON = (BWSCON & ~(BWSCON_DW2)) | (BWSCON_DW(2, BWSCON_DW_8));     
    BWSCON = (BWSCON & ~(BWSCON_DW4)) | (BWSCON_DW(4, BWSCON_DW_8));     
    BWSCON=(~((~BWSCON)|0x000F0F00));    


    __REG(0x4800000C)=0x5780;    
    __REG(0x48000014)=0x5780;   
    BANKCON4=BANKCON_Tacs0 | BANKCON_Tcos4 | BANKCON_Tacc14 | BANKCON_Toch1 | BANKCON_Tcah4 | BANKCON_Tacp6 | BANKCON_PMC1;
	/*初始化,地址映射*/
	thj_init_set();


    /*TIMER0 interrupt*/
    if (request_irq(IRQ_TIMER0,&TIMER0_irq,SA_INTERRUPT,DEVICE_NAME,&TIMER0_irq))
    {
        printk(DEVICE_NAME " can't request TIMER0_irq!\n");
        return -1;    
    }
    /*TIMER1 interrupt*/
    if (request_irq(IRQ_TIMER1,&TIMER1_irq,SA_INTERRUPT,DEVICE_NAME,&TIMER1_irq))
    {
        printk(DEVICE_NAME " can't request TIMER1_irq!\n");
        return -1;    
    }
    
    timer_config(127);

    return 0;
}

/*rmmod 卸载驱动后执行*/
static void __exit thj_exit(void)
{
    iounmap(io_8255_base);
    iounmap(io_base);
    iounmap(io_mem);
    free_irq(IRQ_TIMER0,TIMER0_irq);
    free_irq(IRQ_TIMER1,TIMER1_irq);
    
    
    devfs_unregister(devfs_handle);
    unregister_chrdev(DEVICE_MAJOR, DEVICE_NAME);
}

module_init(thj_init);
module_exit(thj_exit);

MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;
