CFLAGS := -g -Wall -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -lpthread

all: host bridge bpdu

bpdu:
host:
bridge:

clean:
	rm bpdu host bridge
