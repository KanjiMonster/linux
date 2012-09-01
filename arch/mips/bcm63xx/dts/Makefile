# generic fallback boards
obj-$(CONFIG_BCM63XX_CPU_6328)		+= bcm96328_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6338)		+= bcm96338_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6345)		+= bcm96345_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6348)		+= bcm96348_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6358)		+= bcm96358_generic.dtb.o
obj-$(CONFIG_BCM63XX_CPU_6368)		+= bcm96368_generic.dtb.o

# board support
obj-$(CONFIG_BOARD_96328AVNG)		+= 96328avng.dtb.o
obj-$(CONFIG_BOARD_96338GW)		+= 96338gw.dtb.o
obj-$(CONFIG_BOARD_96338G)		+= 96338g.dtb.o
obj-$(CONFIG_BOARD_96345GW2)		+= 96345gw2.dtb.o
obj-$(CONFIG_BOARD_96348GW)		+= 96348gw.dtb.o
obj-$(CONFIG_BOARD_96348GW_10)		+= 96348gw_10.dtb.o
obj-$(CONFIG_BOARD_96348GW_11)		+= 96348gw_11.dtb.o
obj-$(CONFIG_BOARD_96348GW_A)		+= 96348gw_a.dtb.o
obj-$(CONFIG_BOARD_96348R)		+= 96348r.dtb.o
obj-$(CONFIG_BOARD_96358VW)		+= 96358vw.dtb.o
obj-$(CONFIG_BOARD_96358VW2)		+= 96358vw2.dtb.o
obj-$(CONFIG_BOARD_AGPF_S0)		+= agpf_s0.dtb.o
obj-$(CONFIG_BOARD_DV201AMR)		+= dv201amr.dtb.o
obj-$(CONFIG_BOARD_DWV_S0)		+= dwv_s0.dtb.o
obj-$(CONFIG_BOARD_FAST2404)		+= fast2404.dtb.o
obj-$(CONFIG_BOARD_RTA1025W_16)		+= rta1025w_16.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
