scanme - a safe honeypot
===

This is a *honeypot*, a program designed to mimic real services on the network
in order to log hackers trying to hack those services, and to delay them
by wasting their time. Bugs/vulnerabilities in honeypots may themselves
enable hacker attacks, this is written with extra paranoia. The services
are written in a strongly sandboxed Lua scripting environment.

Some major attributes are:
	* Lua -- All the services are written in the Lua scripting language, which
	  is popular for other cybersec projects, such as Snort, nmap, and Wireshark.
	* Sandbox -- Instead of a standard Lua environment, this has been sandboxed.
	  There is no generic access to the filesystem, for example, only a tightly
	  controlled one. Of course, the expectation is that there will be further
	  containerization and VMs securing this as well.
	* Scale -- Takes advantage of scalable APIs like `epoll()` in order to support
	  massive numbers of connections, so that firewalls/routers can be configured
	  to redirect traffic to this device.
	* Multi-core -- Designed to use all the CPU cores in the system.
	* Lightweight -- Designed to be run on low-end $19 wifi routers that have been
	  reflashed with OpenWRT that may have only 64-megabytes of memory.
	* Portable -- Runs on Linux, macOS, Windows, as well as pretty much any operating
	  system that supports ANSI C and POSIX Sockets. This can include things like AIX,
	  Solaris, QNX, VxWorks, and so on.
	* Functional -- Many of the mock services are fully functional, so you could use
	  this as a real web-server for example.
	* User-mode -- Some honeypots are designed to mimic other operating systems at
	  the TCP stack level. In contrast, this is designed to run in user-mode, using
	  whatever TCP/IP stack is on the system.
    * UPnP client -- Uses UPnP so that it can open up listening ports on the public
	  Internet from behind a home NAT router.


Building
===

The code is ANSI C, so the files can just be compiled with your favorite
compiler.

Or you could use the `Makefile`, which is written for `gmake` and works for
a lot of platforms, such as Linux, macOS, and MingGW.

There are also IDE projects for Visual Studio in the `vs10` directory and 
for XCode in the `xcode` directory.

Running
===

Run `scanme configfile.conf` to run as configured in the specified configuration
file.

Run `scanme somescript.lua` to run a service where all the necessarry settings
come from the script itself or from defaults.

