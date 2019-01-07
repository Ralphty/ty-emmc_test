
#CC	= cc
#C89	= c89
#GCC	= gcc
#CCS	= /usr/ccs/bin/cc
#NACC	= /opt/ansic/bin/cc
CFLAGS	=
S10GCCFLAGS    = -m64
S10CCFLAGS     = -m64
FLAG64BIT      = -m64

burnintest:burnintest.c
	$(CC) -DDEBUG -O3 -g $(LDFLAGS) $< -o $@

clean:
	rm burnintest
	rm *.o
