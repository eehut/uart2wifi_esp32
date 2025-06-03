#ifndef __EXPORT_IDS_H__
#define __EXPORT_IDS_H__

/**
 * @file export_ids.h
 * @author LiuChuansen (179712066@qq.com)
 * @brief 导出ID定义
 * @version 0.1
 * @date 2025-05-24
 */

#ifdef __cplusplus
extern "C" {
#endif


enum GPIO_IDS {
    GPIO_SYS_LED = 0,
    GPIO_BUTTON,
};

enum UART_IDS {
    UART_PRIMARY = 0,
};



#ifdef __cplusplus
}
#endif

#endif // __EXPORT_IDS_H__
