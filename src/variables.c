#include "_variables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ptr_array.h"

// Create a new Variable
static Variable *variable_create(const char *name, const char *value) {
    Variable *var = malloc(sizeof(Variable));
    if (!var) return NULL;

    var->name = string_create(16);
    var->value = string_create(16);
    if (!var->name || !var->value) {
        string_destroy(&var->name);
        string_destroy(&var->value);
        free(var);
        return NULL;
    }

    string_append_zstring(var->name, name);
    string_append_zstring(var->value, value);
    var->exported = false;
    var->read_only = false;
    return var;
}

// Free a Variable
static void variable_free(Variable *var) {
    if (!var) return;
    string_destroy(&var->name);
    string_destroy(var->value);
    free(var);
}

// Free a string_t
static void string_free(void *s) {
    string_destroy(&(string_t *)s);
}

// Find variable by name, return index or -1 if not found
static int find_variable_index(PtrArray *vars, const char *name) {
    for (int i = 0; i < ptr_array_size(vars); i++) {
        Variable *var = (Variable *) ptr_array_get(vars, i);
        if (strcmp(string_data(var->name), name) == 0) {
            return i;
        }
    }
    return -1;
}

// Check if name is a special variable
static bool is_special_variable(const char *name) {
    return strcmp(name, "?") == 0 ||
           strcmp(name, "#") == 0 ||
           strcmp(name, "$") == 0 ||
           strcmp(name, "!") == 0 ||
           strcmp(name, "0") == 0 ||
           strcmp(name, "@") == 0 ||
           strcmp(name, "*") == 0 ||
           strcmp(name, "-") == 0;
}

// Create VariableStore
VariableStore *variable_store_create(const char *shell_name) {
    VariableStore *store = malloc(sizeof(VariableStore));
    if (!store) return NULL;

    store->variables = ptr_array_create();
    store->positional_params = ptr_array_create();
    if (!store->variables || !store->positional_params) {
        ptr_array_destroy(store->variables, (PtrArrayFreeFunc)variable_free);
        ptr_array_destroy(store->positional_params, string_free);
        free(store);
        return NULL;
    }

    snprintf(store->status_str, sizeof(store->status_str), "0");
    store->pid = getpid();
    store->shell_name = shell_name ? strdup(shell_name) : strdup("myshell");
    if (!store->shell_name) {
        ptr_array_destroy(store->variables, (PtrArrayFreeFunc)variable_free);
        ptr_array_destroy(store->positional_params, string_free);
        free(store);
        return NULL;
    }
    store->last_bg_pid = 0;
    strcpy(store->options, "i"); // Interactive by default
    return store;
}

// Destroy VariableStore
void variable_store_destroy(VariableStore *self) {
    if (!self) return;
    ptr_array_destroy(self->variables, (PtrArrayFreeFunc)variable_free);
    ptr_array_destroy(self->positional_params, string_free);
    free(self->shell_name);
    free(self);
}

// Set or update variable
void variable_store_set_variable(VariableStore *self, const char *name, const char *value) {
    if (!self || !name || !value) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot set special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = ptr_array_get(self->variables, idx);;
        if (var->read_only) {
            fprintf(stderr, "Variable %s is read-only\n", name);
            return;
        }
        string_clear(var->value);
        string_append_zstring(var->value, value);
    } else {
        Variable *var = variable_create(name, value);
        if (!var) {
            fprintf(stderr, "Memory allocation failed for variable %s\n", name);
            return;
        }
        ptr_array_append(self->variables, var);
    }
}

// Export variable
void variable_store_export_variable(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot export special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = (Variable *) ptr_array_get(self->variables, idx);
        var->exported = true;
    } else {
        Variable *var = variable_create(name, "");
        if (!var) {
            fprintf(stderr, "Memory allocation failed for export %s\n", name);
            return;
        }
        var->exported = true;
        ptr_array_append(self->variables, var);
    }
}

// Unset variable
void variable_store_unset_variable(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot unset special variable %s\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = (Variable *) ptr_array_get(self->variables, idx);
        if (var->read_only) {
            fprintf(stderr, "Variable %s is read-only\n", name);
            return;
        }
        ptr_array_remove(self->variables, idx, (PtrArrayFreeFunc)variable_free);
    }
}

