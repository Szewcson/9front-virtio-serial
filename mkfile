# Native 9front virtio-serial tests and PC64 kernel checks.
</$objtype/mkfile

all:V: qga9-vconsole-test qga9-vconsole-mmio-test qga9-vconsole-dma-test qga9-vconsole-irq-test qga9-vconsole-pump-test qga9-vconsole-stream-test qga9-vconsole-controller-test qga9-vconsole-worker-test qga9-vconsole-io-test

qga9-native-vconsole-access: native-vconsole-access.$O
	$LD -o $target $prereq

install-man:V:
	if(test -e /sys/man/3/vconsole){
		echo 'vconsole manual already exists; preserve it before an explicit update' >[1=2]
		exit exists
	}
	cp man/3/vconsole /sys/man/3/vconsole

native-vconsole-access.$O: tests/native-vconsole-access.c
	$CC $CFLAGS -o $target tests/native-vconsole-access.c

vconsole.$O: kernel/vconsole.c kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole.c

vconsole-test.$O: tests/vconsole-test.c kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-test.c

qga9-vconsole-test: vconsole-test.$O vconsole.$O
	$LD -o $target $prereq

# Compile only: same core against stock PC64 kernel types and declarations.
vconsole-kernel-check.$O: tests/native-vconsole-kernel.c kernel/vconsole.c kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-kernel.c

vconsole-mmio.$O: kernel/vconsole-mmio.c kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-mmio.c

vconsole-mmio-test.$O: tests/vconsole-mmio-test.c kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-mmio-test.c

qga9-vconsole-mmio-test: vconsole-mmio-test.$O vconsole-mmio.$O vconsole.$O
	$LD -o $target $prereq

# PC64 kernel object, compile-only until a real Dev/IRQ/DMA owner is added.
vconsole-pc-check.$O: kernel/vconsole-pc.c kernel/vconsole-pc.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-pc.c

vconsole-mmio-kernel-check.$O: tests/native-vconsole-mmio-kernel.c kernel/vconsole-mmio.c kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-mmio-kernel.c

vconsole-dma.$O: kernel/vconsole-dma.c kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-dma.c

vconsole-dma-test.$O: tests/vconsole-dma-test.c kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-dma-test.c

qga9-vconsole-dma-test: vconsole-dma-test.$O vconsole-dma.$O vconsole-mmio.$O vconsole.$O
	$LD -o $target $prereq

vconsole-dma-kernel-check.$O: tests/native-vconsole-dma-kernel.c kernel/vconsole-dma.c kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-dma-kernel.c

vconsole-irq.$O: kernel/vconsole-irq.c kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-irq.c

vconsole-irq-test.$O: tests/vconsole-irq-test.c kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-irq-test.c

qga9-vconsole-irq-test: vconsole-irq-test.$O vconsole-irq.$O
	$LD -o $target $prereq

vconsole-irq-kernel-check.$O: tests/native-vconsole-irq-kernel.c kernel/vconsole-irq.c kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-irq-kernel.c

vconsole-wake-check.$O: kernel/vconsole-wake.c kernel/vconsole-wake.h kernel/vconsole-irq.h kernel/vconsole-mmio.h kernel/vconsole.h /$objtype/include/ureg.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-wake.c

vconsole-pump.$O: kernel/vconsole-pump.c kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-pump.c

vconsole-pump-test.$O: tests/vconsole-pump-test.c tests/vconsole-fixture.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-pump-test.c

qga9-vconsole-pump-test: vconsole-pump-test.$O vconsole-pump.$O vconsole-dma.$O vconsole-mmio.$O vconsole.$O
	$LD -o $target $prereq

vconsole-pump-kernel-check.$O: tests/native-vconsole-pump-kernel.c kernel/vconsole-pump.c kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-pump-kernel.c

vconsole-stream.$O: kernel/vconsole-stream.c kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-stream.c

