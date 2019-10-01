#ifndef __VIRT_WIFI
#define __VIRT_WIFI

struct virt_wifi_wiphy_priv {
        struct delayed_work scan_result;
        struct cfg80211_scan_request *scan_request;
        bool being_deleted;
        int (*notify_device_open)(void);
        int (*notify_device_stop)(void);
        int (*notify_scan_trigger)(void);
        int (*generate_virt_scan_result)(struct wiphy *wiphy);
};

int register_data_simulation(struct virt_wifi_wiphy_priv *data_ops);
#endif

