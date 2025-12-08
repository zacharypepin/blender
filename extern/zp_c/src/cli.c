#include "zp_c/cli.h"
#include "zp_c/fatal.h"
#include <stdio.h>
#include <string.h>

// =========================================================================================================================================
// =========================================================================================================================================
// cli_parse_args_z: Parse command-line arguments in the format "key=value" and populate destination pointers. Validates inputs and checks for required arguments.
// =========================================================================================================================================
// =========================================================================================================================================
void cli_parse_args_z(int argc, char** argv, const cli_arg_zt* cli_args, size_t cli_arg_count, const char* usage_message)
{
    // =============================================================================================
    // =============================================================================================
    // Validate all input parameters and array elements.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(argv, "argv is null");
        fatal_check_z(cli_args, "cli_args is null");

        for (int i = 0; i < argc; i++)
        {
            fatal_check_z(argv[i], "argv element is null");
        }

        for (size_t i = 0; i < cli_arg_count; i++)
        {
            fatal_check_z(cli_args[i].name, "cli_arg name is null");
            fatal_check_z(cli_args[i].dest, "cli_arg dest is null");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Initialize all destination pointers to null.
    // =============================================================================================
    // =============================================================================================
    {
        for (size_t i = 0; i < cli_arg_count; i++)
        {
            *(cli_args[i].dest) = nullptr;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Parse each argument and match against known CLI arguments.
    // =============================================================================================
    // =============================================================================================
    {
        for (int arg_index = 1; arg_index < argc; arg_index++)
        {
            char* argument    = argv[arg_index];

            char* equals_sign = strchr(argument, '=');
            {
                if (equals_sign == nullptr || equals_sign == argument)
                {
                    if (usage_message != nullptr)
                    {
                        fprintf(stderr, "%s\n", usage_message);
                    }
                    fatal_z("unrecognized argument format");
                }
            }

            bool matched = false;
            {
                size_t key_len = (size_t)(equals_sign - argument);

                for (size_t cli_index = 0; cli_index < cli_arg_count; cli_index++)
                {
                    bool is_correct_length = strlen(cli_args[cli_index].name) == key_len;
                    bool is_correct_name   = strncmp(argument, cli_args[cli_index].name, key_len) == 0;

                    if (is_correct_length && is_correct_name)
                    {
                        *(cli_args[cli_index].dest) = equals_sign + 1;
                        matched                     = true;
                        break;
                    }
                }
            }

            if (!matched)
            {
                if (usage_message != nullptr)
                {
                    fprintf(stderr, "%s\n", usage_message);
                }
                fatal_z("unrecognized argument");
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Validate that all required arguments were provided.
    // =============================================================================================
    // =============================================================================================
    {
        for (size_t i = 0; i < cli_arg_count; i++)
        {
            if (cli_args[i].required && *(cli_args[i].dest) == nullptr)
            {
                if (usage_message != nullptr)
                {
                    fprintf(stderr, "%s\n", usage_message);
                }
                fatal_z("missing required argument");
            }
        }
    }
}
