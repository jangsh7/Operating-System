KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(CURDIR)

obj-m := ftracehooking.o iotracehooking.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

