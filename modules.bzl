# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 The Android Open Source Project

"""
This module contains a full list of kernel modules
 compiled by GKI.
"""

_COMMON_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/block/virtio_blk.ko",
    "drivers/block/zram/zram.ko",
    "drivers/bluetooth/btbcm.ko",
    "drivers/bluetooth/btqca.ko",
    "drivers/bluetooth/btsdio.ko",
    "drivers/bluetooth/hci_uart.ko",
    "drivers/char/virtio_console.ko",
    "drivers/misc/vcpu_stall_detector.ko",
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
    "drivers/net/usb/r8152.ko",
    "drivers/net/usb/r8153_ecm.ko",
    "drivers/net/usb/rtl8150.ko",
    "drivers/net/usb/usbnet.ko",
    "drivers/net/wwan/wwan.ko",
    "drivers/pps/pps_core.ko",
    "drivers/ptp/ptp.ko",
    "drivers/usb/class/cdc-acm.ko",
    "drivers/usb/mon/usbmon.ko",
    "drivers/usb/serial/ftdi_sio.ko",
    "drivers/usb/serial/usbserial.ko",
    "drivers/virtio/virtio_balloon.ko",
    "drivers/virtio/virtio_pci.ko",
    "drivers/virtio/virtio_pci_legacy_dev.ko",
    "drivers/virtio/virtio_pci_modern_dev.ko",
    "kernel/kheaders.ko",
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
    "net/bluetooth/hidp/hidp.ko",
    "net/bluetooth/rfcomm/rfcomm.ko",
    "net/can/can.ko",
    "net/can/can-bcm.ko",
    "net/can/can-gw.ko",
    "net/can/can-raw.ko",
    "net/ieee802154/6lowpan/ieee802154_6lowpan.ko",
    "net/ieee802154/ieee802154.ko",
    "net/ieee802154/ieee802154_socket.ko",
    "net/l2tp/l2tp_core.ko",
    "net/l2tp/l2tp_ppp.ko",
    "net/mac802154/mac802154.ko",
    "net/nfc/nfc.ko",
    "net/rfkill/rfkill.ko",
    "net/tipc/diag.ko",
    "net/tipc/tipc.ko",
    "net/tls/tls.ko",
    "net/vmw_vsock/vmw_vsock_virtio_transport.ko",
]

# Deprecated - Use `get_gki_modules_list` function instead.
COMMON_GKI_MODULES_LIST = _COMMON_GKI_MODULES_LIST

