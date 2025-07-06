#ifndef __WIFI_MODULE_H__
#define __WIFI_MODULE_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 座位数据结构定义 - 只在头文件中定义一次 */
typedef struct {
    char seat_id[10];     // 座位ID，如"A01"
    char status[5];       // 座位状态，如"1"
    rt_bool_t new_data;   // 标记是否有新数据
} SeatData;

int wifi_module_init(void);  // 初始化WiFi + 启动UDP线程

extern rt_bool_t g_connected;
extern rt_bool_t g_data_received;
extern int g_blink_count;
extern SeatData g_seat_data;  // 声明全局座位数据结构

#ifdef __cplusplus
}
#endif

#endif
