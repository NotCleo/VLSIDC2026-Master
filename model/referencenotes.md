# VLSID2026


SubID = 0062

Most visited URLS are 

    https://chatgpt.com/share/68a9d839-f9f0-8013-bfcf-3cb1bb41fc4f - Shravan's GPT context
    
    
    https://www.microchip.com/en-us/development-tool/mpfs-icicle-kit-es
    https://github.com/Microchip-Vectorblox/VectorBlox-SDK/blob/master/tutorials/onnx/yolov7/yolov7.sh
    https://github.com/polarfire-soc/icicle-kit-reference-design?tab=readme-ov-file
    https://github.com/Microchip-Vectorblox/VectorBlox-Video-Kit-Demo?tab=readme-ov-file
    https://web.pa.msu.edu/people/edmunds/Disco_Kraken/PolarFire_SoC_Register_Map/PF_SoC_RegMap/pfsoc_regmap.htm
    file:///home/amrut/Downloads/microchip_polarfire_soc_fpga_icicle_kit_user_guide_vb.pdf (obv this is only for me)
    https://github.com/open-mmlab/mmaction2
    https://github.com/mrdbourke/pytorch-deep-learning
    https://github.com/polarfire-soc/icicle-kit-reference-design/blob/master/diagrams/block_diagram.svg
    https://microchip.my.site.com/microchip/s/topic/a5CV4000000ApdxMAC/t403864


---------------------------------------------------------------------------------------------------------------------------------------------


The event registration is here - https://vlsid.org/design-contest/


1) We have flashed the board with a SD flashed with the Ubuntu(RV variant) - ubuntu-24.04.3-preinstalled-server-riscv64+icicle.img.xz
2) Flashing can be done via the RPI-imager tool as well, auto collapses the partition and makes fresh GPT partitions
3) There are two modes of booting, eMMC and via Linux in SD -> The SD card is the physical MUX (Refer BootProcess.png)
4) We applied for the Silver License for both Libero SoC and CoreVectorBlox
5) We can access the Board via PuTTy at 115200 at /dev/ttyUSB1 to access the Ubuntu boot and plug out SD to accesss eMMC boot
6) Refer FAQ's for downloads of License, Libero, CoreVectorIP and other related Doubts

---------------------------------------------------------------------------------------------------------------------------------------------


# Steps to Download, Install, and Launch Libero SoC 2025.1 on Ubuntu 24.04 LTS

1. **Found MAC ID and hostname**
   /sbin/ifconfig -a | grep ether
   hostname

2. **Got license from Microchip**
   Downloaded License.dat from Microchip and placed into ~/Downloads

3. **Created license directory and extracted license daemons**
   sudo mkdir -p /opt/microchip/license
   sudo tar -xvzf ~/Downloads/Linux_Licensing_Daemon_11.19.6.0_64-bit.tar.gz -C /opt/microchip/license

4. **Copied License.dat into license folder**
   sudo cp ~/Downloads/License.dat /opt/microchip/license/

5. **Started license server with log file**
   cd /opt/microchip/license
   sudo /opt/microchip/license/lmgrd -c /opt/microchip/license/License.dat -l /opt/microchip/license/license.log

6. **Fixed FlexLM missing temp directory error**
   sudo mkdir -p /usr/tmp/.flexlm
   sudo chmod 777 /usr/tmp/.flexlm

7. **Set license environment variable**
   export LM_LICENSE_FILE=1702@Maverick

8. **Unzipped Libero 2025.1 web installer**
   unzip ~/Downloads/libero_soc_2025.1_online_lin.zip -d ~/Downloads/libero2025_installer
   cd ~/Downloads/libero2025_installer/Libero_SoC_2025.1_online_lin
   chmod +x Libero_SoC_2025.1_online_lin.bin

9. **Fixed missing dependency (libxcb-cursor)**
   sudo apt update
   sudo apt install libxcb-cursor0

10. **Fixed missing dependency (libpng15) by building from source**
    cd ~/Downloads
    wget https://sourceforge.net/projects/libpng/files/libpng15/older-releases/1.5.15/libpng-1.5.15.tar.gz
    tar -xzf libpng-1.5.15.tar.gz
    cd libpng-1.5.15
    ./configure --prefix=/usr/local
    make
    sudo make install
    sudo ldconfig

    Optional if needed
    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

