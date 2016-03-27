# be-thesis

This repo archives drivers and applications that I developed for my bachelor's
thesis project "Computer Controlled Carpet Weaving Machine" from Aug. 2007 to 
May 2009 in the Embedded System Laboratory at Harbin Institute of Technology 
at Weihai. This project used Samsung S3C2410 as the development board. I ported
Linux 2.4 to the ARM board and all codes are developed using Arm-Linux C.

This work is very different with what I am doing right now, but I was very 
passionate about it at that time. For almost 10 years, I can't remeber all the 
details, so I decide to host it on Gihub for my records.

It is always interesting to revisit old works. Although the codes were not
writtern in a good standards. I might re-style it but other than this, the
roject will not be updated any more.

## File Tree
<pre>
|-- <b>control_modules</b>
    |-- cntrl_main.c <i>(main module that controls weaving machine operations)</i>  
    |-- cntrl_support.c <i>(support module that caches weaving data)</i>  
    |-- cntrl_usb.c <i>(usb module thats downloads weaving data from USB drive)</i>  
    |-- test_simulate.c <i>(simluate user input to test the control modules)</i>  
|-- <b>driver</b>
    |-- thj.h/thj.c <i>(device driver)</i>  
|-- <b>kernel_mods</b>
    |-- check.c <i>(edited Linux source code. Record USB device id when registered)</i>  
    |-- zlmage24 <i>(compiled modif)</i>  
|-- <b>circut</b>
    |-- circut_board.DDB/Bkp <i>(designed by my advisor Prof. Weigong Lv)</i>  
</pre>
## Driver Compile

```
arm-linux-gcc -D__KERNEL__ -I/friendly-arm/kernel/include -DMODULE -c -o thj.o thj.c
```

## History
Codes were written in 2008. No update will be made at this point.