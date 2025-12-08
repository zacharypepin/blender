#pragma once

#include <stddef.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

typedef struct cli_arg_zt
{
    const char* name;
    char** dest;
    bool required;
} cli_arg_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// cli_parse_args_z: Parse command-line arguments in the format "key=value" and populate destination pointers. Validates inputs and checks for required arguments.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void cli_parse_args_z(int argc, char** argv, const cli_arg_zt* cli_args, size_t cli_arg_count, const char* usage_message);
