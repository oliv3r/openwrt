# SPDX-License-Identifier: GPL-2.0-only

define Device/kernel
  KERNEL := \
    kernel-bin | \
    append-dtb | \
    lzma | \
    uImage lzma
endef

define Device/initramfs
  KERNEL_INITRAMFS := \
    kernel-bin | \
    append-dtb | \
    lzma | \
    uImage lzma
endef

define Device/ezu_next-7255gh_a2
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := NEXT-7255GH A2
  DEVICE_VENDOR := EZ-NET Ubiquitous
  IMAGE_SIZE := 15424k
  SOC := rtl9303
  UIMAGE_MAGIC := 0x93030000
endef
TARGET_DEVICES += ezu_next-7255gh_a2

define Device/ezu_next-7255gh_a2_vendor
  $(Device/ezu_next-7255gh_a2)
  DEVICE_MODEL := NEXT-7255GH A2 (vendor)
  IMAGE_SIZE := 13312k
endef
TARGET_DEVICES += ezu_next-7255gh_a2_vendor

define Device/hasivo_f1100w-8splus
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := F1100W-8S+ A2
  DEVICE_VENDOR := Hasivo
  IMAGE_SIZE := 31808k
  SOC := rtl9303
  UIMAGE_MAGIC := 0x93030000
endef
TARGET_DEVICES += hasivo_f1100w-8splus

define Device/hasivo_f1100w-8splus_vendor
  $(Device/hasivo_f1100w-8splus)
  DEVICE_MODEL := F1100W-8S+ A2 (vendor)
  IMAGE_SIZE := 29696k
endef
TARGET_DEVICES += hasivo_f1100w-8splus_vendor

define Device/mestechs_msg9424
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := msg9424
  DEVICE_VENDOR := Mestechs
  IMAGE_SIZE := 31808k
  SOC := rtl9301
  UIMAGE_MAGIC := 0x93010000
endef
TARGET_DEVICES += mestechs_msg9424

define Device/mestechs_msg9424_vendor
  $(Device/mestechs_msg9424)
  DEVICE_MODEL := MSG9424 (vendor)
  IMAGE_SIZE := 7232k
endef
TARGET_DEVICES += mestechs_msg9424_vendor

define Device/tp-link_tl-st1008f_v2.0
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := ST1008F v2.0
  DEVICE_VENDOR := TP-Link
  IMAGE_SIZE := 31808k
  SOC := rtl9303
  UIMAGE_MAGIC := 0x93030000
endef
TARGET_DEVICES += tp-link_tl-st1008f_v2.0

define Device/tp-link_tl-st1008f_v2.0_vendor
  $(Device/tp-link_tl-st1008f_v2.0)
  DEVICE_MODEL := ST1008F v2.0 (vendor)
  IMAGE_SIZE := 14848k
endef
TARGET_DEVICES += tp-link_tl-st1008f_v2.0_vendor

define Device/ubiquiti-usw-aggregation
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := USW-Aggregation
  DEVICE_VENDOR := Ubiquiti
  IMAGE_SIZE := 15424k
  SOC := rtl9303
  UIMAGE_MAGIC := 0x93030000
endef
TARGET_DEVICES += ubiquiti-usw-aggregation

define Device/ubiquiti-usw-aggregation_vendor
  $(Device/ubiquiti-usw-aggregation)
  DEVICE_MODEL := USW-Aggregation (vendor)
  IMAGE_SIZE := 7232k
endef
TARGET_DEVICES += ubiquiti-usw-aggregation_vendor

define Device/whitelabel_ws-sw-24p-4s_r1_v1.0
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_MODEL := ws-sw-24p-4s-r1 v1.0
  DEVICE_VENDOR := WhiteLabel
  IMAGE_SIZE := 31808k
  SOC := rtl9301
  UIMAGE_MAGIC := 0x93010000
endef
TARGET_DEVICES += whitelabel_ws-sw-24p-4s_r1_v1.0

define Device/whitelabel_ws-sw-24p-4s_r1_v1.0_oem
  $(Device/whitelabel_ws-sw-24p-4s_r1_v1.0)
  DEVICE_MODEL := ws-sw-24p-4s-r1 v1.0 (OEM)
  IMAGE_SIZE := 13312k
endef
TARGET_DEVICES += whitelabel_ws-sw-24p-4s_r1_v1.0_oem

define Device/zyxel_initramfs
  KERNEL_INITRAMFS := \
    kernel-bin | \
    append-dtb | \
    lzma | \
    zyxel-vers $$$$(ZYXEL_VERS) | \
    uImage lzma
endef

define Device/zyxel_xgs1200
  $(Device/kernel)
  $(Device/initramfs)
  DEVICE_VENDOR := Zyxel
  IMAGE_SIZE := 15424k
  SOC := rtl9302b
endef

define Device/zyxel_xgs1010-12
  $(Device/zyxel_xgs1200)
  UIMAGE_MAGIC := 0x93001010
  DEVICE_MODEL := XGS1010-12
endef
TARGET_DEVICES += zyxel_xgs1010-12

define Device/zyxel_xgs1010-12_vendor
  $(Device/zyxel_xgs1010-12)
  $(Device/zyxel_initramfs)
  DEVICE_MODEL := XGS1010-12_vendor
  IMAGE_SIZE := 15232k
  ZYXEL_VERS := ABTY
endef
TARGET_DEVICES += zyxel_xgs1010-12_vendor

define Device/zyxel_xgs1210-12
  $(Device/zyxel_xgs1200)
  DEVICE_MODEL := XGS1210-12
  UIMAGE_MAGIC := 0x93001210
endef
TARGET_DEVICES += zyxel_xgs1210-12

define Device/zyxel_xgs1210-12_vendor
  $(Device/zyxel_xgs1210-12)
  $(Device/zyxel_initramfs)
  DEVICE_MODEL := XGS1210-12_vendor
  IMAGE_SIZE := 13184k
  ZYXEL_VERS := ABTY
endef
TARGET_DEVICES += zyxel_xgs1210-12_vendor

define Device/zyxel_xgs1250-12
  $(Device/zyxel_xgs1200)
  DEVICE_MODEL := XGS1250-12
  UIMAGE_MAGIC := 0x93001250
endef
TARGET_DEVICES += zyxel_xgs1250-12

define Device/zyxel_xgs1250-12_vendor
  $(Device/zyxel_xgs1250-12)
  $(Device/zyxel_initramfs)
  DEVICE_MODEL := XGS1250-12_vendor
  IMAGE_SIZE := 13184k
  ZYXEL_VERS := ABWE
endef
TARGET_DEVICES += zyxel_xgs1250-12_vendor
