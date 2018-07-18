# mixal
Based on [MIXAL](http://github.com/darius/mixal) from Darius Bacon.

Changes:

  * tape devices
  * time dependent I/O (low-end Burroughs B5500 peripheral timing)
  * card reader/punch and printer can use files instead of stdin/stdout
  * reference card (see mixref.pdf)
  * overlapping I/O buffer checks, incomplete I/O detection
  * MinGW port
  * count idle time waiting for I/O to complete
  * instruction execution trace
  * memory dump of non-zero locations, with instruction frequency counts
  * limited size of drum and tape devices
  * interrupt facility
  * real-time clock
  * UT-MIX instructions added: OR, XOR, AND, NEG, XCH, SLB, SRB, JrE, JrO
  * UT-MIX indirect addressing

**NOTE**: execution trace and memory dump mechanics loosely 
follows Knuth, Sites: MIX/360 User's Guide, Stanford, March, 1971. For the UT-MIX extensions see Peterson: UT-MIX Reference Manual, Uni. Texas, Jan, 1977. Good luck!


