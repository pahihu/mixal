# mixal
Based on [MIXAL](http://github.com/darius/mixal) from Darius Bacon. 
See `runhello` for example usage.

You can check James L. Peterson's [Computer Organization and Assembly Language Programming](http://www.jklp.org/profession/books/mix/index.html) book, which
uses the MIX machine.

For a reference see the [MIX Reference Card](https://github.com/pahihu/mixal/blob/master/mixref.pdf).


Changes:

  * fixed INT encoding (C=5,F=9), added MIX360 user manual and UT-MIX manual
  * ALF statement without quotes
  * assemble only
  * core memory support (non-volatile memory)
  * punch object card deck
  * GO button
  * MIXmaster instructions added: XEQ (execute), CPrM (CI = reg:M)
  * indirect/double-addressing
  * UT-MIX instructions added: NEG, XCH
  * binary instructions: OR, XOR, AND, SLB, SRB, JrE, JrO
  * interrupt facility, real-time clock (INT instruction)
  * limited size of drum and tape devices
  * memory dump of non-zero locations, with instruction frequency counts
  * instruction execution trace
  * count idle time waiting for I/O to complete
  * MinGW port
  * overlapping I/O buffer checks, incomplete I/O detection
  * [reference card](https://github.com/pahihu/mixal/blob/master/mixref.pdf)
  * card reader/punch and printer can use files instead of stdin/stdout
  * time dependent I/O (low-end Burroughs B5500 peripheral timing)
  * tape devices


**NOTE**: execution trace and memory dump mechanics loosely 
follows [Knuth, Sites: MIX/360 User's Guide](https://github.com/pahihu/mixal/blob/master/doc/CS-TR-71-197.pdf). For the UT-MIX extensions see [Peterson: UT-MIX Reference Manual](https://github.com/pahihu/mixal/blob/master/doc/TR77-64.pdf). Good luck!


