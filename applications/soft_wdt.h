#ifndef SOFT_WDT_H
#define SOFT_WDT_H

#include <rtthread.h>
#include <rtdevice.h>  // 添加缺少的头文件
#include <rthw.h>

/* 软件看门狗超时回调函数类型 */
typedef void (*soft_wdt_callback_t)(void *arg);

/* 软件看门狗句柄类型 */
typedef struct soft_wdt {
    rt_thread_t thread;         // 看门狗线程句柄
    rt_timer_t timer;           // 超时定时器
    rt_mutex_t mutex;           // 互斥锁
    rt_uint32_t timeout;        // 超时时间（毫秒）
    volatile rt_uint32_t feed_count;     // 喂狗计数，添加volatile修饰符
    rt_uint32_t reset_count;    // 复位阈值
    rt_bool_t enabled;          // 看门狗使能标志
    soft_wdt_callback_t callback; // 超时回调函数
    void *callback_arg;         // 回调函数参数
} soft_wdt_t;

/* 软件看门狗初始化 */
soft_wdt_t* soft_wdt_init(const char *name, rt_uint32_t timeout_ms,
                         soft_wdt_callback_t cb, void *arg);

/* 启动软件看门狗 */
rt_err_t soft_wdt_start(soft_wdt_t *wdt);

/* 停止软件看门狗 */
rt_err_t soft_wdt_stop(soft_wdt_t *wdt);

/* 喂狗操作 */
rt_err_t soft_wdt_feed(soft_wdt_t *wdt);

/* 销毁软件看门狗 */
rt_err_t soft_wdt_destroy(soft_wdt_t *wdt);

#endif /* SOFT_WDT_H */
