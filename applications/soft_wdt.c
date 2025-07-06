#include "soft_wdt.h"
#include <rtthread.h>

/* 软件看门狗线程入口函数 */
static void soft_wdt_thread_entry(void *parameter)
{
    soft_wdt_t *wdt = (soft_wdt_t *)parameter;
    rt_uint32_t last_feed = 0;

    while (1) {
        rt_mutex_take(wdt->mutex, RT_WAITING_FOREVER);  // 修正指针传递

        /* 检查是否需要触发超时 */
        if (wdt->enabled && (rt_tick_get() - last_feed > wdt->timeout)) {
            wdt->feed_count++;

            /* 超过复位阈值则触发回调 */
            if (wdt->feed_count >= wdt->reset_count) {
                rt_mutex_release(wdt->mutex);  // 修正指针传递
                rt_kprintf("Software watchdog timeout! Trigger callback\n");

                /* 调用超时回调函数 */
                if (wdt->callback) {
                    wdt->callback(wdt->callback_arg);
                } else {
                    /* 默认回调：系统复位 */
                    rt_kprintf("System will reset now\n");
                    rt_hw_cpu_reset();
                }
            }
        }

        last_feed = rt_tick_get();
        rt_mutex_release(wdt->mutex);  // 修正指针传递

        /* 短暂延时，减少CPU占用 */
        rt_thread_mdelay(100);
    }
}

/* 软件看门狗定时器回调函数 */
static void soft_wdt_timer_callback(void *parameter)
{
    soft_wdt_t *wdt = (soft_wdt_t *)parameter;

    /* 移除中断上下文中的互斥锁操作 */
    /* 使用原子操作增加计数，避免在ISR中使用互斥锁 */
    __sync_fetch_and_add(&wdt->feed_count, 1);
}

/* 软件看门狗初始化 */
soft_wdt_t* soft_wdt_init(const char *name, rt_uint32_t timeout_ms,
                         soft_wdt_callback_t cb, void *arg)
{
    soft_wdt_t *wdt;
    rt_err_t result;

    /* 分配内存 */
    wdt = (soft_wdt_t *)rt_malloc(sizeof(soft_wdt_t));
    if (!wdt) {
        rt_kprintf("Soft watchdog memory allocate failed\n");
        return RT_NULL;
    }

    /* 初始化成员变量 */
    wdt->timeout = timeout_ms * RT_TICK_PER_SECOND / 1000;  // 毫秒转 tick
    wdt->feed_count = 0;
    wdt->reset_count = 2;  // 两次超时触发复位
    wdt->enabled = RT_FALSE;
    wdt->callback = cb;
    wdt->callback_arg = arg;

    /* 创建互斥锁 - 修正：动态分配互斥锁内存 */
    wdt->mutex = (rt_mutex_t)rt_malloc(sizeof(struct rt_mutex));
    if (!wdt->mutex) {
        rt_free(wdt);
        rt_kprintf("Soft watchdog mutex allocate failed\n");
        return RT_NULL;
    }

    /* 初始化互斥锁 */
    result = rt_mutex_init(wdt->mutex, name, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK) {
        rt_free(wdt->mutex);
        rt_free(wdt);
        rt_kprintf("Soft watchdog mutex init failed\n");
        return RT_NULL;
    }

    /* 创建看门狗线程 */
    wdt->thread = rt_thread_create(name,
                                  soft_wdt_thread_entry,
                                  wdt,
                                  1024,
                                  RT_THREAD_PRIORITY_MAX - 2,
                                  10);
    if (!wdt->thread) {
        rt_mutex_detach(wdt->mutex);
        rt_free(wdt->mutex);
        rt_free(wdt);
        rt_kprintf("Soft watchdog thread create failed\n");
        return RT_NULL;
    }

    /* 创建超时定时器 */
    wdt->timer = rt_timer_create(name,
                                soft_wdt_timer_callback,
                                wdt,
                                timeout_ms,
                                RT_TIMER_FLAG_PERIODIC);
    if (!wdt->timer) {
        rt_thread_delete(wdt->thread);
        rt_mutex_detach(wdt->mutex);
        rt_free(wdt->mutex);
        rt_free(wdt);
        rt_kprintf("Soft watchdog timer create failed\n");
        return RT_NULL;
    }

    rt_kprintf("Software watchdog initialized successfully\n");
    return wdt;
}

/* 启动软件看门狗 */
rt_err_t soft_wdt_start(soft_wdt_t *wdt)
{
    if (!wdt) return -RT_ERROR;

    rt_mutex_take(wdt->mutex, RT_WAITING_FOREVER);  // 修正指针传递
    wdt->enabled = RT_TRUE;
    wdt->feed_count = 0;
    rt_mutex_release(wdt->mutex);  // 修正指针传递

    rt_thread_startup(wdt->thread);
    rt_timer_start(wdt->timer);

    rt_kprintf("Software watchdog started\n");
    return RT_EOK;
}

/* 停止软件看门狗 */
rt_err_t soft_wdt_stop(soft_wdt_t *wdt)
{
    if (!wdt) return -RT_ERROR;

    rt_mutex_take(wdt->mutex, RT_WAITING_FOREVER);  // 修正指针传递
    wdt->enabled = RT_FALSE;
    rt_mutex_release(wdt->mutex);  // 修正指针传递

    rt_timer_stop(wdt->timer);
    rt_thread_delete(wdt->thread);

    rt_kprintf("Software watchdog stopped\n");
    return RT_EOK;
}

/* 喂狗操作 */
rt_err_t soft_wdt_feed(soft_wdt_t *wdt)
{
    if (!wdt) return -RT_ERROR;

    rt_mutex_take(wdt->mutex, RT_WAITING_FOREVER);  // 修正指针传递
    wdt->feed_count = 0;
    rt_mutex_release(wdt->mutex);  // 修正指针传递

    return RT_EOK;
}

/* 销毁软件看门狗 */
rt_err_t soft_wdt_destroy(soft_wdt_t *wdt)
{
    if (!wdt) return -RT_ERROR;

    soft_wdt_stop(wdt);
    rt_timer_delete(wdt->timer);
    rt_thread_delete(wdt->thread);

    /* 修正：使用 rt_mutex_detach 并释放内存 */
    rt_mutex_detach(wdt->mutex);
    rt_free(wdt->mutex);

    rt_free(wdt);

    rt_kprintf("Software watchdog destroyed\n");
    return RT_EOK;
}
