obj-m += matmul.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

GIT_HOOKS := .git/hooks/applied

all: $(GIT_HOOKS) user matmul

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

matmul:
	make -C $(KDIR) M=$(PWD) modules

user: user.c
	$(CC) $(CFLAGS) -o $@ $^

check: matmul user
	sudo insmod matmul.ko
	sudo ./user
	sudo rmmod matmul.ko

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f user

