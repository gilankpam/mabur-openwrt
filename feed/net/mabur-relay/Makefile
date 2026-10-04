include $(TOPDIR)/rules.mk

PKG_NAME:=mabur-relay
PKG_VERSION:=1
PKG_RELEASE:=1
PKG_MAINTAINER:=gilankpam <gilankpam@gmail.com>

include $(INCLUDE_DIR)/package.mk

define Package/mabur-relay
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Wireless
  TITLE:=mabur RX relay (CPE510 as a remote mabur card)
  DEPENDS:=+iw +libnl-tiny
endef

define Package/mabur-relay/description
  Forwards mabur air frames (radiotap intact, FCS-failed included) from a
  monitor vif to maburgs over UDP and to the web GS over ws://, and retunes
  the radio on request.
endef

define Package/mabur-relay/conffiles
/etc/mabur-relay.conf
endef

# In-tree source. Copy only sources, never host build artefacts.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/*.c ./src/*.h ./src/Makefile $(PKG_BUILD_DIR)/
endef

# -D_GNU_SOURCE: libnl-tiny's handlers.h/msg.h use struct ucred, which musl's
# sys/socket.h only declares under _GNU_SOURCE (iw's own build sets the same
# define for the same reason).
TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include/libnl-tiny -D_GNU_SOURCE
MAKE_FLAGS += mabur-relay RADIO=nl LDLIBS=-lnl-tiny

define Package/mabur-relay/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/libexec $(1)/etc/init.d $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mabur-relay $(1)/usr/sbin/mabur-relay
	$(INSTALL_BIN) ./files/mabur-relay-start $(1)/usr/libexec/mabur-relay-start
	$(INSTALL_BIN) ./files/mabur-relay.init $(1)/etc/init.d/mabur-relay
	$(INSTALL_CONF) ./files/mabur-relay.conf $(1)/etc/mabur-relay.conf
endef

$(eval $(call BuildPackage,mabur-relay))
