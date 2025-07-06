#ifndef __DATA_SIMULATOR_H__
#define __DATA_SIMULATOR_H__

#include <rtthread.h>
#include "status_manager.h"

extern struct rt_messagequeue ui_mq;

void data_simulator_init(void);

#endif
