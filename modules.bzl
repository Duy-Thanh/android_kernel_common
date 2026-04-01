# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 The Android Open Source Project

"""
This module contains a full list of kernel modules
 compiled by GKI.
"""

_COMMON_GKI_MODULES_LIST = [
    # keep sorted
    "crypto/twofish_common.ko",
    "crypto/twofish_generic.ko",
    "drivers/block/virtio_blk.ko",
    "drivers/block/zram/zram.ko",
    "drivers/bluetooth/btbcm.ko",
    "drivers/bluetooth/btintel.ko",
    "drivers/bluetooth/btqca.ko",
    "drivers/bluetooth/btrtl.ko",
    "drivers/bluetooth/btsdio.ko",
    "drivers/bluetooth/btusb.ko",
    "drivers/bluetooth/hci_uart.ko",
    "drivers/bus/tegra-aconnect.ko",
    "drivers/char/virtio_console.ko",
    "drivers/cpufreq/cpufreq_userspace.ko",
    "drivers/cpufreq/tegra194-cpufreq.ko",
    "drivers/dma/tegra186-gpc-dma.ko",
    "drivers/dma/tegra20-apb-dma.ko",
    "drivers/dma/tegra210-adma.ko",
    "drivers/extcon/extcon-usb-gpio.ko",
    "drivers/firmware/qcom/qcom-scm.ko",
    "drivers/firmware/qcom/qcom_tzmem.ko",
    "drivers/gnss/gnss.ko",
    "drivers/gpu/drm/display/drm_display_helper.ko",
    "drivers/gpu/drm/display/drm_dp_aux_bus.ko",
    "drivers/gpu/drm/tegra/tegra-drm.ko",
    "drivers/gpu/drm/tiny/simpledrm.ko",
    "drivers/gpu/host1x/host1x.ko",
    "drivers/hid/hid-gfrm.ko",
    "drivers/hwmon/hwmon.ko",
    "drivers/hwmon/pwm-fan.ko",
    "drivers/i2c/busses/i2c-tegra-bpmp.ko",
    "drivers/i2c/busses/i2c-tegra.ko",
    "drivers/i2c/i2c-dev.ko",
    "drivers/i2c/i2c-mux.ko",
    "drivers/i2c/muxes/i2c-mux-gpio.ko",
    "drivers/i2c/muxes/i2c-mux-pca954x.ko",
    "drivers/iommu/arm/arm-smmu-v3/arm_smmu_v3.ko",
    "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
    "drivers/media/usb/uvc/uvcvideo.ko",
    "drivers/misc/vcpu_stall_detector.ko",
    "drivers/mmc/host/cqhci.ko",
    "drivers/mmc/host/sdhci-tegra.ko",
    "drivers/mtd/chips/chipreg.ko",
    "drivers/mtd/mtd.ko",
    "drivers/mtd/spi-nor/spi-nor.ko",
    "drivers/net/can/dev/can-dev.ko",
    "drivers/net/can/slcan/slcan.ko",
    "drivers/net/can/vcan.ko",
    "drivers/net/macsec.ko",
    "drivers/net/mii.ko",
    "drivers/net/ppp/bsd_comp.ko",
    "drivers/net/ppp/ppp_deflate.ko",
    "drivers/net/ppp/ppp_generic.ko",
    "drivers/net/ppp/ppp_mppe.ko",
    "drivers/net/ppp/pppox.ko",
    "drivers/net/ppp/pptp.ko",
    "drivers/net/slip/slhc.ko",
    "drivers/net/usb/aqc111.ko",
    "drivers/net/usb/asix.ko",
    "drivers/net/usb/ax88179_178a.ko",
    "drivers/net/usb/cdc_eem.ko",
    "drivers/net/usb/cdc_ether.ko",
    "drivers/net/usb/cdc_ncm.ko",
    "drivers/net/phy/microchip.ko",
    "drivers/net/phy/smsc.ko",
    "drivers/net/usb/lan78xx.ko",
    "drivers/net/usb/r8152.ko",
    "drivers/net/usb/r8153_ecm.ko",
    "drivers/net/usb/rtl8150.ko",
    "drivers/net/usb/smsc95xx.ko",
    "drivers/net/usb/usbnet.ko",
    "drivers/net/wwan/wwan.ko",
    "drivers/pci/controller/dwc/pcie-tegra194.ko",
    "drivers/phy/tegra/phy-tegra-xusb.ko",
    "drivers/phy/tegra/phy-tegra194-p2u.ko",
    "drivers/power/sequencing/pwrseq-core.ko",
    "drivers/pps/pps_core.ko",
    "drivers/ptp/ptp.ko",
    "drivers/pwm/pwm-tegra.ko",
    "drivers/rtc/rtc-tegra.ko",
    "drivers/spi/spi-tegra114.ko",
    "drivers/spi/spidev.ko",
    "drivers/tee/optee/optee.ko",
    "drivers/tee/tee.ko",
    "drivers/thermal/tegra/tegra-bpmp-thermal.ko",
    "drivers/usb/class/cdc-acm.ko",
    "drivers/usb/gadget/function/usb_f_ecm_subset.ko",
    "drivers/usb/gadget/function/usb_f_rndis.ko",
    "drivers/usb/gadget/legacy/g_ether.ko",
    "drivers/usb/gadget/udc/tegra-xudc.ko",
    "drivers/usb/common/usb-conn-gpio.ko",
    "drivers/usb/host/ohci-hcd.ko",
    "drivers/usb/host/ohci-platform.ko",
    "drivers/usb/host/xhci-tegra.ko",
    "drivers/usb/mon/usbmon.ko",
    "drivers/usb/serial/ch341.ko",
    "drivers/usb/serial/cp210x.ko",
    "drivers/usb/serial/ftdi_sio.ko",
    "drivers/usb/serial/garmin_gps.ko",
    "drivers/usb/serial/option.ko",
    "drivers/usb/serial/pl2303.ko",
    "drivers/usb/serial/usb_wwan.ko",
    "drivers/usb/serial/usbserial.ko",
    "drivers/usb/serial/xsens_mt.ko",
    "drivers/usb/storage/ums-cypress.ko",
    "drivers/usb/storage/ums-sddr09.ko",
    "drivers/usb/storage/ums-usbat.ko",
    "drivers/usb/typec/ucsi/typec_ucsi.ko",
    "drivers/usb/typec/ucsi/ucsi_ccg.ko",
    "drivers/video/backlight/pwm_bl.ko",
    "drivers/virtio/virtio_balloon.ko",
    "drivers/virtio/virtio_pci.ko",
    "drivers/virtio/virtio_pci_legacy_dev.ko",
    "drivers/virtio/virtio_pci_modern_dev.ko",
    "fs/efivarfs/efivarfs.ko",
    "fs/netfs/netfs.ko",
    "fs/nls/nls_utf8.ko",
    "fs/pstore/pstore_blk.ko",
    "fs/pstore/pstore_zone.ko",
    "kernel/kheaders.ko",
    "lib/crypto/libarc4.ko",
    "mm/zsmalloc.ko",
    "net/6lowpan/6lowpan.ko",
    "net/6lowpan/nhc_dest.ko",
    "net/6lowpan/nhc_fragment.ko",
    "net/6lowpan/nhc_hop.ko",
    "net/6lowpan/nhc_ipv6.ko",
    "net/6lowpan/nhc_mobility.ko",
    "net/6lowpan/nhc_routing.ko",
    "net/6lowpan/nhc_udp.ko",
    "net/8021q/8021q.ko",
    "net/9p/9pnet.ko",
    "net/9p/9pnet_fd.ko",
    "net/bluetooth/bluetooth.ko",
    "net/bluetooth/bnep/bnep.ko",
    "net/bluetooth/hidp/hidp.ko",
    "net/bluetooth/rfcomm/rfcomm.ko",
    "net/can/can.ko",
    "net/can/can-bcm.ko",
    "net/can/can-gw.ko",
    "net/can/can-raw.ko",
    "net/dns_resolver/dns_resolver.ko",
    "net/ieee802154/6lowpan/ieee802154_6lowpan.ko",
    "net/ieee802154/ieee802154.ko",
    "net/ieee802154/ieee802154_socket.ko",
    "net/l2tp/l2tp_core.ko",
    "net/l2tp/l2tp_ppp.ko",
    "net/mac80211/mac80211.ko",
    "net/mac802154/mac802154.ko",
    "net/nfc/nfc.ko",
    "net/rfkill/rfkill.ko",
    "net/tipc/tipc.ko",
    "net/tipc/tipc_diag.ko",
    "net/vmw_vsock/vmw_vsock_virtio_transport.ko",
    "net/wireless/cfg80211.ko",
    "sound/hda/snd-hda-core.ko",
    "sound/pci/hda/snd-hda-codec-hdmi.ko",
    "sound/pci/hda/snd-hda-codec.ko",
    "sound/pci/hda/snd-hda-tegra.ko",
    "sound/soc/codecs/snd-soc-rl6231.ko",
    "sound/soc/codecs/snd-soc-rt5640.ko",
    "sound/soc/generic/snd-soc-audio-graph-card.ko",
    "sound/soc/generic/snd-soc-simple-card-utils.ko",
    "sound/soc/generic/snd-soc-simple-card.ko",
    "sound/soc/tegra/snd-soc-tegra-audio-graph-card.ko",
    "sound/soc/tegra/snd-soc-tegra-pcm.ko",
    "sound/soc/tegra/snd-soc-tegra186-asrc.ko",
    "sound/soc/tegra/snd-soc-tegra186-dspk.ko",
    "sound/soc/tegra/snd-soc-tegra210-admaif.ko",
    "sound/soc/tegra/snd-soc-tegra210-adx.ko",
    "sound/soc/tegra/snd-soc-tegra210-ahub.ko",
    "sound/soc/tegra/snd-soc-tegra210-amx.ko",
    "sound/soc/tegra/snd-soc-tegra210-dmic.ko",
    "sound/soc/tegra/snd-soc-tegra210-i2s.ko",
    "sound/soc/tegra/snd-soc-tegra210-mixer.ko",
    "sound/soc/tegra/snd-soc-tegra210-mvc.ko",
    "sound/soc/tegra/snd-soc-tegra210-ope.ko",
    "sound/soc/tegra/snd-soc-tegra210-sfc.ko",
]

