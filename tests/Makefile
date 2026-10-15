# Optional Linux tests; not a native kernel build.
CC ?= cc
CPPFLAGS += -DQGA_HOST_TEST
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror
OUT = ../build

.PHONY: all test sanitize analyze
all: $(OUT)/qga9-vconsole-irq-test $(OUT)/qga9-vconsole-irq-thread-test $(OUT)/qga9-vconsole-pump-test $(OUT)/qga9-vconsole-stream-test $(OUT)/qga9-vconsole-controller-test $(OUT)/qga9-vconsole-worker-test $(OUT)/qga9-vconsole-io-test $(OUT)/qga9-vconsole-dev-test $(OUT)/qga9-vconsole-owner-pc-test $(OUT)/qga9-vconsole-boot-test $(OUT)/qga9-vconsole-test $(OUT)/qga9-vconsole-mmio-test $(OUT)/qga9-vconsole-pc-test $(OUT)/qga9-vconsole-dma-test
all: $(OUT)/qga9-vconsole-checked-irq-test

$(OUT)/qga9-vconsole-boot-test: vconsole-boot-test.c vconsole-boot-shim.h vconsole-owner-pc-shim.h vconsole-pc-shim.h ../kernel/vconsole-boot-pc.c ../kernel/vconsole-owner-pc.h $(DEV_HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-boot-test.c

$(OUT):
	mkdir -p $(OUT)

$(OUT)/qga9-vconsole-test: vconsole-test.c ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-test.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-mmio-test: vconsole-mmio-test.c ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-mmio-test.c ../kernel/vconsole-mmio.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-pc-test: vconsole-pc-test.c vconsole-pc-shim.h ../kernel/vconsole-pc.c ../kernel/vconsole-pc.h ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-pc-test.c ../kernel/vconsole-pc.c ../kernel/vconsole-mmio.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-dma-test: vconsole-dma-test.c ../kernel/vconsole-dma.c ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-dma-test.c ../kernel/vconsole-dma.c ../kernel/vconsole-mmio.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-irq-test: vconsole-irq-test.c ../kernel/vconsole-irq.c ../kernel/vconsole-irq.h ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-irq-test.c ../kernel/vconsole-irq.c

$(OUT)/qga9-vconsole-irq-thread-test: vconsole-irq-thread-test.c ../kernel/vconsole-irq.c ../kernel/vconsole-irq.h ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-irq-thread-test.c ../kernel/vconsole-irq.c

$(OUT)/qga9-vconsole-pump-test: vconsole-pump-test.c vconsole-fixture.h ../kernel/vconsole-pump.c ../kernel/vconsole-pump.h ../kernel/vconsole-dma.c ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-pump-test.c ../kernel/vconsole-pump.c ../kernel/vconsole-dma.c ../kernel/vconsole-mmio.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-stream-test: vconsole-stream-test.c vconsole-fixture.h ../kernel/vconsole-stream.c ../kernel/vconsole-stream.h ../kernel/vconsole-pump.c ../kernel/vconsole-pump.h ../kernel/vconsole-dma.c ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ vconsole-stream-test.c ../kernel/vconsole-stream.c ../kernel/vconsole-pump.c ../kernel/vconsole-dma.c ../kernel/vconsole-mmio.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-controller-test: vconsole-controller-test.c ../kernel/vconsole-controller.c ../kernel/vconsole-controller.h ../kernel/vconsole-stream.c ../kernel/vconsole-stream.h ../kernel/vconsole-pump.c ../kernel/vconsole-pump.h ../kernel/vconsole-dma.c ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole-irq.c ../kernel/vconsole-irq.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-controller-test.c ../kernel/vconsole-controller.c ../kernel/vconsole-stream.c ../kernel/vconsole-pump.c ../kernel/vconsole-dma.c ../kernel/vconsole-mmio.c ../kernel/vconsole-irq.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-controller-test: vconsole-controller-fixture.h
$(OUT)/qga9-vconsole-owner-pc-test: ../kernel/vconsole-worker-pc.h ../kernel/vconsole-worker.h ../kernel/vconsole-controller-pc.h

$(OUT)/qga9-vconsole-owner-pc-test: vconsole-owner-pc-test.c vconsole-owner-pc-shim.h vconsole-pc-shim.h ../kernel/vconsole-owner-pc.c ../kernel/vconsole-owner-pc.h ../kernel/vconsole-pc.c ../kernel/vconsole-pc.h ../kernel/vconsole-wake.h ../kernel/vconsole-controller.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole-irq.c ../kernel/vconsole-irq.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-owner-pc-test.c ../kernel/vconsole-owner-pc.c ../kernel/vconsole-pc.c ../kernel/vconsole-mmio.c ../kernel/vconsole-irq.c ../kernel/vconsole.c

WORKER_CORE = ../kernel/vconsole-worker.c ../kernel/vconsole-controller.c ../kernel/vconsole-stream.c ../kernel/vconsole-pump.c ../kernel/vconsole-dma.c ../kernel/vconsole-mmio.c ../kernel/vconsole-irq.c ../kernel/vconsole.c

$(OUT)/qga9-vconsole-checked-irq-test: vconsole-owner-pc-test.c vconsole-owner-pc-shim.h ../kernel/vconsole-owner-pc.c ../kernel/vconsole-owner-pc.h ../kernel/vconsole-pc.c ../kernel/vconsole-pc.h ../kernel/vconsole-wake.h ../kernel/vconsole-controller.h ../kernel/vconsole-mmio.c ../kernel/vconsole-mmio.h ../kernel/vconsole-irq.c ../kernel/vconsole-irq.h ../kernel/vconsole.c ../kernel/vconsole.h | $(OUT)
	$(CC) $(CPPFLAGS) -DVCONSOLE_CHECKED_IRQ $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-owner-pc-test.c ../kernel/vconsole-owner-pc.c ../kernel/vconsole-pc.c ../kernel/vconsole-mmio.c ../kernel/vconsole-irq.c ../kernel/vconsole.c
WORKER_HEADERS = ../kernel/vconsole-worker.h ../kernel/vconsole-controller.h ../kernel/vconsole-stream.h ../kernel/vconsole-pump.h ../kernel/vconsole-dma.h ../kernel/vconsole-mmio.h ../kernel/vconsole-irq.h ../kernel/vconsole.h
$(OUT)/qga9-vconsole-worker-test: vconsole-worker-test.c vconsole-controller-fixture.h $(WORKER_CORE) $(WORKER_HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-worker-test.c $(WORKER_CORE)

$(OUT)/qga9-vconsole-io-test: vconsole-io-test.c vconsole-controller-fixture.h ../kernel/vconsole-io.c ../kernel/vconsole-io.h $(WORKER_CORE) $(WORKER_HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-io-test.c ../kernel/vconsole-io.c $(WORKER_CORE)

$(OUT)/qga9-vconsole-owner-pc-test: ../kernel/vconsole-io.h ../kernel/vconsole-io-pc.h
$(OUT)/qga9-vconsole-owner-pc-test: ../kernel/vconsole-dev.h

DEV_CORE = ../kernel/devvconsole.c ../kernel/vconsole-io-pc.c ../kernel/vconsole-io.c $(WORKER_CORE)
DEV_HEADERS = ../kernel/vconsole-dev.h ../kernel/vconsole-io-pc.h ../kernel/vconsole-io.h ../kernel/vconsole-worker-pc.h ../kernel/vconsole-wake.h $(WORKER_HEADERS)
$(OUT)/qga9-vconsole-checked-irq-test: vconsole-pc-shim.h ../kernel/vconsole-controller-pc.h $(DEV_HEADERS)
$(OUT)/qga9-vconsole-boot-test: $(DEV_HEADERS)
$(OUT)/qga9-vconsole-dev-test: vconsole-dev-test.c vconsole-dev-shim.h vconsole-owner-pc-shim.h vconsole-pc-shim.h vconsole-controller-fixture.h $(DEV_CORE) $(DEV_HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ vconsole-dev-test.c $(DEV_CORE)

test: all
	$(OUT)/qga9-vconsole-checked-irq-test
	$(OUT)/qga9-vconsole-test
	$(OUT)/qga9-vconsole-mmio-test
	$(OUT)/qga9-vconsole-pc-test
	$(OUT)/qga9-vconsole-dma-test
	$(OUT)/qga9-vconsole-irq-test
	$(OUT)/qga9-vconsole-irq-thread-test
	$(OUT)/qga9-vconsole-pump-test
	$(OUT)/qga9-vconsole-stream-test
	$(OUT)/qga9-vconsole-controller-test
	$(OUT)/qga9-vconsole-worker-test
	$(OUT)/qga9-vconsole-io-test
	$(OUT)/qga9-vconsole-dev-test
	$(OUT)/qga9-vconsole-owner-pc-test
	$(OUT)/qga9-vconsole-boot-test
	python3 -m unittest discover -s . -p 'test_*.py' -v

sanitize:
	$(MAKE) OUT=../build/sanitize CFLAGS='-O1 -g -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' all
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-checked-irq-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-mmio-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-pc-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-dma-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-irq-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-irq-thread-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-pump-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-stream-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-controller-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-worker-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-io-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-dev-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-owner-pc-test
	ASAN_OPTIONS=detect_leaks=1 ../build/sanitize/qga9-vconsole-boot-test
	python3 -m unittest discover -s . -p 'test_*.py' -v

analyze: | $(OUT)
	mkdir -p $(OUT)/analyze
	$(CC) $(CPPFLAGS) -DVCONSOLE_CHECKED_IRQ -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-owner-pc.c -o $(OUT)/analyze/vconsole-checked-irq.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole.c -o $(OUT)/analyze/vconsole.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-test.c -o $(OUT)/analyze/vconsole-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-mmio.c -o $(OUT)/analyze/vconsole-mmio.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-mmio-test.c -o $(OUT)/analyze/vconsole-mmio-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-pc.c -o $(OUT)/analyze/vconsole-pc.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-pc-test.c -o $(OUT)/analyze/vconsole-pc-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-dma.c -o $(OUT)/analyze/vconsole-dma.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-dma-test.c -o $(OUT)/analyze/vconsole-dma-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-irq.c -o $(OUT)/analyze/vconsole-irq.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-irq-test.c -o $(OUT)/analyze/vconsole-irq-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-irq-thread-test.c -o $(OUT)/analyze/vconsole-irq-thread-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-pump.c -o $(OUT)/analyze/vconsole-pump.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-pump-test.c -o $(OUT)/analyze/vconsole-pump-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-stream.c -o $(OUT)/analyze/vconsole-stream.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-stream-test.c -o $(OUT)/analyze/vconsole-stream-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-controller.c -o $(OUT)/analyze/vconsole-controller.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-controller-test.c -o $(OUT)/analyze/vconsole-controller-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-worker.c -o $(OUT)/analyze/vconsole-worker.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-io.c -o $(OUT)/analyze/vconsole-io.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c ../kernel/devvconsole.c -o $(OUT)/analyze/devvconsole.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c ../kernel/vconsole-io-pc.c -o $(OUT)/analyze/vconsole-io-pc.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-dev-test.c -o $(OUT)/analyze/vconsole-dev-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-io-test.c -o $(OUT)/analyze/vconsole-io-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-worker-test.c -o $(OUT)/analyze/vconsole-worker-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c ../kernel/vconsole-owner-pc.c -o $(OUT)/analyze/vconsole-owner-pc.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -pthread -c vconsole-owner-pc-test.c -o $(OUT)/analyze/vconsole-owner-pc-test.o
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Werror -fanalyzer -c vconsole-boot-test.c -o $(OUT)/analyze/vconsole-boot-test.o
