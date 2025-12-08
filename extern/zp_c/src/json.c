#include "zp_c/json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zp_c/arena.h"
#include "zp_c/fatal.h"
#include "zp_c/fs.h"

struct json_value_zt
{
    arena_zh arena;
    enum json_type type;
    union
    {
        struct
        {
            struct json_object_pair* pairs;
            size_t pair_count;
            size_t pair_capacity;
        } object;
        struct
        {
            json_zh* elements;
            size_t element_count;
            size_t element_capacity;
        } array;
        char* string;
        int64_t integer;
        double real;
        bool boolean;
    } data;
};

struct json_object_pair
{
    char* key;
    json_zh value;
};

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_z: Create a new JSON object value using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_object_z(arena_zh arena)
{
    fatal_check_z(arena, "arena is null");

    json_zh value                    = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    value->arena                     = arena;
    value->type                      = JSON_TYPE_OBJECT;
    value->data.object.pairs         = nullptr;
    value->data.object.pair_count    = 0;
    value->data.object.pair_capacity = 0;

    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_z: Create a new JSON array value using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_array_z(arena_zh arena)
{
    fatal_check_z(arena, "arena is null");

    json_zh value                      = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    value->arena                       = arena;
    value->type                        = JSON_TYPE_ARRAY;
    value->data.array.elements         = nullptr;
    value->data.array.element_count    = 0;
    value->data.array.element_capacity = 0;

    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_string_z: Create a new JSON string value from a C string using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_string_z(arena_zh arena, const char* str)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(str, "str is null");

    json_zh value      = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    value->arena       = arena;
    value->type        = JSON_TYPE_STRING;
    value->data.string = arena_strdup_z(arena, str);

    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_integer_z: Create a new JSON integer value using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_integer_z(arena_zh arena, int64_t value)
{
    fatal_check_z(arena, "arena is null");

    json_zh json       = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    json->arena        = arena;
    json->type         = JSON_TYPE_INTEGER;
    json->data.integer = value;

    return json;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_boolean_z: Create a new JSON boolean value using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_boolean_z(arena_zh arena, bool value)
{
    fatal_check_z(arena, "arena is null");

    json_zh json       = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    json->arena        = arena;
    json->type         = JSON_TYPE_BOOLEAN;
    json->data.boolean = value;

    return json;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_real_z: Create a new JSON real (floating-point) value using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_real_z(arena_zh arena, double value)
{
    fatal_check_z(arena, "arena is null");

    json_zh json    = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    json->arena     = arena;
    json->type      = JSON_TYPE_REAL;
    json->data.real = value;

    return json;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_set_z: Set a key-value pair in a JSON object. Updates existing key or adds new pair with dynamic capacity expansion.
// =========================================================================================================================================
// =========================================================================================================================================
void json_object_set_z(json_zh object, const char* key, json_zh value)
{
    fatal_check_z(object, "object is null");
    fatal_check_bool_z(object->type == JSON_TYPE_OBJECT, "json_object_set_z: value is not an object");
    fatal_check_z(key, "key is null");
    fatal_check_z(value, "value is null");

    for (size_t i = 0; i < object->data.object.pair_count; i++)
    {
        if (strcmp(object->data.object.pairs[i].key, key) == 0)
        {
            object->data.object.pairs[i].value = value;
            return;
        }
    }

    size_t new_capacity = object->data.object.pair_capacity == 0 ? 8 : object->data.object.pair_capacity * 2;
    if (object->data.object.pair_count >= object->data.object.pair_capacity)
    {
        struct json_object_pair* new_pairs = (struct json_object_pair*)arena_alloc_z(object->arena, new_capacity * sizeof(struct json_object_pair))->data;
        if (object->data.object.pairs)
        {
            memcpy(new_pairs, object->data.object.pairs, object->data.object.pair_count * sizeof(struct json_object_pair));
        }
        object->data.object.pairs         = new_pairs;
        object->data.object.pair_capacity = new_capacity;
    }

    object->data.object.pairs[object->data.object.pair_count].key   = arena_strdup_z(object->arena, key);
    object->data.object.pairs[object->data.object.pair_count].value = value;
    object->data.object.pair_count++;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_append_z: Append a value to a JSON array with dynamic capacity expansion.
// =========================================================================================================================================
// =========================================================================================================================================
void json_array_append_z(json_zh array, json_zh value)
{
    fatal_check_z(array, "array is null");
    fatal_check_bool_z(array->type == JSON_TYPE_ARRAY, "json_array_append_z: value is not an array");
    fatal_check_z(value, "value is null");

    size_t new_capacity = array->data.array.element_capacity == 0 ? 8 : array->data.array.element_capacity * 2;
    if (array->data.array.element_count >= array->data.array.element_capacity)
    {
        json_zh* new_elements = (json_zh*)arena_alloc_z(array->arena, new_capacity * sizeof(json_zh))->data;
        if (array->data.array.elements)
        {
            memcpy(new_elements, array->data.array.elements, array->data.array.element_count * sizeof(json_zh));
        }
        array->data.array.elements         = new_elements;
        array->data.array.element_capacity = new_capacity;
    }

    array->data.array.elements[array->data.array.element_count] = value;
    array->data.array.element_count++;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_type_z: Get the type of a JSON value.
// =========================================================================================================================================
// =========================================================================================================================================
enum json_type json_type_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_z: Get a value from a JSON object by key. Fatal if key not found. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh json_object_get_z(json_zh object, const char* key)
{
    fatal_check_z(object, "object is null");
    fatal_check_bool_z(object->type == JSON_TYPE_OBJECT, "json_object_get_z: value is not an object");
    fatal_check_z(key, "key is null");

    for (size_t i = 0; i < object->data.object.pair_count; i++)
    {
        if (strcmp(object->data.object.pairs[i].key, key) == 0)
        {
            return object->data.object.pairs[i].value;
        }
    }

    fatal_z("json_object_get_z: key '%s' not found", key);
    return nullptr;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_z: Check if a JSON object contains a key. Returns false if object is null, not an object, or key is null.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_z(json_zh object, const char* key)
{
    if (!object || object->type != JSON_TYPE_OBJECT || !key)
    {
        return false;
    }

    for (size_t i = 0; i < object->data.object.pair_count; i++)
    {
        if (strcmp(object->data.object.pairs[i].key, key) == 0)
        {
            return true;
        }
    }

    return false;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_string_z: Check if a JSON object contains a string key. Returns false if key doesn't exist or value is not a string.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_string_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_STRING;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_integer_z: Check if a JSON object contains an integer key. Returns false if key doesn't exist or value is not an integer.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_integer_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_INTEGER;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_real_z: Check if a JSON object contains a real key. Returns false if key doesn't exist or value is not a real.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_real_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_REAL;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_boolean_z: Check if a JSON object contains a boolean key. Returns false if key doesn't exist or value is not a boolean.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_boolean_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_BOOLEAN;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_object_z: Check if a JSON object contains an object key. Returns false if key doesn't exist or value is not an object.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_object_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_OBJECT;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_has_array_z: Check if a JSON object contains an array key. Returns false if key doesn't exist or value is not an array.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_has_array_z(json_zh object, const char* key)
{
    if (!json_object_has_z(object, key))
    {
        return false;
    }
    json_zh value = json_object_get_z(object, key);
    return value->type == JSON_TYPE_ARRAY;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_is_object_z: Check if a JSON value is an object type. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static bool json_is_object_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type == JSON_TYPE_OBJECT;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_is_array_z: Check if a JSON value is an array type. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static bool json_is_array_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type == JSON_TYPE_ARRAY;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_string_value_z: Get the string value from a JSON string value. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static const char* json_string_value_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    fatal_check_bool_z(value->type == JSON_TYPE_STRING, "json_string_value_z: value is not a string");
    return value->data.string;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_integer_value_z: Get the integer value from a JSON integer value. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static int64_t json_integer_value_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    fatal_check_bool_z(value->type == JSON_TYPE_INTEGER, "json_integer_value_z: value is not an integer");
    return value->data.integer;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_real_value_z: Get the real (floating-point) value from a JSON real value. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static double json_real_value_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    fatal_check_bool_z(value->type == JSON_TYPE_REAL, "json_real_value_z: value is not a real");
    return value->data.real;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_boolean_value_z: Get the boolean value from a JSON boolean value. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static bool json_boolean_value_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    fatal_check_bool_z(value->type == JSON_TYPE_BOOLEAN, "json_boolean_value_z: value is not a boolean");
    return value->data.boolean;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_string_z: Get a string value from a JSON object by key. Fatal if key not found or value is not a string.
// =========================================================================================================================================
// =========================================================================================================================================
const char* json_object_get_string_z(json_zh object, const char* key)
{
    return json_string_value_z(json_object_get_z(object, key));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_integer_z: Get an integer value from a JSON object by key. Fatal if key not found or value is not an integer.
// =========================================================================================================================================
// =========================================================================================================================================
int64_t json_object_get_integer_z(json_zh object, const char* key)
{
    return json_integer_value_z(json_object_get_z(object, key));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_real_z: Get a real (floating-point) value from a JSON object by key. Fatal if key not found or value is not a real.
// =========================================================================================================================================
// =========================================================================================================================================
double json_object_get_real_z(json_zh object, const char* key)
{
    return json_real_value_z(json_object_get_z(object, key));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_boolean_z: Get a boolean value from a JSON object by key. Fatal if key not found or value is not a boolean.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_object_get_boolean_z(json_zh object, const char* key)
{
    return json_boolean_value_z(json_object_get_z(object, key));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_object_z: Get an object value from a JSON object by key. Fatal if key not found or value is not an object.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_object_get_object_z(json_zh object, const char* key)
{
    json_zh value = json_object_get_z(object, key);
    fatal_check_bool_z(json_is_object_z(value), "json_object_get_object_z: value is not an object");
    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_object_get_array_z: Get an array value from a JSON object by key. Fatal if key not found or value is not an array.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_object_get_array_z(json_zh object, const char* key)
{
    json_zh value = json_object_get_z(object, key);
    fatal_check_bool_z(json_is_array_z(value), "json_object_get_array_z: value is not an array");
    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_size_z: Get the number of elements in a JSON array.
// =========================================================================================================================================
// =========================================================================================================================================
size_t json_array_size_z(json_zh array)
{
    fatal_check_z(array, "array is null");
    fatal_check_bool_z(array->type == JSON_TYPE_ARRAY, "json_array_size_z: value is not an array");
    return array->data.array.element_count;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_z: Get an element from a JSON array by index. (Internal)
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh json_array_get_z(json_zh array, size_t index)
{
    fatal_check_z(array, "array is null");
    fatal_check_bool_z(array->type == JSON_TYPE_ARRAY, "json_array_get_z: value is not an array");
    fatal_check_bool_z(index < array->data.array.element_count, "json_array_get_z: index out of bounds");
    return array->data.array.elements[index];
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_string_z: Get a string element from a JSON array by index. Fatal if index out of bounds or value is not a string.
// =========================================================================================================================================
// =========================================================================================================================================
const char* json_array_get_string_z(json_zh array, size_t index)
{
    return json_string_value_z(json_array_get_z(array, index));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_integer_z: Get an integer element from a JSON array by index. Fatal if index out of bounds or value is not an integer.
// =========================================================================================================================================
// =========================================================================================================================================
int64_t json_array_get_integer_z(json_zh array, size_t index)
{
    return json_integer_value_z(json_array_get_z(array, index));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_real_z: Get a real (floating-point) element from a JSON array by index. Fatal if index out of bounds or value is not a real.
// =========================================================================================================================================
// =========================================================================================================================================
double json_array_get_real_z(json_zh array, size_t index)
{
    return json_real_value_z(json_array_get_z(array, index));
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_object_z: Get an object element from a JSON array by index. Fatal if index out of bounds or value is not an object.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_array_get_object_z(json_zh array, size_t index)
{
    json_zh value = json_array_get_z(array, index);
    fatal_check_bool_z(json_is_object_z(value), "json_array_get_object_z: value is not an object");
    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_array_z: Get an array element from a JSON array by index. Fatal if index out of bounds or value is not an array.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_array_get_array_z(json_zh array, size_t index)
{
    json_zh value = json_array_get_z(array, index);
    fatal_check_bool_z(json_is_array_z(value), "json_array_get_array_z: value is not an array");
    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_array_get_boolean_z: Get a boolean element from a JSON array by index. Fatal if index out of bounds or value is not a boolean.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_array_get_boolean_z(json_zh array, size_t index)
{
    return json_boolean_value_z(json_array_get_z(array, index));
}

// =========================================================================================================================================
// =========================================================================================================================================
// serialize_value_z: Recursively serialize a JSON value to a string buffer with dynamic capacity expansion.
// =========================================================================================================================================
// =========================================================================================================================================
static void serialize_value_z(arena_zh arena, json_zh value, char** buffer, size_t* capacity, size_t* offset)
{
    switch (value->type)
    {
        case JSON_TYPE_OBJECT:
        {

            if (*offset + 2 >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            (*buffer)[(*offset)++] = '{';

            for (size_t i = 0; i < value->data.object.pair_count; i++)
            {
                if (i > 0)
                {
                    if (*offset + 2 >= *capacity)
                    {
                        *capacity        *= 2;
                        char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                        memcpy(new_buffer, *buffer, *offset);
                        *buffer = new_buffer;
                    }
                    (*buffer)[(*offset)++] = ',';
                }

                const char* key_str = value->data.object.pairs[i].key;
                size_t key_len      = strlen(key_str);
                size_t key_needed   = key_len + 2;

                for (size_t j = 0; j < key_len; j++)
                {
                    if (key_str[j] == '"' || key_str[j] == '\\' || key_str[j] == '\n' || key_str[j] == '\r' || key_str[j] == '\t')
                    {
                        key_needed += 1;
                    }
                }

                while (*offset + key_needed >= *capacity)
                {
                    *capacity        *= 2;
                    char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                    memcpy(new_buffer, *buffer, *offset);
                    *buffer = new_buffer;
                }

                (*buffer)[(*offset)++] = '"';
                for (size_t j = 0; j < key_len; j++)
                {
                    if (key_str[j] == '"')
                    {
                        (*buffer)[(*offset)++] = '\\';
                        (*buffer)[(*offset)++] = '"';
                    }
                    else if (key_str[j] == '\\')
                    {
                        (*buffer)[(*offset)++] = '\\';
                        (*buffer)[(*offset)++] = '\\';
                    }
                    else if (key_str[j] == '\n')
                    {
                        (*buffer)[(*offset)++] = '\\';
                        (*buffer)[(*offset)++] = 'n';
                    }
                    else if (key_str[j] == '\r')
                    {
                        (*buffer)[(*offset)++] = '\\';
                        (*buffer)[(*offset)++] = 'r';
                    }
                    else if (key_str[j] == '\t')
                    {
                        (*buffer)[(*offset)++] = '\\';
                        (*buffer)[(*offset)++] = 't';
                    }
                    else
                    {
                        (*buffer)[(*offset)++] = key_str[j];
                    }
                }
                (*buffer)[(*offset)++] = '"';
                if (*offset + 2 >= *capacity)
                {
                    *capacity        *= 2;
                    char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                    memcpy(new_buffer, *buffer, *offset);
                    *buffer = new_buffer;
                }
                (*buffer)[(*offset)++] = ':';
                serialize_value_z(arena, value->data.object.pairs[i].value, buffer, capacity, offset);
            }

            if (*offset + 2 >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            (*buffer)[(*offset)++] = '}';
            break;
        }
        case JSON_TYPE_ARRAY:
        {
            if (*offset + 2 >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            (*buffer)[(*offset)++] = '[';

            for (size_t i = 0; i < value->data.array.element_count; i++)
            {
                if (i > 0)
                {
                    if (*offset + 2 >= *capacity)
                    {
                        *capacity        *= 2;
                        char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                        memcpy(new_buffer, *buffer, *offset);
                        *buffer = new_buffer;
                    }
                    (*buffer)[(*offset)++] = ',';
                }
                serialize_value_z(arena, value->data.array.elements[i], buffer, capacity, offset);
            }

            if (*offset + 2 >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            (*buffer)[(*offset)++] = ']';
            break;
        }
        case JSON_TYPE_STRING:
        {
            const char* str = value->data.string;
            size_t len      = strlen(str);
            size_t needed   = len + 2;

            for (size_t i = 0; i < len; i++)
            {
                if (str[i] == '"' || str[i] == '\\' || str[i] == '\n' || str[i] == '\r' || str[i] == '\t')
                {
                    needed += 1;
                }
            }

            while (*offset + needed >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }

            (*buffer)[(*offset)++] = '"';
            for (size_t i = 0; i < len; i++)
            {
                if (str[i] == '"')
                {
                    (*buffer)[(*offset)++] = '\\';
                    (*buffer)[(*offset)++] = '"';
                }
                else if (str[i] == '\\')
                {
                    (*buffer)[(*offset)++] = '\\';
                    (*buffer)[(*offset)++] = '\\';
                }
                else if (str[i] == '\n')
                {
                    (*buffer)[(*offset)++] = '\\';
                    (*buffer)[(*offset)++] = 'n';
                }
                else if (str[i] == '\r')
                {
                    (*buffer)[(*offset)++] = '\\';
                    (*buffer)[(*offset)++] = 'r';
                }
                else if (str[i] == '\t')
                {
                    (*buffer)[(*offset)++] = '\\';
                    (*buffer)[(*offset)++] = 't';
                }
                else
                {
                    (*buffer)[(*offset)++] = str[i];
                }
            }
            (*buffer)[(*offset)++] = '"';
            break;
        }
        case JSON_TYPE_INTEGER:
        {
            char num_buf[64];
            int written = snprintf(num_buf, sizeof(num_buf), "%lld", (long long)value->data.integer);
            fatal_check_bool_z(written > 0 && written < (int)sizeof(num_buf), "json_dumps_z: integer formatting failed");

            while (*offset + (size_t)written >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            memcpy(*buffer + *offset, num_buf, (size_t)written);
            *offset += (size_t)written;
            break;
        }
        case JSON_TYPE_REAL:
        {
            char num_buf[64];
            int written = snprintf(num_buf, sizeof(num_buf), "%.17g", value->data.real);
            fatal_check_bool_z(written > 0 && written < (int)sizeof(num_buf), "json_dumps_z: real formatting failed");

            bool has_decimal = false;
            for (int i = 0; i < written; i++)
            {
                if (num_buf[i] == '.' || num_buf[i] == 'e' || num_buf[i] == 'E')
                {
                    has_decimal = true;
                    break;
                }
            }
            if (!has_decimal && written < (int)sizeof(num_buf) - 2)
            {
                num_buf[written++] = '.';
                num_buf[written++] = '0';
            }

            while (*offset + (size_t)written >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            memcpy(*buffer + *offset, num_buf, (size_t)written);
            *offset += (size_t)written;
            break;
        }
        case JSON_TYPE_BOOLEAN:
        {
            const char* bool_str = value->data.boolean ? "true" : "false";
            size_t len           = strlen(bool_str);

            while (*offset + len >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            memcpy(*buffer + *offset, bool_str, len);
            *offset += len;
            break;
        }
        case JSON_TYPE_NULL:
        {
            const char* null_str = "null";
            size_t len           = strlen(null_str);

            while (*offset + len >= *capacity)
            {
                *capacity        *= 2;
                char* new_buffer  = (char*)arena_alloc_z(arena, *capacity)->data;
                memcpy(new_buffer, *buffer, *offset);
                *buffer = new_buffer;
            }
            memcpy(*buffer + *offset, null_str, len);
            *offset += len;
            break;
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_dumps_z: Serialize a JSON value to a string representation using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
char* json_dumps_z(arena_zh arena, json_zh value)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(value, "value is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Initialize buffer and serialize JSON value.
    // =============================================================================================
    // =============================================================================================
    size_t capacity;
    size_t offset;
    char* buffer;
    {
        capacity = 1024;
        offset   = 0;
        buffer   = (char*)arena_alloc_z(arena, capacity)->data;

        serialize_value_z(arena, value, &buffer, &capacity, &offset);
    }

    // =============================================================================================
    // =============================================================================================
    // Ensure buffer has space for null terminator and add it.
    // =============================================================================================
    // =============================================================================================
    {
        if (offset >= capacity)
        {
            capacity         += 1;
            char* new_buffer  = (char*)arena_alloc_z(arena, capacity)->data;
            memcpy(new_buffer, buffer, offset);
            buffer = new_buffer;
        }
        buffer[offset] = '\0';
    }

    return buffer;
}

// =========================================================================================================================================
// =========================================================================================================================================
// skip_whitespace_z: Advance json_str pointer past whitespace characters.
// =========================================================================================================================================
// =========================================================================================================================================
static void skip_whitespace_z(const char** json_str)
{
    while (**json_str && isspace((unsigned char)**json_str))
    {
        (*json_str)++;
    }
}

static json_zh parse_value_z(arena_zh arena, const char** json_str);

// =========================================================================================================================================
// =========================================================================================================================================
// parse_string_z: Parse a JSON string value from json_str, handling escape sequences.
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh parse_string_z(arena_zh arena, const char** json_str)
{
    fatal_check_bool_z(**json_str == '"', "json_loads_z: expected string");
    (*json_str)++;

    size_t start          = 0;
    size_t len            = 0;
    const char* str_start = *json_str;

    while (**json_str && **json_str != '"')
    {
        if (**json_str == '\\')
        {
            (*json_str)++;
            if (**json_str == '\0')
            {
                fatal_z("json_loads_z: unterminated escape sequence at end of input");
            }
        }
        len++;
        (*json_str)++;
    }

    fatal_check_bool_z(**json_str == '"', "json_loads_z: unterminated string");
    (*json_str)++;

    char* result   = (char*)arena_alloc_z(arena, len + 1)->data;
    size_t out_idx = 0;
    const char* in = str_start;

    while (in < *json_str - 1)
    {
        if (*in == '\\')
        {
            in++;
            switch (*in)
            {
                case '"':  result[out_idx++] = '"'; break;
                case '\\': result[out_idx++] = '\\'; break;
                case 'n':  result[out_idx++] = '\n'; break;
                case 'r':  result[out_idx++] = '\r'; break;
                case 't':  result[out_idx++] = '\t'; break;
                default:   fatal_z("json_loads_z: invalid escape sequence '\\%c'", *in);
            }
            in++;
        }
        else
        {
            result[out_idx++] = *in++;
        }
    }
    result[out_idx]    = '\0';

    json_zh value      = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    value->arena       = arena;
    value->type        = JSON_TYPE_STRING;
    value->data.string = result;

    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_number_z: Parse a JSON number (integer or real) from json_str.
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh parse_number_z(arena_zh arena, const char** json_str)
{
    const char* start = *json_str;
    bool is_real      = false;

    if (**json_str == '-')
    {
        (*json_str)++;
    }

    if (**json_str == '0')
    {
        (*json_str)++;
    }
    else if (isdigit((unsigned char)**json_str))
    {
        while (isdigit((unsigned char)**json_str))
        {
            (*json_str)++;
        }
    }
    else
    {
        fatal_z("json_loads_z: invalid number, found '%c'", **json_str);
    }

    if (**json_str == '.')
    {
        is_real = true;
        (*json_str)++;
        fatal_check_bool_z(isdigit((unsigned char)**json_str), "json_loads_z: invalid number after decimal point");
        while (isdigit((unsigned char)**json_str))
        {
            (*json_str)++;
        }
    }

    if (**json_str == 'e' || **json_str == 'E')
    {
        is_real = true;
        (*json_str)++;
        if (**json_str == '+' || **json_str == '-')
        {
            (*json_str)++;
        }
        fatal_check_bool_z(isdigit((unsigned char)**json_str), "json_loads_z: invalid number in exponent");
        while (isdigit((unsigned char)**json_str))
        {
            (*json_str)++;
        }
    }

    size_t len = *json_str - start;
    char num_buf[128];
    fatal_check_bool_z(len < sizeof(num_buf), "json_loads_z: number too long");
    memcpy(num_buf, start, len);
    num_buf[len]  = '\0';

    json_zh value = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
    value->arena  = arena;

    if (is_real)
    {
        value->type      = JSON_TYPE_REAL;
        value->data.real = strtod(num_buf, nullptr);
    }
    else
    {
        value->type         = JSON_TYPE_INTEGER;
        value->data.integer = strtoll(num_buf, nullptr, 10);
    }

    return value;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_value_z: Recursively parse a JSON value (object, array, string, number, boolean, or null) from json_str.
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh parse_value_z(arena_zh arena, const char** json_str)
{
    skip_whitespace_z(json_str);

    if (**json_str == '\0')
    {
        fatal_z("json_loads_z: unexpected end of input while parsing value");
    }

    if (**json_str == '{')
    {
        (*json_str)++;
        skip_whitespace_z(json_str);

        json_zh object = json_object_z(arena);

        if (**json_str == '}')
        {
            (*json_str)++;
            return object;
        }

        while (true)
        {
            skip_whitespace_z(json_str);
            json_zh key = parse_string_z(arena, json_str);
            skip_whitespace_z(json_str);
            fatal_check_bool_z(**json_str == ':', "json_loads_z: expected colon");
            (*json_str)++;
            skip_whitespace_z(json_str);
            json_zh value = parse_value_z(arena, json_str);
            json_object_set_z(object, key->data.string, value);

            skip_whitespace_z(json_str);
            if (**json_str == '}')
            {
                (*json_str)++;
                break;
            }
            fatal_check_bool_z(**json_str == ',', "json_loads_z: expected comma or closing brace");
            (*json_str)++;
        }

        return object;
    }
    else if (**json_str == '[')
    {
        (*json_str)++;
        skip_whitespace_z(json_str);

        json_zh array = json_array_z(arena);

        if (**json_str == ']')
        {
            (*json_str)++;
            return array;
        }

        while (true)
        {
            skip_whitespace_z(json_str);
            json_zh value = parse_value_z(arena, json_str);
            json_array_append_z(array, value);

            skip_whitespace_z(json_str);
            if (**json_str == ']')
            {
                (*json_str)++;
                break;
            }
            fatal_check_bool_z(**json_str == ',', "json_loads_z: expected comma or closing bracket");
            (*json_str)++;
        }

        return array;
    }
    else if (**json_str == '"')
    {
        return parse_string_z(arena, json_str);
    }
    else if (**json_str == '-' || isdigit((unsigned char)**json_str))
    {
        return parse_number_z(arena, json_str);
    }
    else if (strncmp(*json_str, "true", 4) == 0)
    {
        *json_str           += 4;
        json_zh value        = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
        value->arena         = arena;
        value->type          = JSON_TYPE_BOOLEAN;
        value->data.boolean  = true;
        return value;
    }
    else if (strncmp(*json_str, "false", 5) == 0)
    {
        *json_str           += 5;
        json_zh value        = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
        value->arena         = arena;
        value->type          = JSON_TYPE_BOOLEAN;
        value->data.boolean  = false;
        return value;
    }
    else if (strncmp(*json_str, "null", 4) == 0)
    {
        *json_str     += 4;
        json_zh value  = (json_zh)arena_alloc_z(arena, sizeof(struct json_value_zt))->data;
        value->arena   = arena;
        value->type    = JSON_TYPE_NULL;
        return value;
    }
    else
    {
        fatal_z("json_loads_z: unexpected character '%c'", **json_str);
        return nullptr;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_loads_z: Parse a JSON string into a JSON value tree using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_loads_z(arena_zh arena, const char* json_str)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(json_str, "json_str is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Parse JSON value and verify no extra data remains.
    // =============================================================================================
    // =============================================================================================
    json_zh result;
    {
        const char* cursor = json_str;
        result             = parse_value_z(arena, &cursor);
        skip_whitespace_z(&cursor);
        fatal_check_bool_z(*cursor == '\0', "json_loads_z: extra data after JSON value");
    }

    return result;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_load_file_z: Load and parse a JSON file into a JSON value tree using arena allocation.
// =========================================================================================================================================
// =========================================================================================================================================
json_zh json_load_file_z(arena_zh arena, const char* filepath)
{
    // =============================================================================================
    // =============================================================================================
    // Validate inputs.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(filepath, "filepath is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Open file and determine file size.
    // =============================================================================================
    // =============================================================================================
    FILE* file;
    long file_size;
    {
        file = fopen(filepath, "r");
        fatal_check_z(file, "json_load_file_z: failed to open file");

        fseek(file, 0, SEEK_END);
        file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        fatal_check_bool_z(file_size >= 0, "json_load_file_z: failed to get file size");
        fatal_check_bool_z(file_size < 1024 * 1024 * 1024, "json_load_file_z: file too large");
    }

    // =============================================================================================
    // =============================================================================================
    // Read file contents into arena-allocated buffer.
    // =============================================================================================
    // =============================================================================================
    char* buffer;
    {
        buffer      = (char*)arena_alloc_z(arena, (size_t)file_size + 1)->data;
        size_t read = fread(buffer, 1, (size_t)file_size, file);
        fatal_check_bool_z(read == (size_t)file_size, "json_load_file_z: failed to read file");
        buffer[read] = '\0';

        fclose(file);
    }

    return json_loads_z(arena, buffer);
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_is_string_z: Check if a JSON value is a string type.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_is_string_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type == JSON_TYPE_STRING;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_is_integer_z: Check if a JSON value is an integer type.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_is_integer_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type == JSON_TYPE_INTEGER;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_is_boolean_z: Check if a JSON value is a boolean type.
// =========================================================================================================================================
// =========================================================================================================================================
bool json_is_boolean_z(json_zh value)
{
    fatal_check_z(value, "value is null");
    return value->type == JSON_TYPE_BOOLEAN;
}
