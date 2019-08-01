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
  * interrupt facility, real-time clock (INT instruction)
  * binary instructions: OR, XOR, AND, SLB, SRB, JrE, JrO
  * UT-MIX instructions added: NEG, XCH
  * indirect addressing
  * Mixmaster instructions added: XEQ (execute), CPrM (CI = reg:M)
  * GO button
  * punch object card deck
  * core memory support (non-volatile memory)
  * assemble only

**NOTE**: execution trace and memory dump mechanics loosely 
follows Knuth, Sites: MIX/360 User's Guide, Stanford, March, 1971. For the UT-MIX extensions see Peterson: UT-MIX Reference Manual, UT Austin, Jan, 1977. Good luck!