11. **Ran the Libero installer**
    cd ~/Downloads/libero2025_installer/Libero_SoC_2025.1_online_lin
    ./Libero_SoC_2025.1_online_lin.bin

12. **Installed additional required packages for FP6 / Designer tools**
    sudo /home/amrut/microchip/Libero_SoC_2025.1/req_to_install.sh
    sudo /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/fp6_env_install

13. **Fixed GLIBCXX mismatch by using system libstdc++ instead of bundled one**
    mv /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/lib64/rhel/libstdc++.so.6 \
       /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/lib64/rhel/libstdc++.so.6.bak

14. **Launched Libero successfully**
    /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/libero 

15. **(Optional) Create alias for convenience**
    echo 'alias libero="/home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/libero"' >> ~/.bashrc
    source ~/.bashrc

---------------------------------------------------------------------------------------------------------------------------------------------

7) *IMPORTANT* - run /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/libero everytime I have to open Libero

8) see this demo - https://www.youtube.com/watch?v=_4nW-BgvGoU

9) Prof wants us to use a Robot?! - https://clearpathrobotics.com/turtlebot-4/

10) If you face issues regarding the license validity, perform these debug statements; if "/home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/libero" does not work

run these, 

     ps -ef | grep lmgrd
     tail -n 100 /opt/microchip/license/license.log
     echo $LM_LICENSE_FILE
     export LM_LICENSE_FILE=1702@Maverick
     echo 'export LM_LICENSE_FILE=1702@Maverick' >> ~/.bashrc
     verify using echo $LM_LICENSE_FILE again
     /opt/microchip/license/lmutil lmstat -a -c 1702@Maverick
     cd /opt/microchip/license/
     do a list all to verify license.dat's presence
     sudo ./lmgrd -c /opt/microchip/license/License.dat -l /opt/microchip/license/license.log
     ps aux | grep lmgrd
     /opt/microchip/license/lmutil lmstat -a -c 1702@Maverick -> veriy if you get the Actel_BASESoC
     now run -> /home/amrut/microchip/Libero_SoC_2025.1/Libero_SoC/Designer/bin/libero


---------------------------------------------------------------------------------------------------------------------------------------------

1) Start the license server manually
cd /opt/microchip/license
sudo ./lmgrd -c /opt/microchip/license/License.dat -l /opt/microchip/license/license.log

2) Verify the license server is running
ps aux | grep lmgrd

3) Check license server status
/opt/microchip/license/lmutil lmstat -a -c 1702@Maverick

4) (Optional) Make the license server start automatically on boot
sudo nano /etc/systemd/system/microchip-license.service

---------------------------------------------------------------------------------------------------------------------------------------------

11) I downloaded the SoftConsole as mentioned in the vectorblox and it gave the post installation guide here (dont judge it's only for my laptop) - file:///home/amrut/Microchip/SoftConsole-v2022.2-RISC-V-747/documentation/softconsole/using_softconsole/post_installation.html

---------------------------------------------------------------------------------------------------------------------------------------------

12) I needed to get the desktop shortcut sorted out for the SoftConsole, or just run - /home/amrut/Microchip/SoftConsole-v2022.2-RISC-V-747/softconsole.sh everytime to launch the SoftConsole, btw i was told to read the post installation guide - file:////home/amrut/Microchip/SoftConsole-v2022.2-RISC-V-747/documentation/softconsole/using_softconsole/post_installation.html

13) Refer eYantra's learning tasks for questions like, "What is rootfs?, how does the bootloader work in linux, what is a bootloader, what is firmware, image? and what is this /dev, /usr, /bin, /proc, ... all these directories which are different from my /Downloads,/Desktop, .... and what is Uefi, GRUB, ....?"

14) BTW ubuntu distros for Microchip was from here - https://canonical-ubuntu-boards.readthedocs-hosted.com/en/latest/how-to/microchip-polarfire-icicle/

15) In case you get an issue like "Our HSS - v0.99.36 - 2023, so we tried Ubuntu 24 and 22, and in either cases the HSS payload isnt being detected (the Bootloader isnt able to find the "image's header offset")" Just reflash the SD with the RPI viewer after downloading the linux image you want to flash, the header offsets if not matching with HSS payload's expected offset will not allow to boot

Note : No. On the Icicle Kit, HSS runs from eNVM, not QSPI; QSPI contents aren’t involved in this Ubuntu boot flow. Updating QSPI won’t fix this symptom." see https://github.com/polarfire-soc/hart-software-services
 if required.

