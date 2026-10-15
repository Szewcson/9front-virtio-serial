# Include from pc64/mkfile after the native port/pc build rules.
# VCPORT/VCPC may be overridden on mk's command line for build-only staging.
VCPORT=../port
VCPC=../pc
VCIRQFLAGS=
VCPORTMODULES=devvconsole|vconsole|vconsole-mmio|vconsole-dma|vconsole-irq|vconsole-pump|vconsole-stream|vconsole-controller|vconsole-worker|vconsole-io
VCPCMODULES=vconsole-pc|vconsole-wake|vconsole-controller-pc|vconsole-worker-pc|vconsole-owner-pc|vconsole-io-pc|vconsole-boot-pc
VCHFILES=$VCPORT/vconsole.h $VCPORT/vconsole-mmio.h $VCPORT/vconsole-dma.h $VCPORT/vconsole-irq.h $VCPORT/vconsole-pump.h $VCPORT/vconsole-stream.h $VCPORT/vconsole-controller.h $VCPORT/vconsole-worker.h $VCPORT/vconsole-io.h $VCPORT/vconsole-pc.h $VCPORT/vconsole-wake.h $VCPORT/vconsole-controller-pc.h $VCPORT/vconsole-worker-pc.h $VCPORT/vconsole-owner-pc.h $VCPORT/vconsole-io-pc.h $VCPORT/vconsole-dev.h

^($VCPORTMODULES)\.$O:R: $VCPORT/\1.c $VCHFILES ../port/virtio10.h /$objtype/include/ureg.h
	$CC $CFLAGS $VCIRQFLAGS -I. -I$VCPORT -o $target $VCPORT/$stem1.c

^($VCPCMODULES)\.$O:R: $VCPC/\1.c $VCHFILES ../port/virtio10.h /$objtype/include/ureg.h
	$CC $CFLAGS $VCIRQFLAGS -I. -I$VCPORT -o $target $VCPC/$stem1.c