_ARM_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
    "arch/arm64/geniezone/gzvm.ko",
    "drivers/char/hw_random/cctrng.ko",
    "drivers/misc/open-dice.ko",
    "drivers/ptp/ptp_kvm.ko",
    "drivers/usb/serial/oti6858.ko",
    "drivers/usb/serial/option.ko",
    "drivers/iio/industrialio-sw-trigger.ko",
    "drivers/net/wireless/legacy/rndis_wlan.ko",
    "sound/core/seq/snd-seq.ko",
    "drivers/hid/hid-vivaldi.ko",
    "net/mac80211/mac80211.ko",
    "drivers/watchdog/softdog.ko",
    "drivers/media/platform/mediatek/mdp3/mtk-mdp3.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650.ko",
    "drivers/hid/hid-led.ko",
    "drivers/media/platform/mediatek/jpeg/mtk-jpeg-dec-hw.ko",
    "drivers/iio/buffer/industrialio-triggered-buffer.ko",
    "drivers/net/ppp/ppp_async.ko",
    "drivers/usb/serial/usb_wwan.ko",
    "drivers/cpufreq/mediatek-cpufreq-hw.ko",
    "drivers/input/touchscreen/usbtouchscreen.ko",
    "drivers/net/usb/pegasus.ko",
    "drivers/iio/trigger/iio-trig-hrtimer.ko",
    "drivers/media/platform/mediatek/vcodec/decoder/mtk-vcodec-dec-hw.ko",
    "sound/core/seq/snd-seq-dummy.ko",
    "drivers/thermal/mediatek/soc_temp_lvts.ko",
    "drivers/i2c/i2c-stub.ko",
    "drivers/hid/hid-holtekff.ko",
    "drivers/usb/serial/ch341.ko",
    "drivers/input/rmi4/rmi_core.ko",
    "drivers/net/usb/dm9601.ko",
    "drivers/net/usb/rndis_host.ko",
    "drivers/cdrom/cdrom.ko",
    "drivers/hid/hid-vivaldi-common.ko",
    "drivers/media/i2c/ov8856.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors_sync.ko",
    "drivers/bluetooth/btusb.ko",
    "crypto/async_tx/async_tx.ko",
    "drivers/media/v4l2-core/v4l2-vp9.ko",
    "drivers/input/joystick/iforce/iforce.ko",
    "drivers/usb/misc/ezusb.ko",
    "drivers/iio/proximity/sx9324.ko",
    "drivers/hid/hid-primax.ko",
    "sound/soc/codecs/snd-soc-tas2781-fmwlib.ko",
    "drivers/net/usb/smsc95xx.ko",
    "drivers/platform/chrome/cros-ec-sensorhub.ko",
    "drivers/usb/serial/cp210x.ko",
    "drivers/gpu/drm/evdi/evdi.ko",
    "sound/soc/sof/snd-sof-of.ko",
    "sound/soc/codecs/snd-soc-rt5677.ko",
    "sound/soc/mediatek/mt8188/mt8188-mt6359.ko",
    "drivers/media/i2c/ov02a10.ko",
    "drivers/input/joydev.ko",
    "drivers/usb/serial/qcserial.ko",
    "sound/soc/codecs/snd-soc-tas2781-i2c.ko",
    "drivers/platform/chrome/cros_ec_rpmsg.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650-rt5514.ko",
    "drivers/usb/gadget/udc/dummy_hcd.ko",
    "sound/soc/codecs/snd-soc-tas2781-comlib.ko",
    "sound/core/seq/snd-seq-midi-event.ko",
    "drivers/md/dm-flakey.ko",
    "sound/soc/sof/mediatek/mt8186/snd-sof-mt8186.ko",
    "drivers/media/platform/mediatek/jpeg/mtk_jpeg.ko",
    "fs/lockd/lockd.ko",
    "drivers/bluetooth/btmtksdio.ko",
    "sound/soc/codecs/snd-soc-rt5514.ko",
    "drivers/iio/buffer/kfifo_buf.ko",
    "crypto/lz4hc.ko",
    "drivers/iio/proximity/sx_common.ko",
    "drivers/usb/serial/pl2303.ko",
    "drivers/iio/light/cros_ec_light_prox.ko",
    "drivers/net/usb/cdc_mbim.ko",
    "sound/soc/sof/snd-sof-utils.ko",
    "lib/crc7.ko",
    "sound/soc/codecs/snd-soc-nau8825.ko",
    "drivers/bluetooth/btmrvl.ko",
    "arch/arm64/lib/xor-neon.ko",
    "drivers/media/platform/mediatek/vcodec/common/mtk-vcodec-dbgfs.ko",
    "drivers/usb/serial/usb-serial-simple.ko",
    "fs/udf/udf.ko",
    "drivers/input/joystick/iforce/iforce-usb.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_lid_angle.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors_core.ko",
    "fs/hfsplus/hfsplus.ko",
    "drivers/net/phy/smsc.ko",
    "fs/isofs/isofs.ko",
    "sound/soc/sof/mediatek/mtk-adsp-common.ko",
    "drivers/gpu/drm/panel/panel-ilitek-ili9882t.ko",
    "drivers/bluetooth/btrtl.ko",
    "drivers/remoteproc/mtk_scp_ipi.ko",
    "drivers/hid/hid-holtek-kbd.ko",
    "drivers/bluetooth/btmrvl_sdio.ko",
    "drivers/md/dm-integrity.ko",
    "crypto/async_tx/async_xor.ko",
    "drivers/hid/hid-holtek-mouse.ko",
    "fs/nfs/nfsv3.ko",
    "drivers/media/platform/mediatek/jpeg/mtk-jpeg-enc-hw.ko",
    "drivers/platform/chrome/cros_hps_i2c.ko",
    "drivers/mmc/core/mmc_test.ko",
    "drivers/hid/hid-google-hammer.ko",
    "drivers/input/serio/serport.ko",
    "drivers/usb/serial/keyspan.ko",
    "drivers/media/platform/mediatek/vcodec/decoder/mtk-vcodec-dec.ko",
    "crypto/xor.ko",
    "drivers/usb/class/cdc-wdm.ko",
    "net/ipv6/sit.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_activity.ko",
    "drivers/gpu/drm/panel/panel-himax-hx83102.ko",
    "drivers/hid/hid-quickstep.ko",
    "drivers/iio/industrialio-configfs.ko",
    "drivers/hid/hid-chicony.ko",
    "drivers/net/usb/mcs7830.ko",
    "drivers/iio/trigger/iio-trig-sysfs.ko",
    "drivers/media/platform/mediatek/vcodec/common/mtk-vcodec-common.ko",
    "sound/soc/mediatek/mt8173/mt8173-afe-pcm.ko",
    "drivers/media/platform/mediatek/mdp/mtk-mdp.ko",
    "fs/nfs_common/grace.ko",
    "drivers/input/serio/serio.ko",
    "drivers/media/platform/mediatek/vcodec/encoder/mtk-vcodec-enc.ko",
    "drivers/hid/hid-cherry.ko",
    "drivers/net/usb/ipheth.ko",
    "sound/soc/sof/xtensa/snd-sof-xtensa-dsp.ko",
    "drivers/scsi/sr_mod.ko",
    "drivers/bluetooth/btintel.ko",
    "sound/core/seq/snd-seq-midi.ko",
    "drivers/net/usb/smsc75xx.ko",
    "net/wireless/cfg80211.ko",
    "fs/nfs/nfsv2.ko",
    "sound/soc/sof/snd-sof.ko",
    "drivers/thermal/mediatek/soc_temp_lvts_mt8192.ko",
    "drivers/usb/usbip/usbip-core.ko",
    "net/dns_resolver/dns_resolver.ko",
    "drivers/bluetooth/bfusb.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650-rt5676.ko",
    "drivers/bluetooth/btmtk.ko",
    "drivers/media/v4l2-core/v4l2-h264.ko",
    "drivers/usb/usbip/vhci-hcd.ko",
    "lib/crc-itu-t.ko",
    "net/netfilter/xt_cgroup.ko",
    "net/ipv6/ah6.ko",
    "drivers/media/i2c/ov5695.ko",
    "sound/soc/codecs/snd-soc-rt5677-spi.ko",
    "drivers/base/test/test_async_driver_probe.ko",
    "drivers/usb/serial/sierra.ko",
    "drivers/bluetooth/hci_vhci.ko",
    "drivers/media/i2c/dw9768.ko",
    "net/sunrpc/sunrpc.ko",
    "fs/nfs/nfs.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors.ko",
    "drivers/remoteproc/mtk_scp.ko",
    "drivers/iio/pressure/cros_ec_baro.ko",
    "fs/nfs/nfsv4.ko",
    "drivers/soc/mediatek/mtk-svs.ko",
    "drivers/hid/hid-rmi.ko",
    "net/ipv4/tcp_lp.ko",
    "drivers/hid/hid-kensington.ko",
    "drivers/firmware/google/vpd-sysfs.ko",
    "drivers/rpmsg/mtk_rpmsg.ko",
    "drivers/usb/gadget/legacy/g_mass_storage.ko",
]

