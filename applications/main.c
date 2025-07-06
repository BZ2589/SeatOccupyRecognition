#include <rtthread.h>
#include <stdlib.h>
#include <rthw.h>
#include <rtdevice.h>
#include <board.h>
#include <msh.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <drv_lcd.h>
#include <rtdef.h>
#include <ctype.h>  // 用于isdigit函数

#include "soft_wdt.h"
#include "status_manager.h"
#include "data_simulator.h"
#include "wifi_module.h"

// 统一定义
#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

// 座位状态字符串映射（通过头文件引入枚举）
const char* seat_status_strings[] = {
    "Available",
    "Occupied",
    "Claimed"
};

// 座位状态颜色映射
#define WHITE           0xFFFF
#define BLACK           0x0000
#define GREEN           0x07E0
#define RED             0xF800
#define YELLOW          0xFFE0
rt_uint16_t seat_status_colors[] = {
    GREEN,      // 空闲-绿色
    RED,        // 使用中-红色
    YELLOW      // 占座中-黄色
};

// 数据库结构
typedef struct {
    uint8_t id;              // 座位ID
    SeatStatus status;       // 座位状态
    rt_tick_t update_tick;   // 最后更新的系统滴答数
} SeatInfo;

typedef struct {
    SeatInfo seats[MAX_SEATS];  // 座位数组
    rt_uint8_t count;           // 当前座位数量
    rt_mutex_t lock;            // 互斥锁（注意：rt_mutex_t是指针类型）
} SeatDatabase;

// 全局变量
SeatDatabase seat_db;         // 座位数据库
SeatData g_seat_data = {0};   // 全局座位数据定义
static soft_wdt_t *soft_wdt = RT_NULL;
rt_uint8_t current_seat_id = 1;  // 当前显示的座位ID

/* 数据库功能声明 */
void db_init(void);
rt_err_t db_update_seat_status(rt_uint8_t seat_id, SeatStatus status);
SeatInfo* db_get_seat(rt_uint8_t seat_id);
void db_display_all_seats(void);
void update_database_from_udp(const char *seat_id_str, const char *status_str);

/* 软件看门狗超时回调函数 */
static void wdt_timeout_callback(void *arg)
{
    rt_kprintf("Software watchdog timeout! System will reset\n");
    rt_hw_cpu_reset();
}

/* 将字符串状态转换为枚举值 */
SeatStatus str_to_seat_status(const char *status_str)
{
    if (strcmp(status_str, "3") == 0 || strcmp(status_str, "4") == 0 || strcmp(status_str, "Available") == 0) {
        return SEAT_AVAILABLE;
    } else if (strcmp(status_str, "1") == 0 || strcmp(status_str, "Occupied") == 0) {
        return SEAT_OCCUPIED;
    } else if (strcmp(status_str, "2") == 0 || strcmp(status_str, "Claimed") == 0) {
        return SEAT_CLAIMED;
    }
    return SEAT_AVAILABLE;
}

