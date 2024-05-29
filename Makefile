BENCHES := fork posix_spawn vfork clone_vm io_uring_spawn
TARGETS := $(BENCHES) t

all: $(TARGETS)

bench: all
	@for s in $(BENCHES); do PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin:. ./$${s} ; done

clean:
	rm -f $(TARGETS)

CFLAGS_io_uring_spawn = $(shell PKG_CONFIG_PATH=/tmp/liburing-spawn/lib/pkgconfig pkg-config --cflags liburing)
LIBS_io_uring_spawn = $(shell PKG_CONFIG_PATH=/tmp/liburing-spawn/lib/pkgconfig pkg-config --libs liburing)

$(BENCHES): bench.c
	gcc -static -DUSE_$@ -DBENCH_NAME=\"$@\" $(CFLAGS_$@) -Wall -Werror -O0 -g $< $(LIBS_$@) -o $@

ARCH=$(shell uname -m)

t: t-$(ARCH).S
	gcc -static -nostartfiles -nodefaultlibs -Wl,--build-id=none -s $< -o $@
