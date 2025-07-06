

#include "board.h"
#include <drv_lcd.h>  // 添加LCD驱动头文件

/* LCD引脚定义 */
#define LCD_RST_PIN       GET_PIN(E, 4)   // LCD复位引脚
#define LCD_RS_PIN        GET_PIN(E, 5)   // LCD数据/命令选择引脚
#define LCD_CS_PIN        GET_PIN(E, 3)   // LCD片选引脚
#define LCD_BL_PIN        GET_PIN(E, 6)   // LCD背光控制引脚

/* 初始化LCD引脚 */
static void lcd_gpio_init(void)
{
    /* 使能GPIO时钟 */
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* 配置LCD控制引脚 */
    rt_pin_mode(LCD_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_RS_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_CS_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_BL_PIN, PIN_MODE_OUTPUT);

    /* 设置初始状态 */
    rt_pin_write(LCD_RST_PIN, PIN_HIGH);
    rt_pin_write(LCD_RS_PIN, PIN_LOW);
    rt_pin_write(LCD_CS_PIN, PIN_HIGH);
    rt_pin_write(LCD_BL_PIN, PIN_HIGH);  // 打开背光
}

/* 配置LCD控制器 */
static void lcd_controller_config(void)
{
    /* 这里根据您的LCD型号添加特定的初始化序列 */
    /* 例如，对于ILI9341控制器： */

    /* 软复位 */
    rt_pin_write(LCD_RST_PIN, PIN_LOW);
    rt_thread_mdelay(50);
    rt_pin_write(LCD_RST_PIN, PIN_HIGH);
    rt_thread_mdelay(150);

    /* 发送初始化命令序列 */
    // lcd_write_cmd(0x01);  // 软件复位
    // rt_thread_mdelay(120);
    // lcd_write_cmd(0xCF);  // Power control A
    // lcd_write_data(0x00);
    // lcd_write_data(0xC1);
    // lcd_write_data(0x30);
    // ... 其他初始化命令 ...
}

/* 初始化LCD设备 */
void lcd_init(void)
{
    /* 初始化GPIO引脚 */
    lcd_gpio_init();

    /* 配置LCD控制器 */
    lcd_controller_config();

    /* 注册LCD设备到RT-Thread系统 */
    /* 这里需要根据RT-Thread的LCD驱动框架完成设备注册 */
    rt_kprintf("LCD initialized successfully.\n");
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /**Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /**Initializes the CPU, AHB and APB busses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /**Initializes the CPU, AHB and APB busses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}
