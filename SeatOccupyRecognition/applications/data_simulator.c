#include <rtthread.h>
#include <stdlib.h>
#include "data_simulator.h"
#include "status_manager.h"

static rt_timer_t sim_timer;

/* 模拟数据生成 */
static void simulate_data(void *param) {
    uint8_t seat_id = rand() % MAX_SEATS;
    uint8_t status = rand() % 3;
    update_seat(seat_id, status); // 自动遵守静默模式设置
}

/* 数据模拟器初始化 */
void data_simulator_init(void) {
    sim_timer = rt_timer_create("sim", simulate_data, NULL,
                              2000, RT_TIMER_FLAG_PERIODIC);
    if(sim_timer) rt_timer_start(sim_timer);
}
