obj-m += movbe_emulator.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	sudo insmod movbe_emulator.ko

unload:
	sudo rmmod movbe_emulator

dmesg:
	dmesg | tail -20
