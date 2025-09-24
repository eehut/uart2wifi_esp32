/**
 * @file ext_gpio.c
 * @author Samuel (179712066@qq.com)
 * @brief GPIO扩展组件
 * @version 0.1
 * @date 2025-05-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "esp_err.h"
#include "ext_gpio_type.h"
#include "ext_gpio_drive.h"
#include "ext_gpio_event.h"
#include "ext_gpio.h"

#include "app_event_loop.h"

#include "uptime.h"

#include "esp_log.h"

static const char *TAG = "ext_gpio";

/**
 * @brief 工作周期，单位为毫秒
 * 
 */
#define CONFIG_EXT_GPIO_WORK_PERIOD_MS  10


/**
 * @brief 控制间隔，单位为毫秒
 * 
 */
#ifndef CONFIG_EXT_GPIO_CONTROL_SLOT_MS
#define CONFIG_EXT_GPIO_CONTROL_SLOT_MS  100
#endif


/**
 * @brief 这是一个GPIO的控制实例
 * 
 */
typedef struct 
{
    const ext_gpio_config_t *config;

    /// 对于闪烁控制来说，
    ///    每一个位表示IO的状态，0 表示关闭，1-表示开启
    /// 
    /// 对于呼吸显示模式，
    ///    [7：0] 表示从0到最亮的过渡时间，最大值255, 单位为100ms
    ///    [15:8] 表示从最亮到灭的过渡时间,最大值255, 单位为100ms
    ///    [27:16] 表示控制索引, 最高时间为25s,如果是10ms控制间隔，需要2500个级别
    ///    [31:28] 预留不使用
    ///   过渡时间最小值不小于100ms
    ///   过渡时间最大值设定为25000ms, 10ms/16ms控制间隔
    ///   有控制周期为p, 索引为index, 求解当次PWM的duty值
    ///     onTransitionTime = [7:0] * 100
    ///     offTransitionTime = [15:8] * 100
    ///     index = [27:16]
    ///     上升阶段:
    ///     if (index * p < onTransitionTime) // => (index * p /100) < [7:0]
    ///     {  
    ///          duty = index * p * 100 / onTransitionTime;
    ///          index ++;
    ///     }
    ///     duty = index * p * 100 / onTransitionTime;
    ///     可以转换为
    ///     duty = index * p / [7:0]
    ///     先用16ms的间隔看看

    uint32_t control;
    /// 用作闪烁或呼吸模式的索引
    uint8_t control_index;   
    /// 控制位：0-32 表示IO闪烁控制
    /// 33 - 表示呼吸显示模式，仅PWM IO支持
    /// > 34 - 保留
    uint8_t control_bits;
    ///重复控制，0 － 表示一直重复，其他值表示重复次数
    uint8_t cycle;  
    /// GPIO的输入输出值
    uint8_t value;
    /// 控制TICK
    sys_tick_t control_tick;
}gpio_manipulate_t; 


/**
 * @brief 按钮状态
 * 
 */
typedef enum {
    _BUTTON_STATE_IDLE = 0,
    _BUTTON_STATE_PRESSED_DEBOUNCE = 1,
    _BUTTON_STATE_PRESSED = 2,
    _BUTTON_STATE_RELEASED_DEBOUNCE = 3,
    _BUTTON_STATE_RELEASED = 4,
    _BUTTON_STATE_END = 5,
}button_state_t;


/// 默认的去抖时间，仅用于按下去抖，后续有必要考虑提供API，可设置每按键不同参数
#ifndef CONFIG_EXT_BUTTON_DEFAULT_DEBOUNCE_MS
#define CONFIG_EXT_BUTTON_DEFAULT_DEBOUNCE_MS              10
#endif 

/// 默认的按键连击超时时间，如果该时间内再次出现按下事件，连击次数增加，否则，连击次数复位
#ifndef CONFIG_EXT_BUTTON_DEFAULT_CONTINUE_CLICK_EXPIRED_MS
#define CONFIG_EXT_BUTTON_DEFAULT_CONTINUE_CLICK_EXPIRED_MS    500
#endif 

