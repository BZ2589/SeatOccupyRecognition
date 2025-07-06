#include <rtthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wlan_mgnt.h>
#include <wlan_prot.h>
#include <wlan_cfg.h>
#include <msh.h>
#include <drv_gpio.h>

#include "wifi_module.h"

#define WLAN_SSID "redmik50"
#define WLAN_PASSWORD "147258369"
#define NET_READY_TIME_OUT (rt_tick_from_millisecond(15 * 1000))

#define SERVER_PORT 8080
#define CLIENT_IP "192.168.80.203"

#define LED_ON  1
#define LED_OFF 0
#define PIN_LED_R GET_PIN(F, 12)

void update_database_from_udp(const char *seat_id_str, const char *status_str);

rt_bool_t g_connected = RT_FALSE;
rt_bool_t g_data_received = RT_FALSE;
int g_blink_count = 0;

/* 注意：这里不再定义SeatData结构体，而是通过头文件引用 */
extern SeatData g_seat_data;  // 声明使用头文件中声明的全局变量

static struct rt_semaphore net_ready;
static struct rt_semaphore scan_done;
static rt_thread_t udp_thread = RT_NULL;
static int sockfd = -1;

static void led_control(int state)
{
    rt_pin_write(PIN_LED_R, state ? LED_ON : LED_OFF);
}

static void blink_led(int count)
{
    int i;
    rt_kprintf("接收到数据，红灯将闪烁 %d 次\n", count);

    for (i = 0; i < count; i++)
    {
        led_control(LED_ON);
        rt_thread_mdelay(300);
        led_control(LED_OFF);
        rt_thread_mdelay(200);
    }

    g_data_received = RT_FALSE;
}

static void print_wlan_information(struct rt_wlan_info *info, int index)
{
    char *security;

    if (index == 0)
    {
        rt_kprintf("             SSID                      MAC            security    rssi chn Mbps\n");
        rt_kprintf("------------------------------- -----------------  -------------- ---- --- ----\n");
    }

    rt_kprintf("%-32.32s", &(info->ssid.val[0]));
    rt_kprintf("%02x:%02x:%02x:%02x:%02x:%02x  ",
            info->bssid[0], info->bssid[1], info->bssid[2],
            info->bssid[3], info->bssid[4], info->bssid[5]);
    switch (info->security)
    {
        case SECURITY_OPEN: security = "OPEN"; break;
        case SECURITY_WEP_PSK: security = "WEP_PSK"; break;
        case SECURITY_WEP_SHARED: security = "WEP_SHARED"; break;
        case SECURITY_WPA_TKIP_PSK: security = "WPA_TKIP_PSK"; break;
        case SECURITY_WPA_AES_PSK: security = "WPA_AES_PSK"; break;
        case SECURITY_WPA2_AES_PSK: security = "WPA2_AES_PSK"; break;
        case SECURITY_WPA2_TKIP_PSK: security = "WPA2_TKIP_PSK"; break;
        case SECURITY_WPA2_MIXED_PSK: security = "WPA2_MIXED_PSK"; break;
        case SECURITY_WPS_OPEN: security = "WPS_OPEN"; break;
        case SECURITY_WPS_SECURE: security = "WPS_SECURE"; break;
        default: security = "UNKNOWN"; break;
    }
    rt_kprintf("%-14.14s %-4d %3d %4d\n", security, info->rssi, info->channel, info->datarate / 1000000);
}

/* WiFi事件回调 */
void wlan_scan_report_hander(int event, struct rt_wlan_buff *buff, void *parameter)
{
    struct rt_wlan_info *info = RT_NULL;
    int index = *((int *)(parameter));

    info = (struct rt_wlan_info *)buff->data;
    print_wlan_information(info, index);
    ++*((int *)(parameter));
}

void wlan_scan_done_hander(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_sem_release(&scan_done);
}

void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_sem_release(&net_ready);
    g_connected = RT_TRUE;
    rt_kprintf("网络连接就绪\n");
}

void wlan_station_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("断开网络连接!\n");
    g_connected = RT_FALSE;
    led_control(LED_OFF);
}

