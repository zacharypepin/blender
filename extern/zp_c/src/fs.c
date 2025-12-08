
#include "zp_c/fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "zp_c/hash.h"
#include "zp_c/fatal.h"

// =========================================================================================================================================
// =========================================================================================================================================
// fs_is_valid_dir_z: Check if a path exists and is a valid directory. Returns false if path is null, empty, doesn't exist, or is not a directory.
// =========================================================================================================================================
// =========================================================================================================================================
bool fs_is_valid_dir_z(const char* path)
{
    {
        if (path == nullptr || path[0] == '\0')
        {
            return false;
        }
    }

    {
        struct stat path_stats;

        if (stat(path, &path_stats) != 0)
        {
            return false;
        }

        if (!S_ISDIR(path_stats.st_mode))
        {
            return false;
        }
    }

    return true;
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_mkdir_z: Create a directory and all parent directories recursively. Uses a scratch arena for temporary path manipulation.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_mkdir_z(const char* path, mode_t mode)
{
    // =============================================================================================
    // =============================================================================================
    // Validate path input.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(path, "path is null");
        if (path[0] == '\0')
        {
            fatal_z("path is empty");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Create scratch arena and copy path for manipulation.
    // =============================================================================================
    // =============================================================================================
    arena_zh scratch;
    char* path_copy;
    {
        scratch         = arena_init_z(strlen(path) + 256);

        size_t path_len = strlen(path);
        span_zh span    = arena_alloc_z(scratch, path_len + 1U);
        path_copy       = (char*)span->data;
        memcpy(path_copy, path, path_len + 1U);
    }

    // =============================================================================================
    // =============================================================================================
    // Create parent directories and final directory.
    // =============================================================================================
    // =============================================================================================
    {
        for (char* p = path_copy + 1; *p != '\0'; p++)
        {
            if (*p == '/')
            {
                *p = '\0';

                if (mkdir(path_copy, mode) != 0 && errno != EEXIST)
                {
                    arena_destroy_z(scratch);
                    fatal_z("failed to create parent directory");
                }

                *p = '/';
            }
        }

        if (mkdir(path_copy, mode) != 0 && errno != EEXIST)
        {
            arena_destroy_z(scratch);
            fatal_z("failed to create directory");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Cleanup scratch arena.
    // =============================================================================================
    // =============================================================================================
    {
        arena_destroy_z(scratch);
    }

    // =============================================================================================
    // =============================================================================================
    // Verify created directory is valid.
    // =============================================================================================
    // =============================================================================================
    {
        if (!fs_is_valid_dir_z(path))
        {
            fatal_z("created path is not a valid directory");
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_count_files_recursive_z: Count the number of regular files in a directory tree recursively. Returns false on error.
// =========================================================================================================================================
// =========================================================================================================================================
static bool fs_count_files_recursive_z(const char* base_dir, const char* relative_dir, arena_zh scratch, size_t* p_count)
{
    // =============================================================================================
    // =============================================================================================
    // Build full directory path from base and relative paths.
    // =============================================================================================
    // =============================================================================================
    size_t base_len;
    size_t relative_len;
    size_t dir_path_len;
    char* dir_path;
    {
        base_len              = strlen(base_dir);
        relative_len          = strlen(relative_dir);
        dir_path_len          = base_len + (relative_len > 0U ? 1U + relative_len : 0U);
        span_zh dir_path_span = arena_alloc_z(scratch, dir_path_len + 1U);
        dir_path              = (char*)dir_path_span->data;

        if (relative_len > 0U)
        {
            (void)snprintf(dir_path, dir_path_len + 1U, "%s/%s", base_dir, relative_dir);
        }
        else
        {
            (void)snprintf(dir_path, dir_path_len + 1U, "%s", base_dir);
        }
    }

    DIR* dir_handle;
    bool count_ok;
    {
        dir_handle = opendir(dir_path);

        if (dir_handle == nullptr)
        {
            return false;
        }

        count_ok = true;

        struct dirent* p_entry;
        while ((p_entry = readdir(dir_handle)) != nullptr)
        {
            if ((strcmp(p_entry->d_name, ".") == 0) || (strcmp(p_entry->d_name, "..") == 0))
            {
                continue;
            }

            size_t child_relative_len   = (relative_len > 0U ? relative_len + 1U : 0U) + strlen(p_entry->d_name);
            span_zh child_relative_span = arena_alloc_z(scratch, child_relative_len + 1U);
            char* child_relative        = (char*)child_relative_span->data;

            if (relative_len > 0U)
            {
                (void)snprintf(child_relative, child_relative_len + 1U, "%s/%s", relative_dir, p_entry->d_name);
            }
            else
            {
                (void)snprintf(child_relative, child_relative_len + 1U, "%s", p_entry->d_name);
            }

            size_t child_full_len   = base_len + 1U + child_relative_len;
            span_zh child_full_span = arena_alloc_z(scratch, child_full_len + 1U);
            char* child_full_path   = (char*)child_full_span->data;

            (void)snprintf(child_full_path, child_full_len + 1U, "%s/%s", base_dir, child_relative);

            struct stat entry_stats;
            if (stat(child_full_path, &entry_stats) != 0)
            {
                continue;
            }

            if (S_ISDIR(entry_stats.st_mode))
            {
                if (!fs_count_files_recursive_z(base_dir, child_relative, scratch, p_count))
                {
                    count_ok = false;
                    break;
                }
                continue;
            }

            if (S_ISREG(entry_stats.st_mode))
            {
                (*p_count)++;
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Close directory handle.
    // =============================================================================================
    // =============================================================================================
    {
        if (closedir(dir_handle) != 0)
        {
            count_ok = false;
        }
    }

    return count_ok;
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_collect_files_impl_z: Recursively collect file entries into a file list. Uses arena for persistent allocations and p_scratch for temporary allocations.
// =========================================================================================================================================
// =========================================================================================================================================
static bool fs_collect_files_impl_z(const char* base_dir, const char* relative_dir, arena_zh arena, arena_zh scratch, fs_file_list_zt* p_list)
{
    // =============================================================================================
    // =============================================================================================
    // Build full directory path from base and relative paths.
    // =============================================================================================
    // =============================================================================================
    size_t base_len;
    size_t relative_len;
    size_t dir_path_len;
    char* dir_path;
    {
        base_len              = strlen(base_dir);
        relative_len          = strlen(relative_dir);
        dir_path_len          = base_len + (relative_len > 0U ? 1U + relative_len : 0U);
        span_zh dir_path_span = arena_alloc_z(scratch, dir_path_len + 1U);
        dir_path              = (char*)dir_path_span->data;

        if (relative_len > 0U)
        {
            (void)snprintf(dir_path, dir_path_len + 1U, "%s/%s", base_dir, relative_dir);
        }
        else
        {
            (void)snprintf(dir_path, dir_path_len + 1U, "%s", base_dir);
        }
    }

    DIR* dir_handle;
    bool collect_ok;
    {
        dir_handle = opendir(dir_path);

        if (dir_handle == nullptr)
        {
            return false;
        }

        collect_ok = true;

        struct dirent* p_entry;
        while ((p_entry = readdir(dir_handle)) != nullptr)
        {
            if ((strcmp(p_entry->d_name, ".") == 0) || (strcmp(p_entry->d_name, "..") == 0))
            {
                continue;
            }

            size_t child_relative_len           = (relative_len > 0U ? relative_len + 1U : 0U) + strlen(p_entry->d_name);
            span_zh child_relative_scratch_span = arena_alloc_z(scratch, child_relative_len + 1U);
            char* child_relative_scratch        = (char*)child_relative_scratch_span->data;

            if (relative_len > 0U)
            {
                (void)snprintf(child_relative_scratch, child_relative_len + 1U, "%s/%s", relative_dir, p_entry->d_name);
            }
            else
            {
                (void)snprintf(child_relative_scratch, child_relative_len + 1U, "%s", p_entry->d_name);
            }

            size_t child_full_len           = base_len + 1U + child_relative_len;
            span_zh child_full_scratch_span = arena_alloc_z(scratch, child_full_len + 1U);
            char* child_full_scratch        = (char*)child_full_scratch_span->data;

            (void)snprintf(child_full_scratch, child_full_len + 1U, "%s/%s", base_dir, child_relative_scratch);

            struct stat entry_stats;
            if (stat(child_full_scratch, &entry_stats) != 0)
            {
                continue;
            }

            if (S_ISDIR(entry_stats.st_mode))
            {
                if (!fs_collect_files_impl_z(base_dir, child_relative_scratch, arena, scratch, p_list))
                {
                    collect_ok = false;
                    break;
                }
                continue;
            }

            if (!S_ISREG(entry_stats.st_mode))
            {
                continue;
            }

            span_zh child_relative_permanent_span = arena_alloc_z(arena, child_relative_len + 1U);
            char* child_relative_permanent        = (char*)child_relative_permanent_span->data;

            memcpy(child_relative_permanent, child_relative_scratch, child_relative_len + 1U);

            span_zh child_full_permanent_span = arena_alloc_z(arena, child_full_len + 1U);
            char* child_full_permanent        = (char*)child_full_permanent_span->data;

            memcpy(child_full_permanent, child_full_scratch, child_full_len + 1U);

            p_list->entries[p_list->count].relative_path = child_relative_permanent;
            p_list->entries[p_list->count].full_path     = child_full_permanent;
            p_list->count++;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Close directory handle.
    // =============================================================================================
    // =============================================================================================
    {
        if (closedir(dir_handle) != 0)
        {
            collect_ok = false;
        }
    }

    return collect_ok;
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_collect_files_recursive_z: Collect all regular files from a directory tree recursively into a file list. Allocates file list entries in arena.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_collect_files_recursive_z(arena_zh arena, arena_zh scratch_arena, const char* base_dir, fs_file_list_zt* p_out_list)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(base_dir, "base_dir is null");
        fatal_check_z(p_out_list, "p_out_list is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Count files to determine required capacity.
    // =============================================================================================
    // =============================================================================================
    size_t file_count;
    {
        file_count = 0U;

        if (!fs_count_files_recursive_z(base_dir, "", scratch_arena, &file_count))
        {
            fatal_z("failed to count files recursively");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate file list entries array.
    // =============================================================================================
    // =============================================================================================
    {
        p_out_list->capacity = file_count;
        p_out_list->count    = 0U;
        span_zh entries_span = arena_alloc_z(arena, file_count * sizeof(fs_file_entry_zt));
        p_out_list->entries  = (fs_file_entry_zt*)entries_span->data;
    }

    // =============================================================================================
    // =============================================================================================
    // Collect files into list if any exist.
    // =============================================================================================
    // =============================================================================================
    {
        if (file_count == 0U)
        {
            return;
        }

        if (!fs_collect_files_impl_z(base_dir, "", arena, scratch_arena, p_out_list))
        {
            fatal_z("failed to collect files");
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_read_file_to_arena_z: Read the entire contents of a file into arena-allocated memory and return a span pointing to it.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_read_file_to_arena_z(arena_zh arena, const char* filepath, span_zt* p_out_span)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(filepath, "filepath is null");
        fatal_check_z(p_out_span, "p_out_span is null");
    }

    size_t file_size;
    {
        struct stat file_stats;

        if (stat(filepath, &file_stats) != 0)
        {
            fatal_z("failed to stat file");
        }

        if (!S_ISREG(file_stats.st_mode))
        {
            fatal_z("filepath is not a regular file");
        }

        file_size = (size_t)file_stats.st_size;
    }

    uint8_t* buffer;
    {
        span_zh buffer_span = arena_alloc_z(arena, file_size);
        buffer              = (uint8_t*)buffer_span->data;
    }

    FILE* file_handle;
    {
        file_handle = fopen(filepath, "rb");

        if (file_handle == nullptr)
        {
            fatal_z("failed to open file");
        }

        if (file_size > 0U)
        {
            size_t bytes_read = fread(buffer, 1U, file_size, file_handle);

            if (bytes_read != file_size)
            {
                fclose(file_handle);
                fatal_z("failed to read all bytes from file");
            }
        }

        if (fclose(file_handle) != 0)
        {
            fatal_z("failed to close file");
        }
    }

    {
        p_out_span->data = buffer;
        p_out_span->size = file_size;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_map_file_readonly_z: Memory-map a file as read-only. Returns early if file is empty. Uses mmap for efficient file access.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_map_file_readonly_z(const char* filepath, fs_mapped_file_zt* p_out_map)
{
    {
        fatal_check_z(filepath, "filepath is null");
        fatal_check_z(p_out_map, "p_out_map is null");
    }

    {
        p_out_map->data = nullptr;
        p_out_map->size = 0U;
    }

    size_t file_size;
    {
        struct stat file_stats;

        if (stat(filepath, &file_stats) != 0)
        {
            fatal_z("failed to stat file");
        }

        if (!S_ISREG(file_stats.st_mode))
        {
            fatal_z("filepath is not a regular file");
        }

        file_size = (size_t)file_stats.st_size;
    }

    {
        if (file_size == 0U)
        {
            return;
        }
    }

    void* mapped_region;
    {
        int fd_local = open(filepath, O_RDONLY);

        if (fd_local < 0)
        {
            fatal_z("failed to open file: %s", filepath);
        }

        mapped_region = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd_local, 0);

        if (mapped_region == MAP_FAILED)
        {
            (void)close(fd_local);
            fatal_z("failed to mmap file: %s", filepath);
        }

        if (close(fd_local) != 0)
        {
            (void)munmap(mapped_region, file_size);
            fatal_z("failed to close file descriptor: %s", filepath);
        }
    }

    {
        p_out_map->data = (uint8_t*)mapped_region;
        p_out_map->size = file_size;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_unmap_file_z: Unmap a memory-mapped file and reset the mapped_file structure. No-op if already unmapped.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_unmap_file_z(fs_mapped_file_zt* p_mapped_file)
{
    {
        fatal_check_z(p_mapped_file, "p_mapped_file is null");
    }

    {
        if ((p_mapped_file->data == nullptr) && (p_mapped_file->size == 0U))
        {
            return;
        }
    }

    {
        if (munmap(p_mapped_file->data, p_mapped_file->size) != 0)
        {
            fatal_z("failed to munmap file");
        }

        p_mapped_file->data = nullptr;
        p_mapped_file->size = 0U;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_collect_files_by_extension_z: Collect file paths from a directory that match the given extension. Allocates file path array in arena.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_collect_files_by_extension_z(arena_zh arena, const char* dir_path, const char* extension, char*** file_paths_out, uint32_t* count_out)
{
    // =============================================================================================
    // =============================================================================================
    // Validate all input parameters.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(dir_path, "dir_path is null");
        fatal_check_z(extension, "extension is null");
        fatal_check_z(file_paths_out, "file_paths_out is null");
        fatal_check_z(count_out, "count_out is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Open directory and count files matching extension.
    // =============================================================================================
    // =============================================================================================
    DIR* dir;
    uint32_t file_count;
    {
        dir = opendir(dir_path);
        if (!dir)
        {
            fatal_z("failed to open directory");
        }

        file_count = 0;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (entry->d_type == DT_REG)
            {
                const char* name = entry->d_name;
                size_t name_len  = strlen(name);
                size_t ext_len   = strlen(extension);
                if (name_len >= ext_len && strcmp(name + name_len - ext_len, extension) == 0)
                {
                    file_count++;
                }
            }
        }
        rewinddir(dir);
    }

    // =============================================================================================
    // =============================================================================================
    // Return early if no matching files found.
    // =============================================================================================
    // =============================================================================================
    {
        if (file_count == 0)
        {
            closedir(dir);
            *file_paths_out = nullptr;
            *count_out      = 0;
            return;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate array for file paths.
    // =============================================================================================
    // =============================================================================================
    span_zh paths_span;
    char** file_paths;
    {
        paths_span = arena_alloc_z(arena, file_count * sizeof(char*));
        file_paths = (char**)paths_span->data;
    }

    // =============================================================================================
    // =============================================================================================
    // Collect matching file paths into allocated array.
    // =============================================================================================
    // =============================================================================================
    uint32_t index;
    {
        index = 0;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr && index < file_count)
        {
            if (entry->d_type == DT_REG)
            {
                const char* name = entry->d_name;
                size_t name_len  = strlen(name);
                size_t ext_len   = strlen(extension);
                if (name_len >= ext_len && strcmp(name + name_len - ext_len, extension) == 0)
                {
                    size_t dir_len    = strlen(dir_path);
                    size_t total_len  = dir_len + 1 + name_len + 1;
                    file_paths[index] = (char*)arena_alloc_z(arena, total_len)->data;
                    snprintf(file_paths[index], total_len, "%s/%s", dir_path, name);
                    index++;
                }
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Close directory and set output parameters.
    // =============================================================================================
    // =============================================================================================
    {
        closedir(dir);
        *file_paths_out = file_paths;
        *count_out      = file_count;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_create_temp_dir_z: Create a temporary directory in /tmp with the given prefix and process ID. Returns path in temp_dir_out allocated from arena.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_create_temp_dir_z(arena_zh arena, const char* prefix, char** temp_dir_out)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(prefix, "prefix is null");
        fatal_check_z(temp_dir_out, "temp_dir_out is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Generate temporary directory path and create directory.
    // =============================================================================================
    // =============================================================================================
    char temp_path[256];
    {
        snprintf(temp_path, sizeof(temp_path), "/tmp/%s_%d", prefix, (int)getpid());
        fs_mkdir_z(temp_path, 0755);
    }

    // =============================================================================================
    // =============================================================================================
    // Duplicate path string in arena and set output.
    // =============================================================================================
    // =============================================================================================
    {
        *temp_dir_out = arena_strdup_z(arena, temp_path);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_get_basename_z: Extract the basename from a file path, optionally removing a file extension. Returns arena-allocated string.
// =========================================================================================================================================
// =========================================================================================================================================
char* fs_get_basename_z(arena_zh arena, const char* file_path, const char* extension_to_remove)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(file_path, "file_path is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Extract basename from path and optionally remove extension.
    // =============================================================================================
    // =============================================================================================
    const char* basename;
    size_t name_len;
    {
        basename = strrchr(file_path, '/');
        if (!basename) basename = file_path;
        else basename++;

        name_len = strlen(basename);

        if (extension_to_remove)
        {
            size_t ext_len = strlen(extension_to_remove);
            if (name_len >= ext_len && strcmp(basename + name_len - ext_len, extension_to_remove) == 0)
            {
                name_len -= ext_len;
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate and copy basename string.
    // =============================================================================================
    // =============================================================================================
    char* result;
    {
        result = (char*)arena_alloc_z(arena, name_len + 1)->data;
        strncpy(result, basename, name_len);
        result[name_len] = '\0';
    }

    return result;
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_cleanup_temp_dir_z: Remove a temporary directory using rm -rf command. No-op if temp_dir is null.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_cleanup_temp_dir_z(const char* temp_dir)
{
    if (temp_dir)
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
        system(cmd);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_write_text_file_z: Write text content to a file, creating or overwriting the file. Fatal on write errors.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_write_text_file_z(const char* filepath, const char* text)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(filepath, "filepath is null");
        fatal_check_z(text, "text is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Open file for writing.
    // =============================================================================================
    // =============================================================================================
    FILE* file;
    {
        file = fopen(filepath, "w");
        if (!file)
        {
            fatal_z("failed to open file for writing: %s", filepath);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Write text content to file.
    // =============================================================================================
    // =============================================================================================
    {
        size_t text_len = strlen(text);
        if (text_len > 0)
        {
            size_t written = fwrite(text, 1, text_len, file);
            if (written != text_len)
            {
                fclose(file);
                fatal_z("failed to write all data to file: %s", filepath);
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Close file.
    // =============================================================================================
    // =============================================================================================
    {
        if (fclose(file) != 0)
        {
            fatal_z("failed to close file: %s", filepath);
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_write_binary_file_z: Write binary data to a file, creating or overwriting the file. Fatal on write errors.
// Writes to a temporary file first, then atomically renames it to the final destination to avoid race conditions.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_write_binary_file_z(const char* filepath, const void* data, size_t size)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(filepath, "filepath is null");
        fatal_check_z(data, "data is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Build temporary file path in same directory as target.
    // =============================================================================================
    // =============================================================================================
    char temp_filepath[1024];
    {
        size_t filepath_len = strlen(filepath);
        if (filepath_len + 10U > sizeof(temp_filepath))
        {
            fatal_z("filepath too long for temp file path");
        }
        snprintf(temp_filepath, sizeof(temp_filepath), "%s.tmp", filepath);
    }

    // =============================================================================================
    // =============================================================================================
    // Open temporary file for binary writing.
    // =============================================================================================
    // =============================================================================================
    FILE* file;
    {
        file = fopen(temp_filepath, "wb");
        if (!file)
        {
            fatal_z("failed to open temporary file for writing: %s", temp_filepath);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Write binary data to temporary file.
    // =============================================================================================
    // =============================================================================================
    {
        if (size > 0)
        {
            size_t written = fwrite(data, 1, size, file);
            if (written != size)
            {
                fclose(file);
                unlink(temp_filepath);
                fatal_z("failed to write all data to temporary file: %s", temp_filepath);
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Flush FILE* buffer, sync to disk, and close temporary file.
    // =============================================================================================
    // =============================================================================================
    {
        if (fflush(file) != 0)
        {
            fclose(file);
            unlink(temp_filepath);
            fatal_z("failed to flush temporary file buffer: %s", temp_filepath);
        }

        int fd = fileno(file);
        if (fd >= 0 && fsync(fd) != 0)
        {
            fclose(file);
            unlink(temp_filepath);
            fatal_z("failed to sync temporary file to disk: %s", temp_filepath);
        }

        if (fclose(file) != 0)
        {
            unlink(temp_filepath);
            fatal_z("failed to close temporary file: %s", temp_filepath);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Atomically rename temporary file to final destination.
    // =============================================================================================
    // =============================================================================================
    {
        if (rename(temp_filepath, filepath) != 0)
        {
            unlink(temp_filepath);
            fatal_z("failed to rename temporary file to final destination: %s", temp_filepath);
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// fs_write_binary_blob_z: Write binary data to a file named with SHA-256 hash. Computes hash of data and creates filename with prefix and hash hex.
// =========================================================================================================================================
// =========================================================================================================================================
void fs_write_binary_blob_z(const void* data, size_t element_size, size_t count, const char* output_dir, const char* prefix, char* filename_out, size_t filename_size)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(output_dir, "output_dir is null");
        fatal_check_z(data, "data is null: %s, %s", output_dir, filename_out);
        fatal_check_z(prefix, "prefix is null: %s, %s", output_dir, filename_out);
        fatal_check_z(filename_out, "filename_out is null: %s, %s", output_dir, filename_out);
    }

    // =============================================================================================
    // =============================================================================================
    // Compute SHA-256 hash of data.
    // =============================================================================================
    // =============================================================================================
    size_t byte_count;
    hash_sha256_zt hash;
    {
        byte_count = element_size * count;
        hash       = hash_sha256_z(data, byte_count);
    }

    // =============================================================================================
    // =============================================================================================
    // Convert hash to hexadecimal string and generate filename.
    // =============================================================================================
    // =============================================================================================
    char hash_hex[HASH_SHA256_SIZE * 2 + 1];
    {
        for (int i = 0; i < HASH_SHA256_SIZE; i++)
        {
            sprintf(hash_hex + i * 2, "%02x", hash.bytes[i]);
        }

        snprintf(filename_out, filename_size, "%s%s.bin", prefix, hash_hex);
    }

    // =============================================================================================
    // =============================================================================================
    // Build full filepath and write data to file.
    // =============================================================================================
    // =============================================================================================
    char filepath[1024];
    {
        snprintf(filepath, sizeof(filepath), "%s/%s", output_dir, filename_out);

        FILE* file = fopen(filepath, "wb");
        if (!file)
        {
            fatal_z("failed to open file for writing: %s", filepath);
        }

        size_t written = fwrite(data, element_size, count, file);
        if (written != count)
        {
            fclose(file);
            fatal_z("failed to write all data to file: %s", filepath);
        }

        if (fclose(file) != 0)
        {
            fatal_z("failed to close file: %s", filepath);
        }
    }
}