/// 默认的进入长按状态的时间，可通过API修改
#ifndef CONFIG_EXT_BUTTON_DEFAULT_LONG_PRESSED_SECOND
#define CONFIG_EXT_BUTTON_DEFAULT_LONG_PRESSED_SECOND      3
#endif 



/**
 * @brief 这是一个按键的控制实例
 *  按键是GPIO实例的子集
 * @details
 *  按键只有两种状态，按下或释放
 *  按键状态切换时，需要去抖，引入一个标志控制去抖，去抖需要一个时间参数
 *  按键由释放进入按下状态时，启动长按计数器，如果到达长按计数器仍未释放，产生长按事件
 *  按键由按下切换为释放状态时，启动连击计数器，如果连击计数器未溢出时，再次产生按下事件，则连击次数增加，否则，连击次数复位
 */
typedef struct 
{
    uint16_t id;

    uint8_t valid;
    uint8_t state;
    uint8_t click_count;
    
    uint16_t long_pressed;
    uint16_t debounce_time; // ms
    
    sys_tick_t expired;
    sys_tick_t long_expired;
    sys_tick_t click_expired;
}button_manipulate_t;


static gpio_manipulate_t s_gpios[CONFIG_EXT_GPIO_MAX_NUM] = { 0 };

#if (CONFIG_EXT_GPIO_CACHE_SIZE > 0)
static uint16_t s_gpio_offsets[CONFIG_EXT_GPIO_CACHE_SIZE] = { 0 };
#endif

#if (CONFIG_EXT_BUTTON_MAX_NUM > 0)
static button_manipulate_t s_buttons[CONFIG_EXT_BUTTON_MAX_NUM] = { 0 };
#endif


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

/**
 * @brief 初始化GPIO缓存为无效索引
 * 
 */
static void _gpio_init(void)
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }

    memset(s_gpios, 0, sizeof(s_gpios));

    #if (CONFIG_EXT_GPIO_CACHE_SIZE > 0)
    for (int i = 0; i < ARRAY_SIZE(s_gpio_offsets); i ++ )
    {
        s_gpio_offsets[i] = 0xffff;
    }
    #endif

    #if (CONFIG_EXT_BUTTON_MAX_NUM > 0)
    memset(s_buttons, 0, sizeof(s_buttons));
    #endif

    ESP_LOGD(TAG, "ext_gpio init done");

    initialized = true;
}


/**
 * @brief 获取一个空闲的GPIO实例
 * 
 * @return gpio_manipulate_t* 
 */
static inline gpio_manipulate_t * _gpio_free_instance(int *offset)
{
    for (int i = 0; i < ARRAY_SIZE(s_gpios); i ++ )
    {
        if (s_gpios[i].config == NULL)
        {
            if (offset != NULL)
            {
                *offset = i;
            }
            return &s_gpios[i];
        }
    }

    return NULL;
}

/**
 * @brief 获取GPIO实例
 * @param id 
 * @return gpio_manipulate_t* 
 */
static gpio_manipulate_t * _gpio_instance(uint16_t id)
{
    int offset = -1;
    #if (CONFIG_EXT_GPIO_CACHE_SIZE > 0)
    if (id < ARRAY_SIZE(s_gpio_offsets))
    {
        offset = s_gpio_offsets[id];
    }
    #endif

    if (offset < 0)
    {
        // 不在缓存中，需要重新查找
        for (int i = 0; i < ARRAY_SIZE(s_gpios); i ++ )
        {
            if (s_gpios[i].config && s_gpios[i].config->id == id)
            {
                return &s_gpios[i];
            }
        }
    }
    else if (offset < ARRAY_SIZE(s_gpios))
    {
        return &s_gpios[offset];
    }
    
    return NULL;
}




/**
 * @brief configure the ext_gpios, can be called in multiple times
 * 
 * @param configs configs array of ext_gpio_config_t
 * @param num number of configs
 * @return int 
 */
