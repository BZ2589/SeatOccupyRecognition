#include <rtthread.h>
#include <finsh.h>
#include <stdlib.h>
#include "status_manager.h"

/* 头文件已定义MAX_SEATS，此处无需重复定义 */

struct seat_manager_type seat_manager;

void status_init(void)
{
    /* 初始化互斥锁 */
    seat_manager.lock = rt_mutex_create("seat_mux", RT_IPC_FLAG_PRIO);
    RT_ASSERT(seat_manager.lock != RT_NULL);

    /* 初始化所有座位状态为空闲 */
    for (int i = 0; i < MAX_SEATS; i++) {
        seat_manager.status[i] = SEAT_AVAILABLE;
    }

    seat_manager.silent_mode = RT_FALSE;
}
MSH_CMD_EXPORT(status_init, initialize seat management system);

void update_seat(uint8_t seat_id, SeatStatus status)
{
    /* 参数校验 */
    if (seat_id >= MAX_SEATS || status > SEAT_CLAIMED) {
        if (!seat_manager.silent_mode) {
            rt_kprintf("ERROR: Invalid seat_id(%d) or status(%d)\n", seat_id, status);
        }
        return;
    }

    /* 线程安全操作 */
    rt_mutex_take(seat_manager.lock, RT_WAITING_FOREVER);
    seat_manager.status[seat_id] = status;
    rt_mutex_release(seat_manager.lock);
}

SeatStatus query_seat(uint8_t seat_id)
{
    SeatStatus status = SEAT_AVAILABLE;

    if (seat_id < MAX_SEATS) {
        rt_mutex_take(seat_manager.lock, RT_WAITING_FOREVER);
        status = seat_manager.status[seat_id];
        rt_mutex_release(seat_manager.lock);
    }

    return status;
}

/* 修改函数定义与头文件声明一致 */
void print_all_seats(void)
{
    rt_kprintf("Current seat status:\n");
    for (int i = 0; i < MAX_SEATS; i++) {
        rt_kprintf("  Seat %d: %s\n", i,
            (seat_manager.status[i] == SEAT_AVAILABLE) ? "Available" :
            (seat_manager.status[i] == SEAT_OCCUPIED) ? "Occupied" : "Claimed");
    }
}

/* MSH命令包装函数（与print_all_seats分离） */
static void msh_print_all_seats(int argc, char **argv)
{
    print_all_seats();
}
MSH_CMD_EXPORT(msh_print_all_seats, show all seat status);

static void set_seat(int argc, char **argv)
{
    if (argc != 3) {
        rt_kprintf("Usage: set_seat <seat_id> <status>\n");
        return;
    }

    int seat_id = atoi(argv[1]);
    int status = atoi(argv[2]);

    if (status >= SEAT_AVAILABLE && status <= SEAT_CLAIMED) {
        update_seat(seat_id, (SeatStatus)status);
    } else {
        rt_kprintf("Invalid status, should be 0(Available), 1(Occupied), 2(Claimed)\n");
    }
}
MSH_CMD_EXPORT(set_seat, set seat status: set_seat <id> <status>);

static void fix_seat(int argc, char **argv)
{
    if (argc != 3) {
        rt_kprintf("Usage: fix_seat <seat_id> <status>\n");
        return;
    }

    int seat_id = atoi(argv[1]);
    int status = atoi(argv[2]);

    /* 强制修复异常数据 */
    if (seat_id >= 0 && seat_id < MAX_SEATS && status <= SEAT_CLAIMED) {
        update_seat(seat_id, (SeatStatus)status);
        rt_kprintf("Seat %d fixed to %s\n", seat_id,
            (status == SEAT_AVAILABLE) ? "Available" :
            (status == SEAT_OCCUPIED) ? "Occupied" : "Claimed");
    } else {
        rt_kprintf("Invalid parameters\n");
    }
}
MSH_CMD_EXPORT(fix_seat, force fix seat status);

void set_silent_mode(rt_bool_t mode)
{
    seat_manager.silent_mode = mode;
}
