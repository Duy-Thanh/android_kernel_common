obj-$(CONFIG_MAC80211_HWSIM)	+= mac80211_hwsim.o
mac80211_hwsim-y		:= mac80211_hwsim_main.o mac80211_hwsim_nan.o
mac80211_hwsim-$(CONFIG_NET_APF) += apf_interpreter.o
CFLAGS_apf_interpreter.o := -include linux/export.h

obj-$(CONFIG_VIRT_WIFI)	+= virt_wifi.o
