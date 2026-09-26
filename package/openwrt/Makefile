# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

PKG_NAME:=wmcsd
PKG_VERSION:=0.2.0
PKG_RELEASE:=10
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=WMCS contributors

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
WMCS_ROOT?=$(abspath $(TOPDIR)/../wmcs)

include $(INCLUDE_DIR)/package.mk

define Package/wmcsd
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WirelessAPD
  TITLE:=Wi-Fi Mesh Coordination System daemon
  DEPENDS:=+libubus +libubox +libuci +libmbedtls
endef

define Package/wmcsd/description
 WMCS controller/agent service with bounded discovery, authenticated pairing,
 encrypted one-WLAN control, ownership-safe release and transactional OpenWrt
 apply. Mutation remains disabled by default.
endef

define Package/wmcsd/conffiles
/etc/config/wmcs
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) $(WMCS_ROOT)/src/wmcsd/. $(PKG_BUILD_DIR)/
	$(CP) $(WMCS_ROOT)/LICENSE $(PKG_BUILD_DIR)/LICENSE
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Werror -fPIE \
		-DWMCS_VERSION='"$(PKG_VERSION)-r$(PKG_RELEASE)"' \
		-I$(PKG_BUILD_DIR) \
		$(PKG_BUILD_DIR)/main.c $(PKG_BUILD_DIR)/ubus_api.c \
		$(PKG_BUILD_DIR)/atomic_file.c \
		$(PKG_BUILD_DIR)/discovery.c $(PKG_BUILD_DIR)/discovery_wire.c \
		$(PKG_BUILD_DIR)/identity.c $(PKG_BUILD_DIR)/pairing.c \
		$(PKG_BUILD_DIR)/pairing_crypto.c $(PKG_BUILD_DIR)/pairing_wire.c \
		$(PKG_BUILD_DIR)/control.c $(PKG_BUILD_DIR)/control_crypto.c \
		$(PKG_BUILD_DIR)/control_wire.c $(PKG_BUILD_DIR)/operation_store.c \
		$(PKG_BUILD_DIR)/result_store.c \
		$(PKG_BUILD_DIR)/roaming.c $(PKG_BUILD_DIR)/roaming_event.c \
		$(PKG_BUILD_DIR)/neighbor_sync.c \
		$(PKG_BUILD_DIR)/neighbor_sync_wire.c \
		$(PKG_BUILD_DIR)/neighbor_sync_store.c \
		$(PKG_BUILD_DIR)/wlan.c \
		$(TARGET_LDFLAGS) -Wl,-z,relro,-z,now -pie \
		-lubus -lubox -luci -lmbedcrypto -o $(PKG_BUILD_DIR)/wmcsd
endef

define Package/wmcsd/install
	$(INSTALL_DIR) $(1)/usr/lib/wmcs
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wmcsd $(1)/usr/lib/wmcs/wmcsd.bin
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/wmcsd.wrapper $(1)/usr/sbin/wmcsd
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wmcsd.init $(1)/etc/init.d/wmcsd
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/wmcs.config $(1)/etc/config/wmcs
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./files/wmcs.rpcd-acl.json $(1)/usr/share/rpcd/acl.d/wmcs.json
	$(INSTALL_DIR) $(1)/etc/wmcs
	chmod 0700 $(1)/etc/wmcs
endef

$(eval $(call BuildPackage,wmcsd))
