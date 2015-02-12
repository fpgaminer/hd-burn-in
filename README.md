HD Burn In
##########

What is this?
=============

Test a hard drive by writing random data to the entire disk, and then reading back and verifying it.  Built for Linux.


Why?
====

I couldn't find a decent tool on Linux to do this, and it was simple enough to make, so I built my own.  I like to run it to make sure new disks are working after I buy them.


Anything else?
==============

The tool is just a simple command line program.  Only one argument, the target disk (e.g. /dev/sdc).  It shows speed and progress while it runs.  The random data is generated with a very fast PRNG, so the only bottleneck is the disk speed.  A 2TB hard drive will take about half a day to test (100MB/s read and write speed).  The program could probably be enhanced with a quick test mode that just writes data sparsely around the drive, but I feel safer with a full test anyway.


Warning
=======

This will *overwrite* any data already on the disk.  That should be obvious from the description.
