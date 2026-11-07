#ifndef MAPS_TEST_SYS_H
#define MAPS_TEST_SYS_H
#define GLAZE_SYS_H
#include <glaze/abi.h>
uint32_t gz_time_ms(void);
int gz_log(const char *s, uint32_t n);
int gz_gps_io(uint32_t op, void *buffer, uint32_t size);
#endif
