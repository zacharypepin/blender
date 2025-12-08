#include "zp_c/repl.h"

#include "zp_c/fatal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define INITIAL_COMMAND_CAPACITY 16
#define MAX_LINE_LENGTH          4096
#define MAX_TOKENS               256

struct repl_command_internal_zt
{
    char* name;
    char** aliases;
    size_t alias_count;
    repl_command_callback_t callback;
    void* user_data;
    char* description;
};

struct repl_zt
{
    struct repl_command_internal_zt* commands;
    size_t command_count;
    size_t command_capacity;
    char* prompt;
    char* banner;
    bool banner_shown;
};

// =========================================================================================================================================
// =========================================================================================================================================
// strcasecmp_z: Case-insensitive string comparison. Returns 0 if strings are equal, negative if s1 < s2, positive if s1 > s2.
// =========================================================================================================================================
// =========================================================================================================================================
static int strcasecmp_z(const char* s1, const char* s2)
{
    fatal_check_z(s1, "s1 is null");
    fatal_check_z(s2, "s2 is null");

    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
        {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

// =========================================================================================================================================
// =========================================================================================================================================
// strdup_z: Duplicate a string using malloc. Returns a newly allocated string that must be freed.
// =========================================================================================================================================
// =========================================================================================================================================
static char* strdup_z(const char* str)
{
    fatal_check_z(str, "str is null");
    size_t len = strlen(str);
    char* dup  = (char*)fatal_alloc_z(len + 1, "failed to allocate string");
    memcpy(dup, str, len + 1);
    return dup;
}

// =========================================================================================================================================
// =========================================================================================================================================
// find_command_z: Find a command by name (case-insensitive). Returns the command index or -1 if not found.
// =========================================================================================================================================
// =========================================================================================================================================
static int find_command_z(repl_zh repl, const char* name)
{
    fatal_check_z(repl, "repl is null");
    fatal_check_z(name, "name is null");

    for (size_t i = 0; i < repl->command_count; i++)
    {
        if (strcasecmp_z(repl->commands[i].name, name) == 0)
        {
            return (int)i;
        }
        for (size_t j = 0; j < repl->commands[i].alias_count; j++)
        {
            if (strcasecmp_z(repl->commands[i].aliases[j], name) == 0)
            {
                return (int)i;
            }
        }
    }
    return -1;
}

// =========================================================================================================================================
// =========================================================================================================================================
// free_command_z: Free all memory associated with a command including name, aliases, and description.
// =========================================================================================================================================
// =========================================================================================================================================
static void free_command_z(struct repl_command_internal_zt* cmd)
{
    if (cmd->name)
    {
        free(cmd->name);
    }
    if (cmd->aliases)
    {
        for (size_t i = 0; i < cmd->alias_count; i++)
        {
            if (cmd->aliases[i])
            {
                free(cmd->aliases[i]);
            }
        }
        free(cmd->aliases);
    }
    if (cmd->description)
    {
        free(cmd->description);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// register_command_internal_z: Internal helper to register a command into the REPL's command array. Expands array if necessary.
// =========================================================================================================================================
// =========================================================================================================================================
static void register_command_internal_z(repl_zh repl, const repl_command_zt* command)
{
    // =============================================================================================
    // =============================================================================================
    // Expand command array if necessary.
    // =============================================================================================
    // =============================================================================================
    {
        if (repl->command_count >= repl->command_capacity)
        {
            size_t new_capacity                           = repl->command_capacity * 2;
            struct repl_command_internal_zt* new_commands = (struct repl_command_internal_zt*)realloc(repl->commands, new_capacity * sizeof(struct repl_command_internal_zt));
            if (!new_commands)
            {
                fatal_z("failed to reallocate command array");
            }
            repl->commands         = new_commands;
            repl->command_capacity = new_capacity;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Copy command data and duplicate strings.
    // =============================================================================================
    // =============================================================================================
    struct repl_command_internal_zt* cmd = &repl->commands[repl->command_count];
    {
        cmd->name        = strdup_z(command->name);
        cmd->callback    = command->callback;
        cmd->user_data   = command->user_data;
        cmd->description = command->description ? strdup_z(command->description) : nullptr;

        cmd->alias_count = command->alias_count;
        if (command->alias_count > 0)
        {
            fatal_check_z(command->aliases, "aliases array is null");
            cmd->aliases = (char**)fatal_alloc_z(command->alias_count * sizeof(char*), "failed to allocate aliases array");
            for (size_t i = 0; i < command->alias_count; i++)
            {
                fatal_check_z(command->aliases[i], "alias is null");
                cmd->aliases[i] = strdup_z(command->aliases[i]);
            }
        }
        else
        {
            cmd->aliases = nullptr;
        }
    }

    repl->command_count++;
}

// =========================================================================================================================================
// =========================================================================================================================================
// repl_init_z: Initialize a new REPL instance with the provided config. Allocates and initializes the REPL structure, registers all commands.
// Prompt defaults to "> " if nullptr in config. Banner can be nullptr for no banner. Commands array can be nullptr if command_count is 0.
// =========================================================================================================================================
// =========================================================================================================================================
repl_zh repl_init_z(const repl_config_zt* config)
{
    // =============================================================================================
    // =============================================================================================
    // Validate config.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(config, "config is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Determine initial command capacity and allocate REPL structure.
    // =============================================================================================
    // =============================================================================================
    repl_zh repl;
    size_t initial_capacity;
    {
        initial_capacity       = config->command_count > INITIAL_COMMAND_CAPACITY ? config->command_count : INITIAL_COMMAND_CAPACITY;
        repl                   = (repl_zh)fatal_alloc_z(sizeof(struct repl_zt), "failed to allocate REPL");
        repl->commands         = (struct repl_command_internal_zt*)fatal_alloc_z(initial_capacity * sizeof(struct repl_command_internal_zt), "failed to allocate command array");
        repl->command_count    = 0;
        repl->command_capacity = initial_capacity;
        repl->prompt           = config->prompt ? strdup_z(config->prompt) : strdup_z("> ");
        repl->banner           = config->banner ? strdup_z(config->banner) : nullptr;
        repl->banner_shown     = false;
    }

    // =============================================================================================
    // =============================================================================================
    // Register all commands from config.
    // =============================================================================================
    // =============================================================================================
    {
        if (config->command_count > 0)
        {
            fatal_check_z(config->commands, "commands array is null");
            for (size_t i = 0; i < config->command_count; i++)
            {
                fatal_check_z(config->commands[i].name, "command name is null");
                fatal_check_z(config->commands[i].callback, "command callback is null");
                register_command_internal_z(repl, &config->commands[i]);
            }
        }
    }

    return repl;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_tokens_z: Parse a line into tokens separated by whitespace. Returns the number of tokens parsed.
// =========================================================================================================================================
// =========================================================================================================================================
static size_t parse_tokens_z(const char* line, char* tokens[MAX_TOKENS])
{
    fatal_check_z(line, "line is null");
    fatal_check_z(tokens, "tokens is null");

    size_t token_count = 0;
    const char* start  = nullptr;

    for (const char* p = line; *p && token_count < MAX_TOKENS; p++)
    {
        if (isspace((unsigned char)*p))
        {
            if (start != nullptr)
            {
                size_t len          = (size_t)(p - start);
                tokens[token_count] = (char*)fatal_alloc_z(len + 1, "failed to allocate token");
                memcpy(tokens[token_count], start, len);
                tokens[token_count][len] = '\0';
                token_count++;
                start = nullptr;
            }
        }
        else
        {
            if (start == nullptr)
            {
                start = p;
            }
        }
    }

    if (start != nullptr && token_count < MAX_TOKENS)
    {
        size_t len          = strlen(start);
        tokens[token_count] = (char*)fatal_alloc_z(len + 1, "failed to allocate token");
        memcpy(tokens[token_count], start, len + 1);
        token_count++;
    }

    return token_count;
}

// =========================================================================================================================================
// =========================================================================================================================================
// repl_update_z: Process one REPL iteration. Displays banner on first call if set, then reads one line from stdin, parses tokens, matches
// commands, and invokes callbacks. Returns true to continue, false to exit (when callback returns false or EOF is encountered).
// Automatically handles empty lines and unknown commands. Call this in a loop controlled by the caller.
// =========================================================================================================================================
// =========================================================================================================================================
bool repl_update_z(repl_zh repl)
{
    // =============================================================================================
    // =============================================================================================
    // Validate REPL state.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(repl, "repl is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Display banner on first call if set.
    // =============================================================================================
    // =============================================================================================
    {
        if (!repl->banner_shown && repl->banner)
        {
            printf("%s\n", repl->banner);
            repl->banner_shown = true;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Check if input is available on stdin without blocking. If no input is available, return
    // immediately to allow the caller to continue with other work (e.g., updating editor core).
    // =============================================================================================
    // =============================================================================================
    {
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        timeout.tv_sec      = 0;
        timeout.tv_usec     = 0;

        int select_result   = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout);
        bool no_input_avail = select_result <= 0 || !FD_ISSET(STDIN_FILENO, &read_fds);
        if (no_input_avail)
        {
            return true;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Input is available, read one line from stdin, parse tokens, match commands, and invoke callbacks.
    // =============================================================================================
    // =============================================================================================
    {
        printf("%s", repl->prompt);
        fflush(stdout);

        char line[MAX_LINE_LENGTH];
        if (!fgets(line, sizeof(line), stdin))
        {
            return false;
        }

        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n')
        {
            line[line_len - 1] = '\0';
            line_len--;
        }

        if (line_len == 0)
        {
            return true;
        }

        char* tokens[MAX_TOKENS];
        size_t token_count = parse_tokens_z(line, tokens);

        if (token_count == 0)
        {
            return true;
        }

        int cmd_index = find_command_z(repl, tokens[0]);
        if (cmd_index >= 0)
        {
            const char* const* token_ptrs = (const char* const*)tokens;
            bool should_continue          = repl->commands[cmd_index].callback(token_count, token_ptrs, repl->commands[cmd_index].user_data);
            for (size_t i = 0; i < token_count; i++)
            {
                free(tokens[i]);
            }
            return should_continue;
        }
        else
        {
            printf("Unknown command: %s\n", tokens[0]);
            printf("Type 'help' for available commands.\n");
            for (size_t i = 0; i < token_count; i++)
            {
                free(tokens[i]);
            }
            return true;
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// repl_destroy_z: Destroy a REPL instance and free all associated memory including registered commands.
// =========================================================================================================================================
// =========================================================================================================================================
void repl_destroy_z(repl_zh repl)
{
    // =============================================================================================
    // =============================================================================================
    // Validate REPL state.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(repl, "repl is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Free all commands and their associated memory.
    // =============================================================================================
    // =============================================================================================
    {
        for (size_t i = 0; i < repl->command_count; i++)
        {
            free_command_z(&repl->commands[i]);
        }
        free(repl->commands);
    }

    // =============================================================================================
    // =============================================================================================
    // Free prompt, banner, and REPL structure.
    // =============================================================================================
    // =============================================================================================
    {
        if (repl->prompt)
        {
            free(repl->prompt);
        }
        if (repl->banner)
        {
            free(repl->banner);
        }
        free(repl);
    }
}
