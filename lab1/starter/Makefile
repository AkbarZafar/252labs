# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS = -D_BSD_SOURCE -Wall -g -std=c99 # compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz   # link with libz

# For students 
LIB_UTIL = zutil.o crc.o
SRCS   = pnginfo.c crc.c zutil.c findpng.c catpng.c #main.c
#OBJS   = main.o $(LIB_UTIL) 
OBJS2   = pnginfo.o $(LIB_UTIL) 
OBJS3   = findpng.o $(LIB_UTIL) 
OBJS4   = catpng.o $(LIB_UTIL) 

TARGETS= pnginfo findpng catpng #main.out

all: ${TARGETS}

#main.out: $(OBJS) 
#	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 


pnginfo: $(OBJS2)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

findpng: $(OBJS3)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

catpng: $(OBJS4)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS) 