/* 显示单个座位信息到LCD（仅修改数据来源） */
void show_seat_on_lcd(void)
{
    static rt_bool_t is_initialized = RT_FALSE;
    static char last_seat_id[32] = {0};
    static SeatStatus last_status = SEAT_AVAILABLE; // 记录上次状态

    if (g_seat_data.new_data && g_seat_data.seat_id[0] != '\0') {
        // 设置文本颜色为黑色，背景为白色
        lcd_set_color(BLACK, WHITE);

        // 首次显示或座位ID变化时显示标题、日期和座位ID
        if (!is_initialized || strcmp(g_seat_data.seat_id, last_seat_id) != 0) {
            // 显示标题和日期
            lcd_show_string(10, 20, 24, "Seat Information");
            char date_str[32];
            rt_sprintf(date_str, "Date: 2025-07-05");
            lcd_show_string(10, 50, 16, date_str);

            // 绘制分隔线
            lcd_draw_line(0, 75, 240, 75);

            // 显示座位ID
            char seat_info[32];
            rt_sprintf(seat_info, "Seat ID: %s", g_seat_data.seat_id);
            lcd_show_string(10, 90, 24, seat_info);

            is_initialized = RT_TRUE;
            strncpy(last_seat_id, g_seat_data.seat_id, sizeof(last_seat_id)-1);
            last_seat_id[sizeof(last_seat_id)-1] = '\0';
        }

        // 从数据库获取座位状态（新增代码）
        SeatInfo *seat = NULL;
        if (rt_mutex_take(seat_db.lock, RT_WAITING_FOREVER) == RT_EOK) {
            seat = db_get_seat(atoi(g_seat_data.seat_id));
            rt_mutex_release(seat_db.lock);
        }

        // 状态分两行显示
        SeatStatus status = SEAT_AVAILABLE;
        if (seat) {
            status = seat->status;
        } else {
            // 座位不存在时的处理（保持原有逻辑）
            status = str_to_seat_status(g_seat_data.status);
        }

        /* 清除旧状态文字（用白色文字+白色背景覆盖） */
        lcd_set_color(WHITE, WHITE);  // 白色文字+白色背景
        lcd_show_string(10, 170, 32, seat_status_strings[last_status]);

        /* 显示新状态文字（正确的颜色+白色背景） */
        lcd_set_color(seat_status_colors[status], WHITE);  // 状态颜色+白色背景
        lcd_show_string(10, 170, 32, seat_status_strings[status]);

        /* 显示状态标题（黑色文字+白色背景） */
        lcd_set_color(BLACK, WHITE);
        lcd_show_string(10, 130, 32, "Status:");

        // 保存当前状态
        last_status = status;

        g_seat_data.new_data = RT_FALSE;
    }
}

void db_init(void) {
    rt_memset(&seat_db, 0, sizeof(SeatDatabase));

    // 使用rt_mutex_create创建互斥锁
    seat_db.lock = rt_mutex_create("seat_lock", RT_IPC_FLAG_FIFO);
    if (seat_db.lock == RT_NULL) {
        LOG_E("Database mutex creation failed");
        return;
    }

    LOG_I("Seat database initialized. Max seats: %d", MAX_SEATS);
}

rt_err_t db_update_seat_status(rt_uint8_t seat_id, SeatStatus status) {
    SeatInfo *seat = NULL;

    if (rt_mutex_take(seat_db.lock, RT_WAITING_FOREVER) != RT_EOK) {
        LOG_E("Failed to take mutex");
        return -RT_ERROR;
    }

    // 在数据库中查找座位
    for (int i = 0; i < seat_db.count; i++) {
        if (seat_db.seats[i].id == seat_id) {
            seat = &seat_db.seats[i];
            break;
        }
    }

    // 如果找不到座位，创建一个新的
    if (seat == NULL) {
        if (seat_db.count < MAX_SEATS) {
            seat = &seat_db.seats[seat_db.count];
            seat->id = seat_id;
            seat_db.count++;
        } else {
            LOG_E("Database full, cannot add new seat!");
            rt_mutex_release(seat_db.lock);
            return -RT_ERROR;
        }
    }

    // 更新座位信息
    seat->status = status;
    seat->update_tick = rt_tick_get();

    LOG_I("Seat %d updated to %s", seat->id, seat_status_strings[seat->status]);

    rt_mutex_release(seat_db.lock);
    return RT_EOK;
}

SeatInfo* db_get_seat(rt_uint8_t seat_id) {
    SeatInfo *seat = RT_NULL;

    if (rt_mutex_take(seat_db.lock, RT_WAITING_FOREVER) != RT_EOK) {
        LOG_E("Failed to take mutex");
        return RT_NULL;
    }

    for (int i = 0; i < seat_db.count; i++) {
        if (seat_db.seats[i].id == seat_id) {
            seat = &seat_db.seats[i];
            break;
        }
    }

    rt_mutex_release(seat_db.lock);
    return seat;
}