static void wlan_connect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("已连接到SSID: %s\n", ((struct rt_wlan_info *)buff->data)->ssid.val);
}

static void wlan_connect_fail_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("连接SSID失败: %s\n", ((struct rt_wlan_info *)buff->data)->ssid.val);
}

/* UDP接收线程修改部分 */
void udp_recv_thread(void *parameter) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char recv_buf[32] = {0};
    char *token = RT_NULL;

    while (1) {
        if (g_connected) {
            int recv_len = recvfrom(sockfd, recv_buf, sizeof(recv_buf) - 1, 0,
                                    (struct sockaddr*)&client_addr, &client_addr_len);
            if (recv_len > 0) {
                recv_buf[recv_len] = '\0';
                g_data_received = RT_TRUE;

                if (strcmp(inet_ntoa(client_addr.sin_addr), CLIENT_IP) == 0) {
                    rt_kprintf("从 %s 接收到数据: %s\n", inet_ntoa(client_addr.sin_addr), recv_buf);

                    /* 解析字符串格式 "座位ID:状态" */
                    token = strtok(recv_buf, ":");
                    if (token) {
                        char seat_id[10] = {0};
                        char status[10] = {0};
                        strncpy(seat_id, token, sizeof(seat_id) - 1);

                        token = strtok(NULL, ":");
                        if (token) {
                            strncpy(status, token, sizeof(status) - 1);

                            // 调用数据库更新函数
                            update_database_from_udp(seat_id, status);
                        }
                    }
                }
                else {
                    rt_kprintf("忽略来自非指定客户端的数据: %s\n", inet_ntoa(client_addr.sin_addr));
                }
            }
        }

        rt_thread_mdelay(100);
    }
}
/* 自动连接配置 */
static int wifi_autoconnect(void)
{
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    rt_wlan_config_autoreconnect(RT_TRUE);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wlan_connect_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, wlan_connect_fail_handler, RT_NULL);
    return 0;
}

int wifi_module_init(void)
{
    static int i = 0;
    int result = RT_EOK;
    struct rt_wlan_info info;

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    led_control(LED_OFF);

    rt_sem_init(&scan_done, "scan_done", 0, RT_IPC_FLAG_FIFO);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, wlan_scan_report_hander, &i);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_DONE, wlan_scan_done_hander, RT_NULL);
    rt_kprintf("开始扫描WiFi热点...\n");
    rt_wlan_scan();
    rt_sem_take(&scan_done, RT_WAITING_FOREVER);

    rt_sem_init(&net_ready, "net_ready", 0, RT_IPC_FLAG_FIFO);
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wlan_ready_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wlan_station_disconnect_handler, RT_NULL);
    result = rt_wlan_connect(WLAN_SSID, WLAN_PASSWORD);

    if (result == RT_EOK)
    {
        rt_memset(&info, 0, sizeof(info));
        rt_wlan_get_info(&info);
        rt_kprintf("已连接热点信息:\n");
        print_wlan_information(&info, 0);

        result = rt_sem_take(&net_ready, NET_READY_TIME_OUT);
        if (result == RT_EOK)
        {
            msh_exec("ifconfig", rt_strlen("ifconfig"));
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in server_addr = {0};
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(SERVER_PORT);

            if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
            {
                rt_kprintf("绑定UDP端口失败\n");
                close(sockfd);
                return -1;
            }

            udp_thread = rt_thread_create("udp_recv", udp_recv_thread, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2, 20);
            if (udp_thread)
                rt_thread_startup(udp_thread);
            else
            {
                close(sockfd);
                return -1;
            }
        }
        else
        {
            rt_kprintf("等待IP获取超时!\n");
            return -1;
        }

        rt_wlan_unregister_event_handler(RT_WLAN_EVT_READY);
        rt_sem_detach(&net_ready);
    }
    else
    {
        rt_kprintf("连接WiFi热点(%s)失败!\n", WLAN_SSID);
        led_control(LED_OFF);
        return -1;
    }

    wifi_autoconnect();
    return 0;
}
