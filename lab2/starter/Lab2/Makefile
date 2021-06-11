# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS = -D_BSD_SOURCE -Wall -g -std=gnu99 # compilation flags
LD = gcc       # linker
LDFLAGS = -std=c99 -g   # debugging symbols in build
LDLIBS = -lz -lcurl -pthread # link with libz

# For students 
LIB_UTIL = 
SRCS   = paster.c #main.c
OBJS   = paster.o $(LIB_UTIL) 

TARGETS= paster #main.out

all: ${TARGETS}

paster: $(OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 


%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS) *.png
