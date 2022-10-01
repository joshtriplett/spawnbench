BENCHES := fork posix_spawn vfork
TARGETS := $(BENCHES) t

all: $(TARGETS)

bench: all
	@for s in $(BENCHES); do PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin:. ./$${s} ; done

clean:
	rm -f $(TARGETS)

$(BENCHES): bench.c
	gcc -static -DUSE_$@ -DBENCH_NAME=\"$@\" -Wall -Werror -O0 -g $< -o $@

ARCH=$(shell uname -m)

t: t-$(ARCH).S
	gcc -static -nostartfiles -nodefaultlibs -Wl,--build-id=none -s $< -o $@
