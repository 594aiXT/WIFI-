#ifndef __NET_CONFIG_H
#define __NET_CONFIG_H

/* WiFi 凭据模板。
 *
 * 用法：把本文件复制为同目录下的 net_config.h，填入真实 SSID 和密码。
 * net_config.h 已在 .gitignore 中，不会进入版本库。
 *
 *   cp app/net_config.example.h app/net_config.h
 */

#define NET_WIFI_SSID          "YOUR_WIFI_SSID"
#define NET_WIFI_PWD           "YOUR_WIFI_PASSWORD"

#endif /* __NET_CONFIG_H */
