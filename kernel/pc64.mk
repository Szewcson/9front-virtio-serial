# Include from the owning pc64 build directory, with CONF=pc64qga on mk argv.
<mkfile
QGAPURE=vconsole|vconsole-mmio|vconsole-dma|vconsole-irq|vconsole-pump|vconsole-stream|vconsole-controller|vconsole-worker|vconsole-io
QGANATIVE=devvconsole|vconsole-pc|vconsole-wake|vconsole-controller-pc|vconsole-worker-pc|vconsole-owner-pc|vconsole-io-pc|vconsole-boot-pc
^($QGAPURE)\.$O:R: qga9kern/\1.c qga9kern/\1-kernel.c
	$CC $CFLAGS -o $target qga9kern/$stem1-kernel.c
^($QGANATIVE)\.$O:R: qga9kern/\1.c
	$CC $CFLAGS -o $target qga9kern/$stem1.c