int ext_gpio_config(const ext_gpio_config_t *configs, int num)
{
    int count = 0;
    int button_count = 0;

    if (configs == NULL || num <= 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    _gpio_init();

    for (int i = 0; i < num; i ++ )
    {
        int offset = -1;
        gpio_manipulate_t *gpio = _gpio_instance(configs[i].id);
        if (gpio != NULL)
        {
            ESP_LOGW(TAG, "gpio.%d already registered with (chip:%d, pin:%d)", configs[i].id, configs[i].chip, configs[i].pin);
            continue;
        }

        gpio = _gpio_free_instance(&offset);
        if (gpio == NULL)
        {
            ESP_LOGE(TAG, "No free instance for gpio<%s>", configs[i].name);
            return ESP_ERR_NO_MEM;
        }

        gpio->config = &configs[i];

        // set gpio offset 
        #if (CONFIG_EXT_GPIO_CACHE_SIZE > 0)
        if (configs[i].id < ARRAY_SIZE(s_gpio_offsets))
        {
            s_gpio_offsets[configs[i].id] = offset;
            ESP_LOGD(TAG, "gpio<%s> cache offset: %d", configs[i].name, offset);
        }
        #endif

        ext_gpio_low_level_config(&configs[i]);

        if (configs[i].flags & _GPIO_FLAG_BUTTON)
        {
            #if (CONFIG_EXT_BUTTON_MAX_NUM > 0)
            int k = 0;
            for (k = 0; k < ARRAY_SIZE(s_buttons); k ++ )
            {
                if (s_buttons[k].valid == 0)
                {
                    s_buttons[k].valid = 1;
                    s_buttons[k].id = configs[i].id;
                    s_buttons[k].debounce_time = CONFIG_EXT_BUTTON_DEFAULT_DEBOUNCE_MS;
                    s_buttons[k].state = _BUTTON_STATE_IDLE;

                    button_count ++;

                    ESP_LOGI(TAG, "gpio<%s> is a button", configs[i].name);
                    break;
                }
            }
            #else 
            ESP_LOGW(TAG, "Few gpio has flags for button, but codes for button are not enabled");
            #endif          
        }

        count ++;
    }

    ESP_LOGI(TAG, "Total %d gpio(s) and %d button(s) registered", count, button_count);

    return 0;
}   


/**
 * @brief 获取GPIO的名称
 * 
 * @param id 
 * @return const char* 
 */
const char * ext_gpio_name(uint16_t id)
{
    gpio_manipulate_t *gpio = _gpio_instance(id);
    if (gpio == NULL){
        return "n/a";
    }

    return gpio->config->name;
}


/**
 * @brief 设定GPIO为指定的状态, 0 - 低电平, 1 - 高电平, 将自动关闭GPIO的自动控制
 * 
 * @param id 
 * @param value 
 * @return int 
 */
int ext_gpio_set(uint16_t id, int value)
{
    gpio_manipulate_t *gpio = _gpio_instance(id);
    if (gpio == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    int ret = ext_gpio_low_level_set(gpio->config, value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    gpio->control_bits = 0;
    gpio->value = value;

    return ret;
}

int ext_gpio_revert(uint16_t id)
{
    gpio_manipulate_t *gpio = _gpio_instance(id);
    if (gpio == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    int value = !gpio->value;
    int ret = ext_gpio_low_level_set(gpio->config, value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    gpio->control_bits = 0;
    gpio->value = value;

    return ret;
}


/** 
 * @brief GPIO顶层控制函数
 * 
 * @param id GPIO逻辑索引
 * @param control  GPIO控制位，与GPIO控制的SLOT时间一起作用
 * @param bits  GPIO有效控制位，0-1， 所有位有效，2 - control[0:1]有效，3 - control[0:2]有效 ...
 * @param cycle GPIO控制循环次数， 0 - 一直循环，1 - 循环一次，2 - 循环两次 ...
 * @return int 
 */
int ext_gpio_control(uint16_t id, uint32_t control, uint8_t bits, uint8_t cycle)
{
    gpio_manipulate_t *gpio = _gpio_instance(id);
    if (gpio == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    gpio->control = control;
    gpio->control_bits = bits <= 1 ? sizeof(control) * 8 : bits;
    // reset control index
    gpio->control_index = 0;
    gpio->cycle = cycle;
    gpio->control_tick = uptime();
    return 0;
}

/**
 * @brief 获取GPIO的值, 为了更方便使用,它不返回错误值, 所有错误都返回0
 * 
 */
int ext_gpio_get(uint16_t id)
{
    gpio_manipulate_t *gpio = _gpio_instance(id);
    if (gpio == NULL)
    {
        ESP_LOGD(TAG, "ext_gpio_get: gpio.%d not found", id);
        return 0;
    }

    int value = 0;
    int ret = ext_gpio_low_level_get(gpio->config, &value);
    if (ret != ESP_OK)
    {
        ESP_LOGD(TAG, "ext_gpio_get: gpio.%d get value failed", id);
        return 0;
    }

    return value;
}

/**
 * @brief 设置LED为指定状态
 * 
 * @param id 
 * @param on 
 * @return int 
 */
int ext_led_set(uint16_t id, bool on)
{
    return ext_gpio_set(id, on);
}

/**
 * @brief 设置LED为闪烁状态
 * 
 * @param id 
 * @param control 
 * @param mask 从0位开始算, 至少两个1起作用,否则当作所有control位有效
 * @return int 
 */
int ext_led_flash(uint16_t id, uint32_t control, uint32_t mask)
{
    uint8_t bits = 0;
    for (int i = 0; i < sizeof(control) * 8; i ++ )
    {
        // 只允许连续的1
        if (mask & (1 << i)){
            bits ++;
        } else {
            break;
        }
    }
    return ext_gpio_control(id, control, bits, 0);
}


/**
 * @brief GPIO输出控制
 * 
 */
static inline void _gpio_output_control(gpio_manipulate_t *gpio)
{
    int v = gpio->control & (1 << gpio->control_index) ? 1 : 0;

    ext_gpio_low_level_set(gpio->config, v);

    gpio->control_index ++;
    if (gpio->control_index >= gpio->control_bits)
    {
        gpio->control_index = 0;

        if (gpio->cycle > 0)
        {
            gpio->cycle --;

            if (gpio->cycle == 0)
            {
                gpio->control_bits = 0;
            }
        }
    }
}

#if (CONFIG_EXT_BUTTON_MAX_NUM > 0)
static inline void _button_state_machine(button_manipulate_t *button)
{

    bool pressed = ext_gpio_get(button->id);
    const char *gpio_name = ext_gpio_name(button->id);
    sys_tick_t now = uptime();

    switch (button->state)
    {
        case _BUTTON_STATE_IDLE:
            if (pressed){
                button->state = _BUTTON_STATE_PRESSED_DEBOUNCE;
                button->expired = now + button->debounce_time;
            } else {
                if (button->click_count > 0 && uptime_after(now, button->click_expired))
                {
                    // 发送连击停止消息
                    ext_gpio_send_button_event(button->id, gpio_name, 
                                              EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK, 
                                              button->click_count, 0);

                    button->click_count = 0;
                    ESP_LOGD(TAG, "button<%s> continue-click stop", gpio_name);
                }
            }
            break;
        case _BUTTON_STATE_PRESSED_DEBOUNCE:
            if (!pressed)
            {
                button->state = _BUTTON_STATE_IDLE;
                break;
            }

            // 去拌时间是否到了
            if (uptime_after(now, button->expired))
            {
                button->state = _BUTTON_STATE_PRESSED;
                button->long_expired = now + (CONFIG_EXT_BUTTON_DEFAULT_LONG_PRESSED_SECOND * 1000);
                button->long_pressed = 0;

                button->click_count ++;
                button->click_expired = now + CONFIG_EXT_BUTTON_DEFAULT_CONTINUE_CLICK_EXPIRED_MS;

                // 发送按下消息
                ext_gpio_send_button_event(button->id, gpio_name, 
                                          EXT_GPIO_EVENT_BUTTON_PRESSED, 
                                          button->click_count, 0);
                
                ESP_LOGD(TAG, "button<%s> pressed(%d)", gpio_name, button->click_count);
            }
            break;
        case _BUTTON_STATE_PRESSED:
            // 在按住状态下，如果GPIO一直按下，需要检测长按是否到达，长按到达后，设置长按状态
            if (pressed)
            {
                if (!button->long_pressed && uptime_after(now, button->long_expired))
                {
                    button->long_pressed = CONFIG_EXT_BUTTON_DEFAULT_LONG_PRESSED_SECOND;
                    button->click_count = 0;
                    // 长按时间间隔为1秒
                    button->expired = now + 1000;

                    // 发送长按消息
                    ext_gpio_send_button_event(button->id, gpio_name, 
                                              EXT_GPIO_EVENT_BUTTON_LONG_PRESSED, 
                                              0, button->long_pressed);
                    
                    ESP_LOGD(TAG, "button<%s> long pressed(%d)", gpio_name, button->long_pressed);
                }
                else if (button->long_pressed > 0 && uptime_after(now, button->expired))
                {
                    button->long_pressed ++;
                    button->expired = now + 1000;

                    // 发送长按消息
                    ext_gpio_send_button_event(button->id, gpio_name, 
                                              EXT_GPIO_EVENT_BUTTON_LONG_PRESSED, 
                                              0, button->long_pressed);
                    
                    ESP_LOGD(TAG, "button<%s> long pressed(%d)", gpio_name, button->long_pressed);
                }
            }
            else 
            {
                button->state = _BUTTON_STATE_RELEASED_DEBOUNCE;
                button->expired = now + 100;// 使用了100ms的释放去抖，防止误双击
            }
            break;
        case _BUTTON_STATE_RELEASED_DEBOUNCE:
            if (pressed)
            {
                button->state = _BUTTON_STATE_PRESSED;
                break;
            }

            if (uptime_after(now, button->expired))
            {
                button->state = _BUTTON_STATE_RELEASED;

                // 发送释放消息
                ext_gpio_send_button_event(button->id, gpio_name, 
                                          EXT_GPIO_EVENT_BUTTON_RELEASED, 
                                          button->click_count, button->long_pressed);
                
                ESP_LOGD(TAG, "button<%s> released", gpio_name);
            }
            break;
        case _BUTTON_STATE_RELEASED:
            // UCT to idle
            button->state = _BUTTON_STATE_IDLE;
            break;
        case _BUTTON_STATE_END:
            // 结束状态
            if (!pressed)
            {
                button->state = _BUTTON_STATE_IDLE;
            }
            break;
        default:
            break;
    }
}
#endif // CONFIG_EXT_BUTTON_MAX_NUM > 0


/**
 * @brief GPIO主循环
 * 
 */
static void ext_gpio_main_loop(void *pvParameters)
{
    while (true)
    {
        for (int i = 0; i < ARRAY_SIZE(s_gpios); i ++ )
        {
            if (s_gpios[i].config == NULL)
            {
                continue;
            }

            // 按钮或不是输出口,跳过
            if ((s_gpios[i].config->flags & _GPIO_FLAG_BUTTON) 
                || ((s_gpios[i].config->flags & _GPIO_FLAG_OUTPUT) == 0))
            {
                continue;
            }

            sys_tick_t now = uptime();

            if ((s_gpios[i].control_bits > 0) && uptime_after(now, s_gpios[i].control_tick))
            {
                _gpio_output_control(&s_gpios[i]);
                s_gpios[i].control_tick = now + CONFIG_EXT_GPIO_CONTROL_SLOT_MS;
            }
        }

    #if (CONFIG_EXT_BUTTON_MAX_NUM > 0)
        for (int i = 0; i < ARRAY_SIZE(s_buttons); i ++ )
        {
            if (s_buttons[i].valid == 0)
            {
                continue;
            }

            _button_state_machine(&s_buttons[i]);
        }
    #endif

        mdelay(5);
    }

}

/**
 * @brief 启动GPIO 任务
 * 
 * @return int 
 */
int ext_gpio_start(void)
{
    // 初始化
    _gpio_init();

    // 启动GPIO主循环
    xTaskCreate(ext_gpio_main_loop, "ext_gpio", 2048, NULL, 5, NULL);

    return 0;
}