vconsole-stream-test.$O: tests/vconsole-stream-test.c tests/vconsole-fixture.h kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-stream-test.c

qga9-vconsole-stream-test: vconsole-stream-test.$O vconsole-stream.$O vconsole-pump.$O vconsole-dma.$O vconsole-mmio.$O vconsole.$O
	$LD -o $target $prereq

vconsole-stream-kernel-check.$O: tests/native-vconsole-stream-kernel.c kernel/vconsole-stream.c kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-stream-kernel.c

vconsole-controller.$O: kernel/vconsole-controller.c kernel/vconsole-controller.h kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-controller.c

vconsole-controller-test.$O: tests/vconsole-controller-test.c tests/vconsole-controller-fixture.h kernel/vconsole-controller.h kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-controller-test.c

vconsole-io.$O: kernel/vconsole-io.c kernel/vconsole-io.h kernel/vconsole-controller.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-io.c

vconsole-io-test.$O: tests/vconsole-io-test.c tests/vconsole-controller-fixture.h kernel/vconsole-io.h kernel/vconsole-controller.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-io-test.c

qga9-vconsole-io-test: vconsole-io-test.$O vconsole-io.$O vconsole-worker.$O vconsole-controller.$O vconsole-stream.$O vconsole-pump.$O vconsole-dma.$O vconsole-mmio.$O vconsole-irq.$O vconsole.$O
	$LD -o $target $prereq

vconsole-io-kernel-check.$O: tests/native-vconsole-io-kernel.c kernel/vconsole-io.c kernel/vconsole-io.h kernel/vconsole-controller.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-io-kernel.c

vconsole-io-pc-check.$O: kernel/vconsole-io-pc.c kernel/vconsole-io-pc.h kernel/vconsole-io.h kernel/vconsole-controller.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-io-pc.c

vconsole-dev-check.$O: kernel/devvconsole.c kernel/vconsole-dev.h kernel/vconsole-worker-pc.h kernel/vconsole-io-pc.h kernel/vconsole-io.h kernel/vconsole-controller.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/devvconsole.c

qga9-vconsole-controller-test: vconsole-controller-test.$O vconsole-controller.$O vconsole-stream.$O vconsole-pump.$O vconsole-dma.$O vconsole-mmio.$O vconsole-irq.$O vconsole.$O
	$LD -o $target $prereq

vconsole-controller-kernel-check.$O: tests/native-vconsole-controller-kernel.c kernel/vconsole-controller.c kernel/vconsole-controller.h kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-controller-kernel.c

vconsole-controller-pc-check.$O: kernel/vconsole-controller-pc.c kernel/vconsole-controller-pc.h kernel/vconsole-controller.h kernel/vconsole-stream.h kernel/vconsole-pump.h kernel/vconsole-dma.h kernel/vconsole-mmio.h kernel/vconsole-irq.h kernel/vconsole.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-controller-pc.c

vconsole-worker.$O: kernel/vconsole-worker.c kernel/vconsole-worker.h kernel/vconsole-controller.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target kernel/vconsole-worker.c

vconsole-worker-test.$O: tests/vconsole-worker-test.c tests/vconsole-controller-fixture.h kernel/vconsole-worker.h kernel/vconsole-controller.h
	$CC $CFLAGS -DVCONSOLE_USER_TEST -Ikernel -o $target tests/vconsole-worker-test.c

qga9-vconsole-worker-test: vconsole-worker-test.$O vconsole-worker.$O vconsole-controller.$O vconsole-stream.$O vconsole-pump.$O vconsole-dma.$O vconsole-mmio.$O vconsole-irq.$O vconsole.$O
	$LD -o $target $prereq

vconsole-worker-kernel-check.$O: tests/native-vconsole-worker-kernel.c kernel/vconsole-worker.c kernel/vconsole-worker.h kernel/vconsole-controller.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target tests/native-vconsole-worker-kernel.c

