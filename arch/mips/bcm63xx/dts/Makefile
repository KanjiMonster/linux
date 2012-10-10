# generic fallback boards
obj-$(CONFIG_BCM63XX_CPU_6328)		+= bcm96328_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6338)		+= bcm96338_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6345)		+= bcm96345_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6348)		+= bcm96348_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6358)		+= bcm96358_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6368)		+= bcm96368_generic.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