// Get variable value
const char *variable_store_get_variable(VariableStore *self, const char *name) {
    if (!self || !name) return NULL;

    // Special variables
    if (strcmp(name, "?") == 0) {
        return self->status_str;
    }
    if (strcmp(name, "#") == 0) {
        static char num[16];
        snprintf(num, sizeof(num), "%d", ptr_array_size(self->positional_params));
        return num;
    }
    if (strcmp(name, "$") == 0) {
        static char pid[16];
        snprintf(pid, sizeof(pid), "%d", self->pid);
        return pid;
    }
    if (strcmp(name, "!") == 0) {
        if (self->last_bg_pid == 0) return "";
        static char pid[16];
        snprintf(pid, sizeof(pid), "%d", self->last_bg_pid);
        return pid;
    }
    if (strcmp(name, "0") == 0) {
        return self->shell_name;
    }
    if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
        if (ptr_array_is_empty(self->positional_params)) return "";
        string_t *result = string_create(256);
        if (!result) return "";
        for (int i = 0; i < ptr_array_size (self->positional_params); i++) {
            if (i > 0) string_append_char(result, ' ');
            string_append_zstring(result, string_data(ptr_array_get(self->positional_params, i)));
        }
        const char *value = string_data(result);
        string_destroy(&result);
        return value;
    }
    if (strcmp(name, "-") == 0) {
        return self->options;
    }

    // Positional parameters ($1, $2, ...)
    if (isdigit(name[0]) && name[0] != '0') {
        int idx = atoi(name) - 1;
        if (idx >= 0 && idx < ptr_array_size(self->positional_params)) {
            return string_data(ptr_array_get(self->positional_params, idx));
        }
        return "";
    }

    // Regular variables
    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = ptr_array_get(self->variables, idx);
        return string_data(var->value);
    }
    return "";
}

// Make variable read-only
void variable_store_make_readonly(VariableStore *self, const char *name) {
    if (!self || !name) return;

    if (is_special_variable(name)) {
        fprintf(stderr, "Cannot make special variable %s read-only\n", name);
        return;
    }

    int idx = find_variable_index(self->variables, name);
    if (idx >= 0) {
        Variable *var = ptr_array_get(self->variables, idx);
        var->read_only = true;
    } else {
        Variable *var = variable_create(name, "");
        if (!var) {
            fprintf(stderr, "Memory allocation failed for readonly %s\n", name);
            return;
        }
        var->read_only = true;
        ptr_array_append(self->variables, var);
    }
}

// Dump all variables (for debugging)
void variable_store_dump_variables(VariableStore *self) {
    if (!self) {
        printf("VariableStore is NULL\n");
        return;
    }

    printf("Special Variables:\n");
    printf("  ?=%s\n", variable_store_get_variable(self, "?"));
    printf("  #=%s\n", variable_store_get_variable(self, "#"));
    printf("  $=%s\n", variable_store_get_variable(self, "$"));
    printf("  !=%s\n", variable_store_get_variable(self, "!"));
    printf("  0=%s\n", variable_store_get_variable(self, "0"));
    printf("  @=%s\n", variable_store_get_variable(self, "@"));
    printf("  *=%s\n", variable_store_get_variable(self, "*"));
    printf("  -=%s\n", variable_store_get_variable(self, "-"));
    for (int i = 0; i < ptr_array_size (self->positional_params); i++) {
        printf("  %d=%s\n", i + 1, string_data(ptr_array_get(self->positional_params, i)));
    }

    printf("Regular Variables:\n");
    for (int i = 0; i < ptr_array_size (self->variables); i++) {
        Variable *var = ptr_array_get(self->variables, i);
        printf("  %s=%s [exported=%d, read_only=%d]\n",
               string_data(var->name),
               string_data(var->value),
               var->exported,
               var->read_only);
    }
}

// Set exit status
void variable_store_set_status(VariableStore *self, int status) {
    if (!self) return;
    snprintf(self->status_str, sizeof(self->status_str), "%d", status);
}

// Set positional parameters
void variable_store_set_positional_params(VariableStore *self, int argc, char **argv) {
    if (!self) return;
    ptr_array_clear(self->positional_params, string_free);
    for (int i = 0; i < argc; i++) {
        string_t *param = string_create(16);
        if (!param) continue;
        string_append_zstring(param, argv[i]);
        ptr_array_append(self->positional_params, param);
    }
}

// Set background PID
void variable_store_set_background_pid(VariableStore *self, long pid) {
    if (!self) return;
    self->last_bg_pid = pid;
}

// Set shell options
void variable_store_set_options(VariableStore *self, const char *opts) {
    if (!self || !opts) return;
    strncpy(self->options, opts, sizeof(self->options) - 1);
    self->options[sizeof(self->options) - 1] = '\0';
}

// Helper function to match a pattern (basic glob-style matching for % and #)
static bool pattern_matches(const char *str, const char *pattern) {
    // POSIX shell patterns for % and # are simple suffix/prefix matches
    // For simplicity, treat pattern as a literal string unless it contains '*'
    if (strchr(pattern, '*')) {
        // Handle '*' as a wildcard (matches any sequence)
        size_t pat_len = strlen(pattern);
        if (pattern[0] == '*' && pat_len == 1) {
            return true; // '*' matches anything
        }
        if (pattern[0] == '*' && strstr(str, pattern + 1)) {
            return true; // '*suffix' matches if str contains suffix
        }
        if (pattern[pat_len - 1] == '*' && strncmp(str, pattern, pat_len - 1) == 0) {
            return true; // 'prefix*' matches if str starts with prefix
        }
        return false;
    }
    // Literal match
    return strstr(str, pattern) != NULL;
}