vconsole-worker-pc-check.$O: kernel/vconsole-worker-pc.c kernel/vconsole-worker-pc.h kernel/vconsole-worker.h kernel/vconsole-controller.h kernel/vconsole-wake.h kernel/vconsole-io-pc.h kernel/vconsole-io.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-worker-pc.c

vconsole-owner-pc-check.$O: kernel/vconsole-owner-pc.c kernel/vconsole-owner-pc.h kernel/vconsole-pc.h kernel/vconsole-controller.h kernel/vconsole-wake.h kernel/vconsole-worker-pc.h kernel/vconsole-worker.h kernel/vconsole-controller-pc.h kernel/vconsole-io-pc.h kernel/vconsole-io.h kernel/vconsole-dev.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-owner-pc.c

vconsole-boot-pc-check.$O: kernel/vconsole-boot-pc.c kernel/vconsole-owner-pc.h kernel/vconsole-dev.h
	$CC $CFLAGS -Ikernel -I/sys/src/9/pc64 -o $target kernel/vconsole-boot-pc.c

kernel-check:V: vconsole-kernel-check.$O vconsole-pc-check.$O vconsole-mmio-kernel-check.$O vconsole-dma-kernel-check.$O vconsole-irq-kernel-check.$O vconsole-wake-check.$O vconsole-pump-kernel-check.$O vconsole-stream-kernel-check.$O vconsole-io-kernel-check.$O vconsole-io-pc-check.$O vconsole-dev-check.$O vconsole-controller-kernel-check.$O vconsole-controller-pc-check.$O vconsole-worker-kernel-check.$O vconsole-worker-pc-check.$O vconsole-owner-pc-check.$O vconsole-boot-pc-check.$O

kernel-build:V:
	rc kernel/build-pc64.rc

test:V: qga9-vconsole-test qga9-vconsole-mmio-test qga9-vconsole-dma-test qga9-vconsole-irq-test qga9-vconsole-pump-test qga9-vconsole-stream-test qga9-vconsole-controller-test qga9-vconsole-worker-test qga9-vconsole-io-test
	./qga9-vconsole-test
	./qga9-vconsole-mmio-test
	./qga9-vconsole-dma-test
	./qga9-vconsole-irq-test
	./qga9-vconsole-pump-test
	./qga9-vconsole-stream-test
	./qga9-vconsole-controller-test
	./qga9-vconsole-worker-test
	./qga9-vconsole-io-test

clean:V:
	rm -f native-vconsole-access.$O vconsole.$O vconsole-test.$O vconsole-kernel-check.$O vconsole-mmio.$O vconsole-mmio-test.$O vconsole-pc-check.$O vconsole-mmio-kernel-check.$O vconsole-dma.$O vconsole-dma-test.$O vconsole-dma-kernel-check.$O vconsole-irq.$O vconsole-irq-test.$O vconsole-irq-kernel-check.$O vconsole-wake-check.$O vconsole-pump.$O vconsole-pump-test.$O vconsole-pump-kernel-check.$O vconsole-stream.$O vconsole-stream-test.$O vconsole-stream-kernel-check.$O vconsole-controller.$O vconsole-controller-test.$O vconsole-io.$O vconsole-io-test.$O vconsole-io-kernel-check.$O vconsole-io-pc-check.$O vconsole-dev-check.$O vconsole-controller-kernel-check.$O vconsole-controller-pc-check.$O vconsole-worker.$O vconsole-worker-test.$O vconsole-worker-kernel-check.$O vconsole-worker-pc-check.$O vconsole-owner-pc-check.$O vconsole-boot-pc-check.$O qga9-vconsole-test qga9-vconsole-mmio-test qga9-vconsole-dma-test qga9-vconsole-irq-test qga9-vconsole-pump-test qga9-vconsole-stream-test qga9-vconsole-controller-test qga9-vconsole-worker-test qga9-vconsole-io-test qga9-native-vconsole-access
