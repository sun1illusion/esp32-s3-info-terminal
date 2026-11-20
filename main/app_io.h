#ifndef __APP_IO_H
#define __APP_IO_H

/* 可操作的 GPIO 抽象层：屏幕设置页和手机网页共用同一份状态，
 * 避免两边各管一套导致显示不一致。 */

#define APP_IO_COUNT 8

extern const int app_io_pins[APP_IO_COUNT];   /* {8,9,10,11,12,13,14,18} */

void AppIO_Init(void);
int  AppIO_Get(int idx);            /* 返回 0/1；越界返回 -1 */
void AppIO_Set(int idx, int level); /* level 非 0 即高电平 */
void AppIO_Toggle(int idx);

#endif
