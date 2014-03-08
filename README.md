Compiling
---------

You will need to install Qt4 development packages.
On the Raspberry Pi, run:

	sudo apt-get update
	sudo apt-get install qt4-dev-tools
	
and then, in the `rpiclock` source directory:

	qmake-qt4 rpiclock.pro
	make

You need ntpd `running` and synchronised.
Make sure that `/etc/ntp.conf` allows ntp queries via the local interface:

	restrict 127.0.0.1
	restrict ::1
	
Setting up a Raspberry Pi 
-------------------------

Assuming the user pi is running `rpiclock`:

/etc/inittab needs the line

	1:2345:respawn:/sbin/getty --noclear 38400 tty
	
changed to

	1:2345:respawn:/bin/login -f pi tty1 </dev/tty1 >/dev/tty1 2>&1

so that pi is automatically logged in on tty1

/etc/rc.local needs the line

	su -l pi -c startx
	
to automatically start X-windows

In pi's home directory you need the file

	.config/autostart/rpiclock.desktop
	
which contains

	[Desktop Entry]
	Type=Application
	Exec=/path/to/rpiclock

to automatically run `rpiclock`.


Startup on system boot is a bit slow. The kernel will not declare "synchronised" until about 15 minutes after boot
so the time will not be displayed during this period. This is a bit pernickety but I have an aversion to displaying
the wrong time.

Power management
----------------

The `tvservice` tool is used on the Rapsberry Pi. This has worked fine for me on an LCD monitor. The monitor and the backlight go off.

On other Linuxen+x386, YMMV. I tried `dpms` and `vbetool` but there were problems. With `xset`, the backlight would go off briefly and then come back on. With `vbetool`, there were occasional freezes of up to 30s before the monitor turned off. Unfortunately there is no standard way of controlling the monitor in Linux so power management may not work for you.

On Debian systems, `vbetool` needs to run via `sudo` so to disable the password for just `vbetool` you need to edit /etc/sudoers:

	user_name ALL=(ALL) NOPASSWD: /usr/sbin/vbetool
	
Configuration file
------------------

`rpiclock` uses a configuration file `rpiclock.xm`l. The comments in the file should be enough to get you going.
The search path for this is `./:~/rpiclock:~/.rpiclock:/usr/local/etc:/etc`
All other paths are explicit.

Known bugs/quirks
-----------------

The power on/power off logic assumes on < off.