// ${parameter:-[word]} - Return word if parameter is unset or null, else parameter's value
const char *variable_store_default_value(VariableStore *self, const char *name, const char *word) {
    if (!self || !name) return word ? word : "";
    const char *value = variable_store_get_variable(self, name);
    if (!value || *value == '\0') {
        return word ? word : "";
    }
    return value;
}

// ${parameter:=[word]} - Assign word to parameter if unset or null, return assigned value
const char *variable_store_assign_default(VariableStore *self, const char *name, const char *word) {
    if (!self || !name) return word ? word : "";
    const char *value = variable_store_get_variable(self, name);
    if (!value || *value == '\0') {
        if (!is_special_variable(name)) {
            int idx = find_variable_index(self->variables, name);
            if (idx >= 0) {
                Variable *var = ptr_array_get(self->variables, idx);
                if (!var->read_only) {
                    string_clear(var->value);
                    string_append_zstring(var->value, word ? word : "");
                } else {
                    fprintf(stderr, "Variable %s is read-only\n", name);
                    return "";
                }
            } else {
                variable_store_set_variable(self, name, word ? word : "");
            }
        }
        return word ? word : "";
    }
    return value;
}

// ${parameter:?[word]} - If unset or null, print word to stderr and exit, else return value
const char *variable_store_indicate_error(VariableStore *self, const char *name, const char *word) {
    if (!self || !name) {
        fprintf(stderr, "%s: parameter null or not set\n", word ? word : "");
        exit(1);
    }
    const char *value = variable_store_get_variable(self, name);
    if (!value || *value == '\0') {
        fprintf(stderr, "%s: parameter null or not set\n", word ? word : "");
        exit(1);
    }
    return value;
}

// ${parameter:+[word]} - Return word if parameter is set and non-null, else null
const char *variable_store_alternative_value(VariableStore *self, const char *name, const char *word) {
    if (!self || !name) return "";
    const char *value = variable_store_get_variable(self, name);
    if (value && *value != '\0') {
        return word ? word : "";
    }
    return "";
}

// ${#parameter} - Return length of parameter's value
size_t variable_store_length(VariableStore *self, const char *name) {
    if (!self || !name) return 0;
    const char *value = variable_store_get_variable(self, name);
    return value ? strlen(value) : 0;
}

// ${parameter%[word]} or ${parameter%%[word]} - Remove shortest/longest suffix matching pattern
const char *variable_store_remove_suffix(VariableStore *self, const char *name, const char *pattern, bool longest) {
    static char result[1024]; // Static buffer for result
    result[0] = '\0';
    
    if (!self || !name) return result;
    const char *value = variable_store_get_variable(self, name);
    if (!value || !pattern) {
        strncpy(result, value ? value : "", sizeof(result) - 1);
        result[sizeof(result) - 1] = '\0';
        return result;
    }

    strncpy(result, value, sizeof(result) - 1);
    result[sizeof(result) - 1] = '\0';
    
    if (pattern_matches(value, pattern)) {
        size_t value_len = strlen(value);
        size_t pattern_len = strlen(pattern);
        
        if (pattern[0] == '*' && pattern_len > 1) {
            const char *suffix = pattern + 1;
            size_t suffix_len = pattern_len - 1;
            char *match = strstr(value, suffix);
            if (match && (!longest || (longest && match == value + value_len - suffix_len))) {
                size_t pos = match - value;
                result[pos] = '\0';
            }
        } else if (value_len >= pattern_len) {
            char *end = result + value_len - pattern_len;
            if (strcmp(end, pattern) == 0 && (!longest || longest)) {
                *end = '\0';
            }
        }
    }
    
    return result;
}

// ${parameter#[word]} or ${parameter##[word]} - Remove shortest/longest prefix matching pattern
const char *variable_store_remove_prefix(VariableStore *self, const char *name, const char *pattern, bool longest) {
    static char result[1024]; // Static buffer for result
    result[0] = '\0';
    
    if (!self || !name) return result;
    const char *value = variable_store_get_variable(self, name);
    if (!value || !pattern) {
        strncpy(result, value ? value : "", sizeof(result) - 1);
        result[sizeof(result) - 1] = '\0';
        return result;
    }

    strncpy(result, value, sizeof(result) - 1);
    result[sizeof(result) - 1] = '\0';
    
    if (pattern_matches(value, pattern)) {
        size_t value_len = strlen(value);
        size_t pattern_len = strlen(pattern);
        
        if (pattern[pattern_len - 1] == '*' && pattern_len > 1) {
            const char *prefix = pattern;
            size_t prefix_len = pattern_len - 1;
            if (strncmp(value, prefix, prefix_len) == 0 && (!longest || longest)) {
                strncpy(result, value + prefix_len, sizeof(result) - 1);
                result[sizeof(result) - 1] = '\0';
            }
        } else if (value_len >= pattern_len) {
            if (strncmp(result, pattern, pattern_len) == 0 && (!longest || longest)) {
                strncpy(result, value + pattern_len, sizeof(result) - 1);
                result[sizeof(result) - 1] = '\0';
            }
        }
    }
    
    return result;
}
