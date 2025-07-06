import sensor
import image
import time
import tf
import os
import gc
import network
import socket

# 系统初始化参数
SEAT_ID = "A01"  # 座位编号
STATUS_DESCRIPTIONS = {
    "1": "使用中",
    "2": "占座中",
    "3": "空闲",
    "4": "空闲"
}

# WiFi网络配置（需根据实际环境修改）
WIFI_SSID = "redmik50"
WIFI_KEY = "147258369"
DEST_IP = "192.168.80.2"
DEST_PORT = 8080

# 传感器初始化
def init_sensor():
    sensor.reset()
    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QVGA)
    sensor.set_windowing(300, 300)
    sensor.skip_frames(time=2000)
    print("传感器初始化完成")

# WiFi与UDP初始化
def init_network():
    # 初始化WiFi
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print(f"正在连接WiFi: {WIFI_SSID}")
        wlan.connect(WIFI_SSID, WIFI_KEY)
        while not wlan.isconnected():
            time.sleep(1)
    print(f"WiFi连接成功，本地IP: {wlan.ifconfig()[0]}")

    # 初始化UDP socket
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"UDP发送器启动，目标: {DEST_IP}:{DEST_PORT}")
    return udp_socket

# 模型初始化
def load_model():
    model_path = 'trained.tflite'
    if model_path not in os.listdir():
        raise Exception(f"{model_path} 未找到")

    try:
        net = tf.load(model_path, load_to_fb=True)
        print("模型加载成功!")
        return net
    except Exception as e:
        raise Exception("模型加载失败: " + str(e))

# 主程序逻辑
try:
    # 初始化各模块
    init_sensor()
    net = load_model()
    udp_socket = init_network()

    # 类别标签与颜色
    labels = ["background", "book", "other", "person", "table"]
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 0, 255)]

    # 检测参数
    confidence_threshold = 0.8
    clock = time.clock()

    # 状态检测参数
    time_window = 5000  # 5秒统计窗口
    person_threshold = 7  # 5秒内Person检测次数阈值
    status_check_interval = 1000  # 每秒检查一次

    # 检测结果统计
    detection_stats = {
        "person_count": 0,
        "book_detected": False,
        "other_detected": False,
        "table_detected": False,
        "last_reset": time.ticks_ms(),
        "last_status": None
    }

    current_status = None

    print("\n===== 启动目标检测与数据发送 =====")

    # 主循环
    while True:
        clock.tick()
        img = sensor.snapshot()
        current_time = time.ticks_ms()

        try:
            # 执行目标检测
            detections = net.detect(img, threshold=confidence_threshold)

            # 重置帧级检测标志
            frame_has_person = False
            frame_has_book = False
            frame_has_other = False
            frame_has_table = False

            # 解析检测结果
            for class_idx in range(1, len(labels)):
                class_name = labels[class_idx]
                class_detections = detections[class_idx] if class_idx < len(detections) else []

                for obj in class_detections:
                    try:
                        confidence = obj.output() if callable(getattr(obj, 'output', None)) else 0.0
                        if float(confidence) >= confidence_threshold:
                            if class_name == "person":
                                frame_has_person = True
                            elif class_name == "book":
                                frame_has_book = True
                            elif class_name == "other":
                                frame_has_other = True
                            elif class_name == "table":
                                frame_has_table = True
                    except Exception as e:
                        print("解析检测对象失败:", str(e))

            # 更新统计窗口
            if frame_has_person:
                detection_stats["person_count"] += 1
            if frame_has_book:
                detection_stats["book_detected"] = True
            if frame_has_other:
                detection_stats["other_detected"] = True
            if frame_has_table:
                detection_stats["table_detected"] = True

            # 绘制检测框
            for class_idx in range(1, len(labels)):
                class_name = labels[class_idx]
                class_detections = detections[class_idx] if class_idx < len(detections) else []
                for obj in class_detections:
                    try:
                        x, y, w, h = obj.x(), obj.y(), obj.w(), obj.h()
                        confidence = obj.output()

                        x, y, w, h = int(x), int(y), int(w), int(h)
                        if confidence >= confidence_threshold:
                            color_idx = class_idx - 1
                            color = colors[color_idx] if color_idx < len(colors) else (255, 255, 255)
                            img.draw_rectangle(x, y, w, h, color=color, thickness=2)
                            img.draw_string(x, y-12, f"{class_name}:{confidence:.2f}", color=color)
                    except Exception as e:
                        print("绘制检测框失败:", str(e))

            # 定时检查状态并发送数据
            if time.ticks_diff(current_time, detection_stats["last_reset"]) >= status_check_interval:
                time_elapsed = time.ticks_diff(current_time, detection_stats["last_reset"])

                if time_elapsed >= time_window:
                    person_count = detection_stats["person_count"]
                    book_detected = detection_stats["book_detected"]
                    other_detected = detection_stats["other_detected"]
                    table_detected = detection_stats["table_detected"]

                    # 根据检测结果确定状态码
                    if person_count >= person_threshold:
                        status = "1"
                    elif person_count < 2 and (book_detected or other_detected):
                        status = "2"
                    elif table_detected and not (book_detected or other_detected or person_count):
                        status = "4"
                    else:
                        status = "3"

                    # 生成发送数据
                    send_data = f"{SEAT_ID}:{status}"
                    try:
                        udp_socket.sendto(send_data.encode(), (DEST_IP, DEST_PORT))
                        print(f"[UDP发送] 座位{SEAT_ID} 状态码:{status} -> {DEST_IP}:{DEST_PORT}")
                    except Exception as send_err:
                        print("UDP发送失败:", str(send_err))

                    # 获取状态描述
                    status_desc = STATUS_DESCRIPTIONS.get(status, "未知状态")
                    log_msg = f"[状态更新] 座位ID:{SEAT_ID} | 状态码:{status} | 描述:{status_desc} | "
                    log_msg += f"Person:{person_count}, Book:{book_detected}, Other:{other_detected}, Table:{table_detected}"
                    print(log_msg)

                    # 记录当前状态
                    current_status = status

                    # 重置统计数据
                    detection_stats = {
                        "person_count": 0,
                        "book_detected": False,
                        "other_detected": False,
                        "table_detected": False,
                        "last_reset": current_time,
                        "last_status": status
                    }

        except Exception as e:
            print("检测异常:", str(e))

        gc.collect()

except Exception as e:
    print("系统错误:", str(e))
    while True:
        time.sleep(1)