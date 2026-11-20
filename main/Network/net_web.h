#ifndef __NET_WEB_H
#define __NET_WEB_H

/* 手机远程控制：设备内跑 HTTP 服务，手机浏览器访问设备 IP 即可
 *   GET /                    控制页面（内嵌 HTML，走 Flash 不占 RAM）
 *   GET /api/io              GPIO 状态 JSON
 *   GET /api/io/set?pin=8&lvl=1  设置某个 GPIO 电平
 *   GET /api/log             最近 24 行串口日志（纯文本）
 * 无需 App、无需云平台，限同一局域网。 */

void        Web_Init(void);
const char *Web_GetIP(void);   /* 返回 "192.168.x.x"，未连上返回 "0.0.0.0" */

#endif
