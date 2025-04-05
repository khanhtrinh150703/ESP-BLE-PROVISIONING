#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>

// Khai báo kiểu dữ liệu
typedef struct {
    void (*handle)(void); // Con trỏ hàm không nhận tham số
} CommandStrategy;

typedef struct {
    const char *key;
    CommandStrategy strategy;
} HashEntry;

// Khai báo các hàm public
void handle_led_command(const char *data, int len);
void handle_erase_wifi(void);

#endif // COMMAND_HANDLER_H