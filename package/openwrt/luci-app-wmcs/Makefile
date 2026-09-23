# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

LUCI_TITLE:=WMCS LuCI interface
LUCI_DESCRIPTION:=Runtime, topology, roaming and UCI interface for the WMCS coordination daemon
LUCI_DEPENDS:=+luci-base
LUCI_PKGARCH:=all
PKG_NAME:=luci-app-wmcs
LUCI_NAME:=luci-app-wmcs
override LUCI_LANGUAGES:=ru
PKG_VERSION:=0.2.0
PKG_RELEASE:=4
PKG_PO_VERSION:=0.2.0-r4

PKG_LICENSE:=Apache-2.0
PKG_MAINTAINER:=WMCS contributors

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
