
#include "debug_log.h"

#define LOG_ITEMS 256

#if DEBUG
static uint32_t log[LOG_ITEMS];
static uint32_t log_ptr;

void debug_log(uint32_t value)
{
	if (log_ptr >= LOG_ITEMS) return;

	log[log_ptr] = value;
	log_ptr++;

	return;
}

uint32_t debug_log_get(uint32_t **log_result)
{
	*log_result = log;
	return log_ptr;
}
#endif
