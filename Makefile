.EXPORT_ALL_VARIABLES:

EXEC = elevator_mgr
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c = .o)

RTDIR = $(shell pwd)/..

CFLAGS += -I$(ROOTDIR)/lib/libcjson -I$(ROOTDIR)/app/curl/curl/include -I$(ROOTDIR)/lib/libnvram
LDFLAGS += -lcjson -lm -lnvram -lcfg -L$(ROOTDIR)/app/curl/curl/lib/.libs -lcurl -L$(ROOTDIR)/app/openssl-1.0.0l/openssl-1.0.0l -lssl -lcrypto -lz -lpthread
CFLAGS += -I$(ROOTDIR)/lib/libfiletapi -I$(LIBUSEDIR)
LDFLAGS += -L$(ROOTDIR)/lib/lib -luseful -lcfg -lfiletapi

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) 

%.o:%.c
	$(CC) -c $< $(CFLAGS)

romfs:
	$(ROMFSINST) /bin/$(EXEC)

clean:
	-rm -f $(EXEC) *.elf *.gdb *.o *~
