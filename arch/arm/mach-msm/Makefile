obj-$(CONFIG_MSM_PROC_COMM) += clock.o

obj-$(CONFIG_MSM_VIC) += irq-vic.o

obj-$(CONFIG_ARCH_MSM7X00A) += irq.o
obj-$(CONFIG_ARCH_QSD8X50) += sirc.o

obj-$(CONFIG_MSM_PROC_COMM) += proc_comm.o clock-pcom.o vreg.o

obj-$(CONFIG_ARCH_MSM7X00A) += dma.o io.o
obj-$(CONFIG_ARCH_MSM7X30) += dma.o io.o
obj-$(CONFIG_ARCH_QSD8X50) += dma.o io.o

obj-$(CONFIG_MSM_SMD) += last_radio_log.o

obj-$(CONFIG_MACH_TROUT) += board-trout.o board-trout-gpio.o board-trout-mmc.o devices-msm7x00.o
obj-$(CONFIG_MACH_TROUT) += board-trout.o board-trout-gpio.o board-trout-mmc.o board-trout-panel.o devices-msm7x00.o
obj-$(CONFIG_MACH_HALIBUT) += board-halibut.o devices-msm7x00.o
obj-$(CONFIG_ARCH_MSM7X30) += gpiomux-7x30.o devices-msm7x30.o
obj-$(CONFIG_ARCH_QSD8X50) += board-qsd8x50.o devices-qsd8x50.o
obj-$(CONFIG_MSM_GPIOMUX) += gpiomux.o
obj-$(CONFIG_ARCH_QSD8X50) += gpiomux-8x50.o

obj-$(CONFIG_MSM_IOMMU) += iommu.o iommu_dev.o iommu_domains.o
obj-y += memory.o clock-voter.o clock-dummy.o
obj-y += subsystem_map.o
obj-$(CONFIG_CPU_FREQ_MSM) += cpufreq.o
obj-$(CONFIG_KEXEC) += msm_kexec.o
obj-$(CONFIG_FORCE_FAST_CHARGE) += fastchg.o

obj-y += acpuclock.o
obj-y += drv_callback.o
obj-$(CONFIG_ARCH_MSM_SCORPION) += pmu.o

obj-$(CONFIG_ARCH_MSM_SCORPION) += msm_fault_handlers.o

obj-$(CONFIG_MSM_PROC_COMM) += mpp.o
ifdef CONFIG_MSM_PROC_COMM
	obj-$(CONFIG_REGULATOR) += footswitch-pcom.o
	obj-$(CONFIG_DEBUG_FS) += pmic_debugfs.o
endif

obj-$(CONFIG_CPU_V7) += idle-v7.o
obj-$(CONFIG_MSM_JTAG_V7) += jtag-v7.o

quiet_cmd_mkrpcsym = MKCAP   $@
      cmd_mkrpcsym = $(PERL) $(srctree)/$(src)/mkrpcsym.pl $< $@

target += smd_rpc_sym.c
$(obj)/smd_rpc_sym.c: $(src)/smd_rpc_sym $(src)/mkrpcsym.pl
	$(call if_changed,mkrpcsym)

obj-$(CONFIG_MSM_FIQ_SUPPORT) += fiq_glue.o
obj-$(CONFIG_MSM_SMD) += remote_spinlock.o socinfo.o
obj-$(CONFIG_MSM_SMD) += pmic.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += rpc_hsusb.o rpc_fsusb.o

obj-$(CONFIG_ARCH_MSM7X30_SMD) += 7x30-smd/
obj-$(CONFIG_ARCH_MSM7X30_SMD) += smd.o

obj-$(CONFIG_MSM_DALRPC) += dal.o
obj-$(CONFIG_ARCH_MSM7X30) += dal_axi.o htc_sdservice.o
obj-$(CONFIG_MSM7KV2_AUDIO) += qdsp5v2/
obj-$(CONFIG_MSM7KV2_AUDIO) += htc_acoustic_7x30.o htc_acdb_7x30.o

obj-$(CONFIG_MSM_IDLE_STATS) += pm-stats.o
ifdef CONFIG_PM
	obj-$(CONFIG_ARCH_MSM7X30) += arch-init-7x30.o
	obj-y += pm-boot.o
endif
obj-y += pwrtest.o

obj-$(CONFIG_MSM_SPM_V1) += spm.o

obj-$(CONFIG_SURF_FFA_GPIO_KEYPAD) += keypad-surf-ffa.o

obj-$(CONFIG_HTC_BATTCHG) += htc_battery.o

obj-$(CONFIG_ARCH_MSM7X30) += devices_htc.o emmc_partitions.o msm_vibrator.o
obj-$(CONFIG_ARCH_MSM7X30) += clock-local.o clock-7x30.o acpuclock-7x30.o htc_awb_cal.o

# MSM7x30 boards
obj-$(CONFIG_ARCH_MSM7X30) += htc_lpm.o
obj-$(CONFIG_MACH_SPADE) += board-spade.o
obj-$(CONFIG_MACH_SPADE) += board-spade-microp.o board-spade-rfkill.o
obj-$(CONFIG_MACH_SPADE) += board-spade-keypad.o board-spade-audio.o
obj-$(CONFIG_MACH_SPADE) += board-spade-mmc.o board-spade-wifi.o
obj-$(CONFIG_MACH_SPADE) += htc_bdaddress.o

obj-$(CONFIG_ARCH_MSM_FLASHLIGHT) += msm_flashlight.o

CFLAGS_msm_vibrator.o += -Idrivers/staging/android

obj-$(CONFIG_HTC_BATTCHG) += htc_battery.o

obj-$(CONFIG_HTC_HEADSET_MGR) += htc_headset_mgr.o
obj-$(CONFIG_HTC_HEADSET_GPIO) += htc_headset_gpio.o
obj-$(CONFIG_HTC_HEADSET_MICROP) += htc_headset_microp.o
obj-$(CONFIG_HTC_HEADSET_PMIC) += htc_headset_pmic.o
ifeq ($(CONFIG_ARCH_MSM7X30),y)
	obj-$(CONFIG_MSM_RMT_STORAGE_CLIENT) += rmt_storage_client-7x30.o
endif

obj-$(CONFIG_MSM_SHOW_RESUME_IRQ) += msm_show_resume_irq.o
obj-$(CONFIG_MSM_RPC_VIBRATOR) += msm_vibrator.o

obj-$(CONFIG_MICROP_COMMON) += atmega_microp_common.o
obj-y += display/
obj-$(CONFIG_MSM_SMD) += smd_debug.o
