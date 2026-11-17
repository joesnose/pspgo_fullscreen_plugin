TARGET = PSPGoTVScale
OBJS = main.o render.o effects.o
CFLAGS = -include build_compat.h -O2 -G0 -Wall -Wextra -Werror -std=gnu11 -fno-pic
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
USE_KERNEL_LIBS = 1
USE_KERNEL_LIBC = 1
PSP_FW_VERSION = 661
LDFLAGS = -nostartfiles
LIBS = -lpspsystemctrl_kernel -lpspge_driver -lgcc
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
main.o: render.h build_compat.h effects.h write_guard.inc native_output.inc scanout_probe.inc ge_api_guard.inc
render.o: render.h

effects.o: render.h effects.h
