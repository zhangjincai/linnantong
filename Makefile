

CC=arm-none-linux-gnueabi-gcc
AR=arm-none-linux-gnueabi-ar
STRIP=arm-none-linux-gnueabi-strip
CFLAGS = -Wall -g -O2 -D_GNU_SOURCE



SRCS = $(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)



all: $(OBJS) lib

$(OBJS): %.o:%.c
	$(CC) $(CFLAGS) -c -o $@ $< 

lib: $(OBJS)
	$(AR) rcu liblnt.a $(OBJS) general.o md5.o aes.o des.o 




clean:
	@rm -f device.o lnt.o var_data.o
	@rm -f liblnt.a