16) Note to self - reset is SW4 on the board and do have a look at the diagram above always


-> Had the coco2017's calibration dataset (as a bin file), and we would PTQ the yolo (actually the bash script is what's doing it automatically because it's preloaded), so we have not one but many such "calibration datasets" (hopefully as a bin file) for the POSENET (this is what i understood, please correct me), so this is how the quantization problem came up, we will need to figure this out


17) For the SD card to boot reliably, the j34/j43 jumpers must be short pins 1 and 2 to set 1.8 volts, if we want to verify see the video on the polarfire kit's website. If the jumpers are shorted on pin 2 and 3, it will be set at 3.3volts (reference for pin 1 2 3 regarding how to read is, keep the power pin of the board facing left and then read the pins 123 from right to left), video is here btw - https://www.youtube.com/watch?v=wip-mpxsR8k&t=35s



18) Extra info : if we cat /dev/bus/usb/00x (x=1,2,3,4,5,6,7,8,9,10), we are being dumped with the .lss format (I guess for the peripheral GPIO-x)

19) DO read : https://www.microchip.com/en-us/products/fpgas-and-plds/system-on-chip-fpgas/polarfire-soc-fpgas/asymmetric-multiprocessing

pcie interfacing / lan/dhcp if linux done / sd or emmc to boot

20) If we ever forget the password use this - https://askubuntu.com/questions/24006/how-do-i-reset-a-lost-administrative-password

Our login - ubuntu
Our pwd - ubuntu

:) Why did I even write this down here!


21) To check liecense - https://www.microchipdirect.com/fpga-software-products, I'm using my amrutayan6@gmail.com ID

22) I cannot use MobaXterm why? No cp120x Device Driver!!

23) Quick References while on UART :
        
        free -h : to check RAM
        lscpu  to check cpu stats
        df -h : to check disc utilization

24) Now I need to figure out how to control the peripherals of the board, like GPIO/PWM via kernel and not via bare-metal (Soft console)

A silly observation : What is "User LEDS"? is it an external LED that I have to connect or the onboard LEDS? - Well its the onboard LEDs! and the linux-examples repo talks about controlling the LED (GPIO) via SW2/3 so we will click on/off to turn on/off the GPIO flowstate.


25) How do I set up LAN + Test GPIO (Referring Linux-example) on the board?!

        Physically set up LAN (cat6) between Me and Board at Eth1

On the PuTTy Perform these

    I'm not a root user, hence do sudo before every command!!
    lsblk -> show block /dev and mountpoints (checks rootfs)
    sudo apt install -y build-essential git libgpiod-dev pkg-config -> install build essentials and libgpiod (runtime + dev for compiling examples)
    But above command requires LAN set up;
    sudo ip link set eth1 up
    sudo dhclient eth1
    ip addr show eth1
    inet 10.x.x.x or 192.168.x.x  (means you got an IP)
    ping -c 3 google.com
    echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf ( run ping -c 3 8.8.8.8 and if only that worked and not ping google then run this line)
    Perform "gpiodetect" after LAN set up!
    sudo apt install -y gpiod -> If it does not work
    ls /sys/class/leds
    sudo mkdir -p /opt/microchip/gpio
    cd /opt/microchip/gpio
    open up the two gpio-event.c and gpio-test.c and compile them from /opt/microchip/gpio
    (I believe we cannot vim because PuTTy sucks so git clone https://github.com/polarfire-soc/polarfire-soc-linux-examples.git at /opt/microchip/gpio!)
    sudo make -> To build the Makefile
    gcc -o gpiod-test gpiod-test.c -lgpiod
    gcc -o gpiod-event gpiod-event.c -lgpiod
    sudo ./gpiod-test
    sudo ./gpio-event

On my local machine Perform these : 

    Do settings > network > wired > IPv4 > Shared to other computers > apply after LAN cable set up



The board is currently running the Ubuntu image from Canonical, which is a generic Linux image. That explains why /dev/gpiochip* does not exist — the kernel in that image does not have the Microchip FPGA GPIO driver (gpio-mpfs) built, nor the GPIO character device support.

To check OS on the container - cat /etc/os-release


While taking and sending files to and fro the GPU via ssh use scp command : 

scp [OPTIONS] [[user@]source_host:]file1 [[user@]dest_host:]file2