# Deprecated - Use `get_gki_modules_list` function instead.
COMMON_GKI_MODULES_LIST = _COMMON_GKI_MODULES_LIST

_ARM_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
    "arch/arm64/crypto/aes-ce-blk.ko",
    "arch/arm64/crypto/aes-ce-cipher.ko",
    "arch/arm64/crypto/aes-neon-blk.ko",
    "arch/arm64/crypto/ghash-ce.ko",
    "arch/arm64/crypto/sha1-ce.ko",
    "arch/arm64/crypto/sha2-ce.ko",
    "arch/arm64/crypto/sha256-arm64.ko",
    "arch/arm64/geniezone/gzvm.ko",
    "drivers/android/binder/rust_binder.ko",
    "drivers/char/hw_random/cctrng.ko",
    "drivers/misc/open-dice.ko",
    "drivers/ptp/ptp_kvm.ko",
    "drivers/virt/halla/exynos-hvm.ko",
]

_X86_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/android/binder/rust_binder.ko",
    "drivers/ptp/ptp_kvm.ko",
]

# buildifier: disable=unnamed-macro
def get_gki_modules_list(arch = None):
    """ Provides the list of GKI modules.

    Args:
      arch: One of [arm, arm64, i386, x86_64].

    Returns:
      The list of GKI modules for the given |arch|.
    """
    gki_modules_list = [] + _COMMON_GKI_MODULES_LIST
    if arch == "arm":
        gki_modules_list += _ARM_GKI_MODULES_LIST
    elif arch == "arm64":
        gki_modules_list += _ARM64_GKI_MODULES_LIST
    elif arch == "i386":
        gki_modules_list += _X86_GKI_MODULES_LIST
    elif arch == "x86_64":
        gki_modules_list += _X86_64_GKI_MODULES_LIST
    else:
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    return gki_modules_list

