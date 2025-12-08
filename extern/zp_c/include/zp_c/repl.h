#pragma once

#include <stddef.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

typedef struct repl_zt* repl_zh;

// =========================================================================================================================================
// =========================================================================================================================================
// repl_command_callback_t: Callback function type for REPL commands. Receives token count, array of token strings, and user data pointer.
// Returns true to continue the REPL loop, false to exit.
// =========================================================================================================================================
// =========================================================================================================================================
typedef bool (*repl_command_callback_t)(size_t token_count, const char* const* tokens, void* user_data);

typedef struct repl_command_zt
{
    const char* name;
    const char* const* aliases;
    size_t alias_count;
    repl_command_callback_t callback;
    void* user_data;
    const char* description;
} repl_command_zt;

typedef struct repl_config_zt
{
    const char* prompt;
    const char* banner;
    const repl_command_zt* commands;
    size_t command_count;
} repl_config_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// repl_init_z: Initialize a new REPL instance with the provided config. Allocates and initializes the REPL structure, registers all commands.
// Prompt defaults to "> " if nullptr in config. Banner can be nullptr for no banner. Commands array can be nullptr if command_count is 0.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C repl_zh repl_init_z(const repl_config_zt* config);

// =========================================================================================================================================
// =========================================================================================================================================
// repl_update_z: Process one REPL iteration. Displays banner on first call if set, then checks if input is available on stdin. If input is
// available, reads one line, parses tokens, matches commands, and invokes callbacks. If no input is available, returns immediately without
// blocking. Returns true to continue, false to exit (when callback returns false or EOF is encountered). Automatically handles empty lines
// and unknown commands.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C bool repl_update_z(repl_zh repl);

// =========================================================================================================================================
// =========================================================================================================================================
// repl_destroy_z: Destroy a REPL instance and free all associated memory including registered commands.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void repl_destroy_z(repl_zh repl);
