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

**NOTE**: execution trace and memory dump mechanics loosely 
follows Knuth, Sites: MIX/360 User's Guide, Stanford, March, 1971


