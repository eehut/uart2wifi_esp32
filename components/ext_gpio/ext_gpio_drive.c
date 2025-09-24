

/**
 * @file ext_gpio_drive.c
 * @author Samuel (179712066@qq.com)
 * @brief low level functions for ext_gpio
 * @version 0.1
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "ext_gpio_drive.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "ext_gpio";

// esp32 not return output value, so we need to cache the output value
static uint64_t s_soc_gpio_output_cache = 0;

/**
 * @brief 初始化GPIO
 * 
 * @param gpio 
 * @return int 
 */
int ext_gpio_low_level_config(const ext_gpio_config_t *gpio)
{
    if (gpio == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "gpio<%s>: id:%d, chip:%d, pin:%d, %s, flags:0x%04x", gpio->name, gpio->id, gpio->chip, gpio->pin, 
        (gpio->flags & _GPIO_FLAG_OUTPUT) ? "out" : "in", gpio->flags);

    switch (gpio->chip)
    {
        case _GPIO_CHIP_SOC:
        {
            gpio_config_t config = {0};

            // check gpio pin if valid
            if (/*gpio->pin < GPIO_NUM_0 ||*/ gpio->pin >= GPIO_NUM_MAX)
            {
                ESP_LOGE(TAG, "gpio<%s>: pin %d on soc is invalid", gpio->name, gpio->pin);
                return ESP_ERR_INVALID_ARG;
            }

            // reset pin
            gpio_reset_pin(gpio->pin);

            // set gpio mode
            config.pin_bit_mask = (1ULL << gpio->pin);

            // if this gpio configured as button, we need to force to input mode
            if ((gpio->flags & _GPIO_FLAG_BUTTON) && (gpio->flags & _GPIO_FLAG_OUTPUT))
            {
                ESP_LOGW(TAG, "gpio<%s>: marked as button and output enabled, force to input mode", gpio->name);
                config.mode = GPIO_MODE_INPUT;
            }
            else
            {
                config.mode = (gpio->flags & _GPIO_FLAG_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
            }
            
            config.pull_up_en = (gpio->flags & _GPIO_FLAG_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
            config.pull_down_en = (gpio->flags & _GPIO_FLAG_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
            config.intr_type = GPIO_INTR_DISABLE;

            // set gpio config
            esp_err_t ret = gpio_config(&config);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "gpio<%s>: failed to config soc pin(%d): %s", gpio->name, gpio->pin, esp_err_to_name(ret));
                return ret;
            }

            // set initial value
            if (gpio->flags & _GPIO_FLAG_OUTPUT) 
            {
                bool active = gpio->flags & _GPIO_FLAG_INIT_ACTIVE;
                if (gpio->flags & _GPIO_FLAG_ACTIVE_LOW){
                    active = !active;
                }
                ESP_LOGD(TAG, "gpio<%s>: set initial %s: %d", gpio->name, (gpio->flags & _GPIO_FLAG_INIT_ACTIVE) ? "active" : "inactive", active);
                gpio_set_level(gpio->pin, active);
            }
        }
        break;
        case _GPIO_CHIP_PWM:
        return ESP_ERR_NOT_SUPPORTED;
        default:
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief 设置GPIO的输出值
 * 
 * @param gpio 
 * @param value 
 */
int ext_gpio_low_level_set(const ext_gpio_config_t *gpio, int value)
{
    if (gpio == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (gpio->chip)
    {
        case _GPIO_CHIP_SOC:
        {
            uint32_t level = (gpio->flags & _GPIO_FLAG_ACTIVE_LOW) ? !value : value;
            if (level){
                s_soc_gpio_output_cache |= (1ULL << gpio->pin);
            }else{
                s_soc_gpio_output_cache &= ~(1ULL << gpio->pin);
            }
            return gpio_set_level(gpio->pin, level);
        }
        case _GPIO_CHIP_PWM:
        {
            return ESP_ERR_NOT_SUPPORTED;
        }
        default:
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief 获取GPIO的输入值
 * 
 * @param gpio 
 * @return int 
 */
int ext_gpio_low_level_get(const ext_gpio_config_t *gpio, int *value)
{
    if (gpio == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (gpio->chip)
    {
        case _GPIO_CHIP_SOC:
        {
            if (gpio->flags & _GPIO_FLAG_OUTPUT){
                *value = (s_soc_gpio_output_cache & (1ULL << gpio->pin)) ? 1 : 0;
            } else {
                int raw_level = gpio_get_level(gpio->pin);
                *value = (gpio->flags & _GPIO_FLAG_ACTIVE_LOW) ? !raw_level : raw_level;
            }
            return ESP_OK;
        }
        case _GPIO_CHIP_PWM:
        {
            return ESP_ERR_NOT_SUPPORTED;
        }
        default:
        return ESP_ERR_INVALID_ARG;
    }
}

