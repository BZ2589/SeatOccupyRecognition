#ifndef __STATUS_MANAGER_H__
#define __STATUS_MANAGER_H__

#include <rtthread.h>

/* 在头文件中声明MAX_SEATS宏，在c文件中定义 */
#define MAX_SEATS 2

// 统一座位状态枚举
typedef enum {
    SEAT_AVAILABLE = 0,     // 空闲
    SEAT_OCCUPIED = 1,      // 使用中
    SEAT_CLAIMED = 2        // 占座中
} SeatStatus;

struct seat_manager_type {
    rt_mutex_t lock;  // 正确：互斥锁指针类型
    uint8_t status[MAX_SEATS];
    rt_bool_t silent_mode;
};

extern struct seat_manager_type seat_manager;

void status_init(void);
void update_seat(uint8_t seat_id, SeatStatus status);
SeatStatus query_seat(uint8_t seat_id);
void print_all_seats(void);
void set_silent_mode(rt_bool_t mode);

#endif
