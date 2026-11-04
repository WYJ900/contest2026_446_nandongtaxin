#pragma once
#include <stddef.h>
#include <sys/types.h>

ssize_t property_get_buffer(const char *key, void *data, size_t size);
int property_get_with_err(const char *key, char *value);
int property_set_buffer(const char *key, const void *data, size_t size);
int property_commit(void);