_KUNIT_FRAMEWORK_MODULES = [
    "lib/kunit/kunit.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_defconfig
_KUNIT_COMMON_MODULES_LIST = [
    # keep sorted
    "drivers/base/regmap/regmap-kunit.ko",
    "drivers/base/regmap/regmap-ram.ko",
    "drivers/base/regmap/regmap-raw-ram.ko",
    "drivers/hid/hid-uclogic-test.ko",
    "drivers/iio/test/iio-test-format.ko",
    "drivers/input/tests/input_test.ko",
    "drivers/of/of_kunit_helpers.ko",
    "drivers/rtc/lib_test.ko",
    "fs/ext4/ext4-inode-test.ko",
    "fs/fat/fat_test.ko",
    "kernel/time/time_test.ko",
    "lib/kunit/kunit-example-test.ko",
    "lib/kunit/kunit-test.ko",
    "lib/kunit/platform-test.ko",
    # "mm/kfence/kfence_test.ko",
    "net/core/dev_addr_lists_test.ko",
    "sound/soc/soc-topology-test.ko",
    "sound/soc/soc-utils-test.ko",
]

# Modules defined by tools/testing/kunit/configs/android/kunit_clk_defconfig
_KUNIT_CLK_MODULES_LIST = [
    "drivers/clk/clk-gate_test.ko",
    "drivers/clk/clk-test.ko",
    "drivers/clk/clk_kunit_helpers.ko",
]

# buildifier: disable=unnamed-macro
def get_kunit_modules_list(arch = None):
    """ Provides the list of GKI modules.

    Args:
      arch: One of [arm, arm64, i386, x86_64].

    Returns:
      The list of KUnit modules for the given |arch|.
    """
    kunit_modules_list = _KUNIT_FRAMEWORK_MODULES + _KUNIT_COMMON_MODULES_LIST
    if arch == "arm":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "arm64":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "i386":
        kunit_modules_list += []
    elif arch == "x86_64":
        kunit_modules_list += []
    else:
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    return kunit_modules_list

_COMMON_UNPROTECTED_MODULES_LIST = [
    "drivers/block/zram/zram.ko",
    "mm/zsmalloc.ko",
]

# buildifier: disable=unnamed-macro
def get_gki_protected_modules_list(arch = None):
    all_gki_modules = get_gki_modules_list(arch) + get_kunit_modules_list(arch)
    unprotected_modules = _COMMON_UNPROTECTED_MODULES_LIST
    protected_modules = [mod for mod in all_gki_modules if mod not in unprotected_modules]
    return sorted(protected_modules)
