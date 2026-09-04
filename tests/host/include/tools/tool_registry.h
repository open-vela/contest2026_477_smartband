#pragma once

#include <stddef.h>

int tool_registry_execute(const char *name, const char *input_json,
                          char *output, size_t output_size);