_X86_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/acpi/fan.ko",
    "drivers/powercap/intel_rapl_common.ko",
    "drivers/ptp/ptp_kvm.ko",
    "drivers/thermal/intel/int340x_thermal/acpi_thermal_rel.ko",
    "drivers/thermal/intel/int340x_thermal/int3400_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3401_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3402_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3403_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int340x_thermal_zone.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci_legacy.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_mbox.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rapl.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rfim.ko",
    "drivers/thermal/intel/intel_soc_dts_iosf.ko",
    "drivers/thermal/intel/intel_soc_dts_thermal.ko",
    "drivers/thunderbolt/thunderbolt.ko",
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

# Common Kunit test modules
_KUNIT_COMMON_MODULES_LIST = [
    # keep sorted
    "drivers/base/regmap/regmap-kunit.ko",
    "drivers/base/regmap/regmap-ram.ko",
    "drivers/base/regmap/regmap-raw-ram.ko",
    "drivers/hid/hid-uclogic-test.ko",
    "drivers/iio/test/iio-test-format.ko",
    "drivers/input/tests/input_test.ko",
    "drivers/rtc/lib_test.ko",
    "fs/ext4/ext4-inode-test.ko",
    "fs/fat/fat_test.ko",
    "kernel/time/time_test.ko",
    "lib/kunit/kunit-example-test.ko",
    "lib/kunit/kunit-test.ko",
    # "mm/kfence/kfence_test.ko",
    "net/core/dev_addr_lists_test.ko",
    "sound/soc/soc-topology-test.ko",
    "sound/soc/soc-utils-test.ko",
]

# KUnit test module for arm64 only
_KUNIT_CLK_MODULES_LIST = [
    "drivers/clk/clk-gate_test.ko",
    "drivers/clk/clk_test.ko",
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
    "kernel/kheaders.ko",
    "mm/zsmalloc.ko",
]

# buildifier: disable=unnamed-macro
def get_gki_protected_modules_list(arch = None):
    all_gki_modules = get_gki_modules_list(arch) + get_kunit_modules_list(arch)
    unprotected_modules = _COMMON_UNPROTECTED_MODULES_LIST
    protected_modules = [mod for mod in all_gki_modules if mod not in unprotected_modules]
    return protected_modules