void db_display_all_seats(void) {
    LOG_I("=== All Seats Status ===");

    if (rt_mutex_take(seat_db.lock, RT_WAITING_FOREVER) != RT_EOK) {
        LOG_E("Failed to take mutex");
        return;
    }

    for (int i = 0; i < seat_db.count; i++) {
        SeatInfo *seat = &seat_db.seats[i];
        LOG_I("Seat %d: %-10s Updated: %d ticks",
              seat->id,
              seat_status_strings[seat->status],
              (int)seat->update_tick);
    }

    rt_mutex_release(seat_db.lock);
}

void update_database_from_udp(const char *seat_id_str, const char *status_str) {
    // 跳过非数字字符（如'A'），只解析数字部分
    const char *id_start = seat_id_str;
    while (*id_start != '\0' && !isdigit(*id_start)) {
        id_start++;
    }

    // 转换数字部分
    rt_uint8_t seat_id = atoi(id_start);
    SeatStatus status = str_to_seat_status(status_str);

    // 参数有效性检查
    if (seat_id == 0 || status > SEAT_CLAIMED) {
        LOG_W("Invalid seat data: ID=%d, status=%d (raw ID=%s, raw status=%s)",
              seat_id, status, seat_id_str, status_str);
        return;
    }

    // 更新数据库
    db_update_seat_status(seat_id, status);

    // 更新全局数据用于显示（确保ID和状态正确）
    strncpy(g_seat_data.seat_id, seat_id_str, sizeof(g_seat_data.seat_id) - 1);
    strncpy(g_seat_data.status, status_str, sizeof(g_seat_data.status) - 1);

    // 强制触发显示更新（确保UI线程刷新）
    g_seat_data.new_data = RT_TRUE;
}

static void ui_thread_entry(void *param) {
    rt_kprintf("[UI] Thread started\n");

    // 初始化LCD前加日志
    rt_kprintf("[UI] Start LCD init\n");
    rt_device_t dev = rt_device_find("lcd");
    if (dev == RT_NULL) {
        rt_kprintf("[UI] LCD device not found!\n");
        return;
    }
    rt_device_init(dev);
    rt_kprintf("[UI] LCD init done\n");

    lcd_clear(WHITE);
    while(1) {
        if (g_connected) {
            // 显示座位信息到LCD
            show_seat_on_lcd();

            if (soft_wdt) {
                soft_wdt_feed(soft_wdt);
            }

            rt_thread_mdelay(500);
        }
    }
}

/* 周期性喂狗线程 */
static void feed_thread_entry(void *param) {
    while (1) {
        if (soft_wdt) {
            soft_wdt_feed(soft_wdt);
            rt_kprintf("[WDT] Feed software watchdog\n");
        }
        rt_thread_mdelay(2000);
    }
}

int main(void) {
    rt_thread_t tid = RT_NULL;

    /* 初始化软件看门狗 */
    soft_wdt = soft_wdt_init("soft_wdt", 5000, wdt_timeout_callback, RT_NULL);
    if (!soft_wdt) {
        rt_kprintf("Software watchdog initialization failed!\n");
        return RT_ERROR;
    }
    soft_wdt_start(soft_wdt);

    /* 初始化数据库 */
    db_init();

    /* 添加初始数据 */
    db_update_seat_status(1, SEAT_AVAILABLE);
    db_update_seat_status(2, SEAT_OCCUPIED);

    /* 初始化状态管理器 */
    status_init();

    /* 创建周期性喂狗线程 */
    tid = rt_thread_create("feed", feed_thread_entry,
                          NULL, 1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if(tid) rt_thread_startup(tid);

    /* 创建UI线程 */
    tid = rt_thread_create("ui", ui_thread_entry,
                          NULL, 2048, RT_THREAD_PRIORITY_MAX - 5, 10);
    if(tid) rt_thread_startup(tid);

    /* 初始化 WiFi 模块 */
    if (wifi_module_init() != RT_EOK) {
        rt_kprintf("WiFi模块初始化失败，系统退出\n");
        return -1;
    }

    rt_kprintf("System startup completed, software watchdog enabled\n");

    while (1) {
        rt_thread_mdelay(500);
    }

    return 0;
}
