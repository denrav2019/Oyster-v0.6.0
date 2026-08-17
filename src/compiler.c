// // ============================================================
// compiler.c — Oyster Compiler v0.6.0
// ============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vm.h"

#define STRING_POOL_SIZE 256
#define CODE_BUF_SIZE 262144
#define STR_POOL_BUF_SIZE 131072
#define NESTING_MAX 32
#define MAX_CONST 256
#define HEADER_LEN 59
#define _GNU_SOURCE
#define HASH_SIZE 64  // степень двойки

#define UNICODE_MARKER "\x5C\x75\x75\x00"
#define UNICODE_MARKER_LEN 4
typedef struct {
    const char* key;
    int index;
} HashEntry;

static HashEntry hash_table[HASH_SIZE];
static int hash_initialized = 0;

// Forward declarations
static int is_alpha(char c);
static int strncasecmp_ascii(const char* a, const char* b, int n);

static int g_in_function = 0;  // 1 = мы внутри тела функции
//static int* g_func_lc = NULL;  // указатель на lc текущей функции
static char* string_pool[STRING_POOL_SIZE];
static int string_pool_count = 0;
char str_pool_buf[STR_POOL_BUF_SIZE] = {0};
int str_pool_pos = 0;

typedef struct {
    char* name;
    int index;
    int is_local;
} VarEntry;

static VarEntry* saved_global_var_table = NULL;
static int saved_global_var_count = 0;
static int saved_global_var_capacity = 0;
static int g_func_lc = 0;  // ДОЛЖНО БЫТЬ int, не int*

static VarEntry* var_table = NULL;
static int var_count = 0;
static int var_capacity = 0;

typedef enum { CTX_IF } CtxType;

typedef struct {
    CtxType type;
    int entry;
    int exit_false;
    int exit_true;
    int has_else;
    int jump_outs[16];      // позиции <j:...> для выхода из тел if/elseif
    int jump_out_count;
} CtxFrame;

static CtxFrame ctx_stack[NESTING_MAX];
static int ctx_depth = 0;

typedef struct {
    char name[128];
    char module_name[64];
    int name_idx;
    int param_count;
    int local_count;
    int code_offset;       // будет заполнено в конце
    int export_flag;
    char* func_code;       // временный буфер с байт-кодом функции
    int func_code_len;     // длина кода функции
} FuncDef;

static FuncDef func_table[FUNC_TABLE_MAX];
static int func_count = 0;

typedef struct {
    char* name;
    Value value;
} ConstDef;
static ConstDef const_table[MAX_CONST];
static int const_count = 0;

typedef struct {
    char alias[64];
    char module_name[64];
} AliasMap;
static AliasMap alias_map[64];
static int alias_count = 0;

// Кэш загруженных модулей
static char loaded_module_names[64][64];
static int loaded_module_count = 0;

typedef struct {
    int loop_start;
    int loop_continue;
    int loop_end;
    int body_start;
    char label[64];
    int last_jumps[16];
    int last_count;
    int next_jumps[16];
    int next_count;
    int redo_jumps[16];
    int redo_count;
} LoopInfo;

static LoopInfo loop_stack[32];
static int loop_depth = 0;

// Параметры текущей функции (для правильной индексации)
static char* current_func_params[16];
static int current_func_param_count = 0;


static char const_pool_buf[65536];
static int const_pool_pos = 0;
static char tdf_buf[65536];
static int tdf_pos = 0;

// Forward declarations
void compile_to_bytecode(const char* source, uint8_t** out_code, int* out_size, int source_mode, int extended_mode);
static int parse_or(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_statement(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code, int source_mode);
static Value load_module_constant(const char* module_name, const char* const_name);

static char* my_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

static unsigned int hash_str(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

static int add_string(const char* str) {
    if (!str) return 0;

    if (!hash_initialized) {
        memset(hash_table, 0, sizeof(hash_table));
        hash_initialized = 1;
    }

    unsigned int h = hash_str(str);
    int idx = h & (HASH_SIZE - 1);

    while (hash_table[idx].key != NULL) {
        if (strcmp(hash_table[idx].key, str) == 0) {
            return hash_table[idx].index;
        }
        idx = (idx + 1) & (HASH_SIZE - 1);
    }

    int new_idx = string_pool_count;
    string_pool[new_idx] = my_strdup(str);
    string_pool_count++;

    hash_table[idx].key = string_pool[new_idx];
    hash_table[idx].index = new_idx;

    return new_idx;
}

void free_string_pool(void) {
    for (int i = 0; i < string_pool_count; i++) free(string_pool[i]);
    string_pool_count = 0;
    memset(hash_table, 0, sizeof(hash_table));
    hash_initialized = 0;
}

static int get_var_index(const char* name, int is_local) {
    // Сначала ищем переменную в таблице
    for (int i = 0; i < var_count; i++) {
        if (strcmp(var_table[i].name, name) == 0) {
            return i;
        }
    }

    // Переменная не найдена — создаём новую
    if (is_local) {
        // Создаём локальную переменную (my или параметр)
        if (!g_in_function) {
            fprintf(stderr, "Cannot create local variable '$%s' outside function. Use 'my' only inside functions.\n", name);
            exit(1);
        }

        if (var_count >= var_capacity) {
            var_capacity = var_capacity == 0 ? 16 : var_capacity * 2;
            var_table = realloc(var_table, var_capacity * sizeof(VarEntry));
        }

        var_table[var_count].name = my_strdup(name);
        var_table[var_count].index = var_count;
        var_table[var_count].is_local = 1;
        var_count++;

        // Увеличиваем счётчик локальных переменных
        if (g_in_function) {
            g_func_lc++;
        }

        return var_count - 1;
    }

    // is_local = 0 — ищем в глобальной области или создаём глобальную
    if (g_in_function) {
        // Мы внутри функции, но переменная не локальная — ищем в глобальной таблице
        if (saved_global_var_table != NULL) {
            for (int i = 0; i < saved_global_var_count; i++) {
                if (strcmp(saved_global_var_table[i].name, name) == 0) {
                    // Глобальная переменная существует — возвращаем её индекс
                    // Но её нет в текущей var_table, нужно создать запись-ссылку
                    if (var_count >= var_capacity) {
                        var_capacity = var_capacity == 0 ? 16 : var_capacity * 2;
                        var_table = realloc(var_table, var_capacity * sizeof(VarEntry));
                    }

                    var_table[var_count].name = my_strdup(name);
                    var_table[var_count].index = i;  // индекс в глобальной таблице
                    var_table[var_count].is_local = 0;  // это глобальная переменная
                    var_count++;

                    return var_count - 1;
                }
            }
        }

        // Глобальная переменная не найдена — ошибка
        fprintf(stderr, "Variable '$%s' not found in global scope.\n", name);
        exit(1);
    } else {
        // Основной код — создаём глобальную переменную
        if (var_count >= var_capacity) {
            var_capacity = var_capacity == 0 ? 16 : var_capacity * 2;
            var_table = realloc(var_table, var_capacity * sizeof(VarEntry));
        }

        var_table[var_count].name = my_strdup(name);
        var_table[var_count].index = var_count;
        var_table[var_count].is_local = 0;
        var_count++;

        return var_count - 1;
    }
}

static int strncasecmp_ascii(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca - cb;
        if (ca == '\0') return 0;
    }
    return 0;
}


static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Проверка ключевого слова (регистронезависимо)
static int is_keyword(const char* p, const char* kw, int len) {
    int cmp = strncasecmp_ascii(p, kw, len);
    int alpha = is_alpha(p[len]);
    return cmp == 0 && !alpha;
}

static void ctx_push(CtxType type) {
    CtxFrame* f = &ctx_stack[ctx_depth++];
    f->type = type;
    f->entry = 0;
    f->exit_false = 0;
    f->exit_true = 0;
    f->has_else = 0;
    f->jump_out_count = 0;
}

static CtxFrame* ctx_top(void) {
    return ctx_depth > 0 ? &ctx_stack[ctx_depth - 1] : NULL;
}

static void ctx_pop(void) {
    if (ctx_depth > 0) ctx_depth--;
}

static void emit_source_comment(char* code_buf, int* code_pos, const char* line, int source_mode) {
    if (!source_mode || !line || !*line) return;
    const char* end = line; while (*end && *end != '\n') end++;
    while (end > line && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r')) end--;
    int len = end - line;
    if (len <= 0 || len > 200) return;
    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, " #");
    for (int i = 0; i < len; i++) code_buf[*code_pos + i] = line[i];
    *code_pos += len;
    code_buf[(*code_pos)++] = '\n';
}

static void patch_jump(char* code_buf, int pos, int target) {
    char tmp[9]; snprintf(tmp, sizeof(tmp), "%08X", target);
    memcpy(code_buf + pos + 3, tmp, 8);
}

// === Конструкторы значений ===
static Value make_number(int64_t hi, uint32_t lo) {
    Value v; v.hi = hi; v.lo = lo;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_NUMBER; return v;
}

static Value make_string(int str_idx) {
    Value v; v.hi = (int64_t)str_idx; v.lo = 0;
    v.pad[0] = v.pad[1] = v.pad[2] = 0;
    v.tag = V_STRING; return v;
}

// === Пул констант и ТДФ ===
static void add_const_to_pool(const char* name, Value value) {
    if (value.tag == V_NUMBER) {
        const_pool_pos += snprintf(const_pool_buf + const_pool_pos,
                                   sizeof(const_pool_buf) - const_pool_pos,
                                   "<d:%s:%016llX:%08X>\n",
                                   name,
                                   (unsigned long long)value.hi,
                                   value.lo);
    } else if (value.tag == V_STRING) {
        const char* str = string_pool[(int)value.hi];
        int len = strlen(str);
        const_pool_pos += snprintf(const_pool_buf + const_pool_pos,
                                   sizeof(const_pool_buf) - const_pool_pos,
                                   "<s:%s:%04X>%s\n",
                                   name, len, str);
    }
}

// Формат ТДФ: <f:NAME_IDX:MODULE_IDX:PARAM_COUNT:LOCAL_COUNT:CODE_OFFSET:EXPORT_FLAG>
// MODULE_IDX = 0 для локальных функций
// НОВЫЙ ФОРМАТ ТДФ: <f:func_name:module_name:pc:lc:offset:export>
// Записи разделены \n для удобства парсинга
// НОВЫЙ ФОРМАТ ТДФ: <f:func_name:module_name:pc:lc:offset:export>
static void add_func_to_tdf(const char* name, const char* module_name, int pc, int lc, int off, int exp) {
    tdf_pos += snprintf(tdf_buf + tdf_pos, sizeof(tdf_buf) - tdf_pos,
                        "<f:%s:%s:%02X:%02X:%08X:%X>\n",
                        name,
                        (module_name && module_name[0]) ? module_name : "",
                        pc, lc, off, exp);
}

static int compile_module(const char* module_name) {
    // Проверяем, не загружен ли модуль
    for (int i = 0; i < loaded_module_count; i++) {
        if (strcmp(loaded_module_names[i], module_name) == 0) {
            return 1;  // уже загружен
        }
    }

    fprintf(stderr, "compile_module: %s\n", module_name);
    char filename[256]; FILE* f = NULL;
    snprintf(filename, sizeof(filename), "%s.osm", module_name);
    f = fopen(filename, "r");
    if (!f) { snprintf(filename, sizeof(filename), "./modules/%s.osm", module_name); f = fopen(filename, "r"); }
    if (!f) { snprintf(filename, sizeof(filename), "%s.ocm", module_name); f = fopen(filename, "rb"); if (f) { fclose(f); return 1; } }
    if (!f) return 0;

    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char* source = malloc(len + 1);
    fread(source, 1, len, f);
    source[len] = '\0';
    fclose(f);
    // Сохраняем состояние
    VarEntry* svt = var_table; int svc = var_count, svcp = var_capacity;
    int sfc = func_count;
    FuncDef* sft = malloc(func_count * sizeof(FuncDef));
    memcpy(sft, func_table, func_count * sizeof(FuncDef));
    // Освобождаем старые func_code
    for (int i = 0; i < sfc; i++) {
        if (func_table[i].func_code) {
            free(func_table[i].func_code);
            func_table[i].func_code = NULL;
        }
    }
    // func_code — указатели, их не копируем глубоко, они будут освобождены
    /*
    for (int i = 0; i < func_count; i++) {
        func_table[i].func_code = NULL;
        func_table[i].func_code_len = 0;
    }*/
    func_count = 0;
    int ssc = string_pool_count; char** ssp = malloc(string_pool_count * sizeof(char*));
    for (int i = 0; i < string_pool_count; i++) ssp[i] = string_pool[i];
    int scc = const_count; ConstDef* sct = malloc(const_count * sizeof(ConstDef));
    memcpy(sct, const_table, const_count * sizeof(ConstDef));
    int sac = alias_count; AliasMap* sam = malloc(alias_count * sizeof(AliasMap));
    memcpy(sam, alias_map, alias_count * sizeof(AliasMap));

    var_table = NULL; var_count = 0; var_capacity = 0;
    func_count = 0; string_pool_count = 0; const_count = 0; alias_count = 0;
    uint8_t* bytecode = NULL; int bytecode_size = 0;
    compile_to_bytecode(source, &bytecode, &bytecode_size, 0, 0);

    snprintf(filename, sizeof(filename), "%s.ocm", module_name);
    FILE* out = fopen(filename, "w");
    if (out) { fwrite(bytecode, 1, bytecode_size, out); fclose(out); }

    // Восстанавливаем состояние
    var_table = svt;
    var_count = svc;
    var_capacity = svcp;

    memcpy(func_table, sft, sfc * sizeof(FuncDef));
    func_count = sfc;
    free(sft);

    for (int i = 0; i < string_pool_count; i++) free(string_pool[i]);
    string_pool_count = ssc;
    for (int i = 0; i < ssc; i++) string_pool[i] = ssp[i];
    free(ssp);


    // Перестраиваем хеш-таблицу после восстановления пула строк
    memset(hash_table, 0, sizeof(hash_table));
    for (int i = 0; i < string_pool_count; i++) {
        fprintf(stderr, "  string_pool[%d] = %p\n", i, (void*)string_pool[i]);
        unsigned int h = hash_str(string_pool[i]);
        int hi = h & (HASH_SIZE - 1);
        while (hash_table[hi].key != NULL) {
            hi = (hi + 1) & (HASH_SIZE - 1);
        }
        hash_table[hi].key = string_pool[i];
        hash_table[hi].index = i;
    }

    memcpy(const_table, sct, scc * sizeof(ConstDef)); const_count = scc; free(sct);
    memcpy(alias_map, sam, sac * sizeof(AliasMap)); alias_count = sac; free(sam);
        //fprintf(stderr, "DEBUG before free: source=%p, bytecode=%p\n", (void*)source, (void*)bytecode);
    //free(source);
    //free(bytecode);

    // Сохраняем имя модуля в кэш
    strcpy(loaded_module_names[loaded_module_count], module_name);
    loaded_module_count++;
    return 1;
}

// === Ленивый резолвинг внешней функции ===
static int resolve_external_function(const char* alias, const char* func_name) {
    // Найти модуль по алиасу
    char module_name[64] = {0};
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_map[i].alias, alias) == 0) {
            strcpy(module_name, alias_map[i].module_name);
            break;
        }
    }
    if (module_name[0] == '\0') {
        fprintf(stderr, "Unknown module alias '%s'\n", alias);
        return -1;
    }

    // Проверить, нет ли уже такой функции в func_table
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s.%s", alias, func_name);
    for (int i = 0; i < func_count; i++) {
        if (strcmp(func_table[i].name, full_name) == 0) {
            return i;
        }
    }

    // Найти и открыть .ocm модуля
    char filename[256];
    FILE* f = NULL;
    snprintf(filename, sizeof(filename), "%s.ocm", module_name);
    f = fopen(filename, "rb");
    if (!f) {
        snprintf(filename, sizeof(filename), "./modules/%s.ocm", module_name);
        f = fopen(filename, "rb");
    }
    if (!f) {
        fprintf(stderr, "Module '%s' not found\n", module_name);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    if (size < 6 || memcmp(data, "<OYCE;", 6) != 0) {
        free(data);
        return -1;
    }

    // Парсим заголовок
    const char* ptr = (char*)data + 6;
    const char* he = strchr(ptr, '>');
    if (!he) { free(data); return -1; }

    size_t hl = he - ptr;
    char* hb = malloc(hl + 1);
    memcpy(hb, ptr, hl);
    hb[hl] = '\0';

    uint32_t tdf_off = 0;
    char* tok = strtok(hb, ";");
    while (tok) {
        if (tok[0] == 'F') tdf_off = strtoul(tok + 2, NULL, 16);
        tok = strtok(NULL, ";");
    }
    free(hb);

    if (!tdf_off) { free(data); return -1; }

    // Ищем функцию в ТДФ модуля (новый формат: <f:name:module:pc:lc:offset:export>)
    const uint8_t* tp = data + tdf_off;
    int found = 0;
    int found_param_count = 0;
    int found_local_count = 0;

    while (tp < data + size) {
        if (tp[0] == '<' && tp[1] == 'f' && tp[2] == ':') {
            tp += 3;

            // Читаем имя функции до ':'
            char fname[128]; int i = 0;
            while (tp < data + size && *tp != ':') fname[i++] = *tp++;
            fname[i] = '\0';
            if (*tp == ':') tp++;

            // Читаем имя модуля до ':' (пропускаем, не нужно для поиска)
            while (tp < data + size && *tp != ':') tp++;
            if (*tp == ':') tp++;

            // PARAM_COUNT (2 hex)
            char tmp[3]; memcpy(tmp, tp, 2); tmp[2] = '\0';
            int param_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // LOCAL_COUNT (2 hex)
            memcpy(tmp, tp, 2); tmp[2] = '\0';
            int local_count = strtoul(tmp, NULL, 16);
            tp += 2;
            if (*tp == ':') tp++;

            // CODE_OFFSET (8 hex) — пропускаем
            tp += 8;
            if (*tp == ':') tp++;

            // EXPORT_FLAG (до '>')
            int export_flag = 0;
            if (*tp >= '0' && *tp <= '9') export_flag = *tp - '0';
            else if (*tp >= 'A' && *tp <= 'F') export_flag = *tp - 'A' + 10;
            tp++;

            // Ищем '>' (конец записи)
            while (tp < data + size && *tp != '>') tp++;
            if (*tp == '>') tp++;

            // Проверяем: функция экспортирована и имя совпадает
            if (export_flag && strcmp(fname, func_name) == 0) {

                found = 1;
                found_param_count = param_count;
                found_local_count = local_count;
                break;
            }
        } else {
            tp++;
        }
    }

    free(data);
    if (!found) {
        fprintf(stderr, "Function '%s' not found in module '%s'\n", func_name, module_name);
        return -1;
    }

    // Добавляем функцию в func_table текущего модуля
    if (func_count >= FUNC_TABLE_MAX) return -1;

    FuncDef* fd = &func_table[func_count];
    strcpy(fd->name, full_name);
    strcpy(fd->module_name, module_name);
    fd->name_idx = add_string(func_name);
    fd->param_count = found_param_count;
    fd->local_count = found_local_count;
    fd->code_offset = 0;
    fd->export_flag = 0;

    add_func_to_tdf(func_name, module_name, found_param_count, found_local_count, 0, 0);

    return func_count++;
}

// === Парсер выражений ===
static int parse_atom(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_cmp(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_expr(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_term(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_bit_and(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_bit_xor(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_bit_or(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);
static int parse_and(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code);

static int parse_atom(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    while (**p == ' ') (*p)++;

    // Шестнадцатеричное число: 0x...
    if (**p == '0' && (*(*p+1) == 'x' || *(*p+1) == 'X')) {
        (*p) += 2;  // пропускаем 0x
        const char* saved = *p;
        while ((**p >= '0' && **p <= '9') || (**p >= 'A' && **p <= 'F') || (**p >= 'a' && **p <= 'f')) {
            (*p)++;
        }
        int len = *p - saved;
        if (len == 0) { fprintf(stderr, "Expected hex digits\n"); return 0; }

        char* hex = malloc(len + 1);
        memcpy(hex, saved, len);
        hex[len] = '\0';

        uint64_t val = strtoull(hex, NULL, 16);
        free(hex);

        // Для V_NUMBER: hi = целая часть, lo = 0
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(val >> 32));
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(val & 0xFFFFFFFF));
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", 0);
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<(:00000000>");
        *has_code = 1;
        return 1;
    }
    // Число
    if ((**p >= '0' && **p <= '9') || **p == '.') {
        const char* saved = *p; int has_dot = 0;
        while ((**p >= '0' && **p <= '9') || **p == '.') {
            if (**p == '.') { if (has_dot) break; has_dot = 1; } (*p)++;
        }
        int len = *p - saved; if (len == 0) return 0;
        char* ns = malloc(len + 1); memcpy(ns, saved, len); ns[len] = '\0';
        int64_t hi; uint32_t lo;
        if (has_dot) {
            char* dot = strchr(ns, '.'); *dot = '\0'; hi = strtoll(ns, NULL, 10);
            char* frac = dot + 1; int fl = strlen(frac); if (fl > 9) frac[9] = '\0';
            uint64_t fv = strtoull(frac, NULL, 10); uint64_t denom = 1;
            for (int i = 0; i < fl && i < 9; i++) denom *= 10;
            lo = (uint32_t)((((__int128)fv << 32) / denom) & 0xFFFFFFFF);
        } else { hi = strtoll(ns, NULL, 10); lo = 0; }
        free(ns);
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(hi >> 32));
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(hi & 0xFFFFFFFF));
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", lo);
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<(:00000000>");
        *has_code = 1; return 1;
    }
    // Unicode-строка: u"..." или u'...'
    if (**p == 'u' && (*(*p+1) == '"' || *(*p+1) == '\'')) {
        (*p)++;  // пропускаем 'u'
        char quote = **p; (*p)++;

        // Первый проход: считаем длину (все символы → UTF-32 = 4 байта)
        int raw_len = 4;  // маркер "\uu\0"
        const char* tmp = *p;
        while (*tmp && *tmp != quote) {
            if (*tmp == '\\' && *(tmp+1) != '\0') {
                raw_len += 4;  // escape → 1 символ UTF-32
                tmp += 2;
            } else {
                raw_len += 4;
                if ((*tmp & 0x80) == 0) tmp++;
                else if ((*tmp & 0xE0) == 0xC0) tmp += 2;
                else if ((*tmp & 0xF0) == 0xE0) tmp += 3;
                else tmp += 4;
            }
        }

        // Выделяем память
        char* str = malloc(raw_len + 1);
        int i = 0;

        // Маркер Unicode
        memcpy(str + i, UNICODE_MARKER, 4);
        i += 4;

        // Второй проход: преобразуем UTF-8 → UTF-32 (с escape-последовательностями)
        while (**p && **p != quote) {
            uint32_t cp = 0;

            // Обработка escape-последовательностей
            if (**p == '\\' && *(*p+1) != '\0') {
                (*p)++;
                switch (**p) {
                    case 'n': cp = '\n'; break;
                    case 't': cp = '\t'; break;
                    case '\\': cp = '\\'; break;
                    case '"': cp = '"'; break;
                    case '\'': cp = '\''; break;
                    case 'r': cp = '\r'; break;
                    default: cp = **p; break;
                }
                (*p)++;
                // UTF-32 little-endian
                str[i++] = (cp & 0xFF);
                str[i++] = ((cp >> 8) & 0xFF);
                str[i++] = ((cp >> 16) & 0xFF);
                str[i++] = ((cp >> 24) & 0xFF);
                continue;
            }

            // Обычный UTF-8 → UTF-32
            if ((**p & 0x80) == 0) {
                cp = **p; (*p)++;
            } else if ((**p & 0xE0) == 0xC0) {
                cp = (**p & 0x1F) << 6;
                cp |= (*(*p+1) & 0x3F);
                (*p) += 2;
            } else if ((**p & 0xF0) == 0xE0) {
                cp = (**p & 0x0F) << 12;
                cp |= (*(*p+1) & 0x3F) << 6;
                cp |= (*(*p+2) & 0x3F);
                (*p) += 3;
            } else if ((**p & 0xF8) == 0xF0) {
                cp = (**p & 0x07) << 18;
                cp |= (*(*p+1) & 0x3F) << 12;
                cp |= (*(*p+2) & 0x3F) << 6;
                cp |= (*(*p+3) & 0x3F);
                (*p) += 4;
            }

            // UTF-32 little-endian
            str[i++] = (cp & 0xFF);
            str[i++] = ((cp >> 8) & 0xFF);
            str[i++] = ((cp >> 16) & 0xFF);
            str[i++] = ((cp >> 24) & 0xFF);
        }
        str[i] = '\0';
        int len = i;

        if (**p == quote) (*p)++;

        // Добавляем в string_pool (используем memcpy, не %s)
        add_string(str);

        // Определяем индекс в str_pool_buf
        int buf_idx = -1;
        const char* bp = str_pool_buf;
        const char* bend = str_pool_buf + *str_pool_pos;
        int count = 0;

        while (bp < bend) {
            if (bp[0] == '<' && bp[1] == 'l' && bp[2] == ':') {
                bp += 3;
                char len_str[5] = {bp[0], bp[1], bp[2], bp[3], '\0'};
                int blen = (int)strtoul(len_str, NULL, 16);
                bp += 4;
                if (*bp != '>') break;
                bp++;
                if (blen == len && memcmp(bp, str, len) == 0) {
                    buf_idx = count;
                    break;
                }
                bp += blen;
                count++;
            } else break;
        }

        if (buf_idx < 0) {
            buf_idx = count;
            int hdr = snprintf(str_pool_buf + *str_pool_pos, STR_POOL_BUF_SIZE - *str_pool_pos, "<l:%04X>", len);
            *str_pool_pos += hdr;
            memcpy(str_pool_buf + *str_pool_pos, str, len);
            *str_pool_pos += len;
        }

        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<s:%08X>", buf_idx);
        *has_code = 1;
        free(str);
        return 1;
    }

    // gets() — чтение строки со стандартного ввода
    if (is_keyword(*p, "gets", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<r:00000000>");
        *has_code = 1;
        return 1;
    }

    // Строка
    if (**p == '"' || **p == '\'') {
        char quote = **p; (*p)++;
        const char* start = *p;

        // Сначала считаем длину с учётом escape-последовательностей
        int raw_len = 0;
        int esc_count = 0;
        const char* tmp = *p;
        while (*tmp && *tmp != quote) {
            if (*tmp == '\\' && *(tmp+1) != '\0') {
                if (*(tmp+1) == 'u') {
                    raw_len += 4;  // маркер "\uu\0"
                    tmp += 2;
                    // Считаем UTF-8 символы как UTF-32 (4 байта каждый)
                    while (*tmp && *tmp != quote) {
                        raw_len += 4;
                        // Пропускаем один UTF-8 символ
                        if ((*tmp & 0x80) == 0) tmp++;
                        else if ((*tmp & 0xE0) == 0xC0) tmp += 2;
                        else if ((*tmp & 0xF0) == 0xE0) tmp += 3;
                        else tmp += 4;
                    }
                    continue;
                }
            } else {
                tmp++;
            }
            raw_len++;
        }

        // Выделяем память под распарсенную строку (без \)
        int len = raw_len;
        char* str = malloc(len + 1);
        int i = 0;

        while (**p && **p != quote) {
            if (**p == '\\' && *(*p+1) != '\0') {
                (*p)++;
                switch (**p) {
                    case 'n': str[i++] = '\n'; break;
                    case 't': str[i++] = '\t'; break;
                    case '\\': str[i++] = '\\'; break;
                    case '"': str[i++] = '"'; break;
                    case '\'': str[i++] = '\''; break;
                    case 'r': str[i++] = '\r'; break;
                    default: str[i++] = **p; break;
                }
                (*p)++;
            } else {
                str[i++] = **p;
                (*p)++;
            }
        }
//
        str[i] = '\0';
        len = i;

        if (**p == quote) (*p)++;

        // Добавляем в string_pool через хеш-таблицу
        add_string(str);

        // Определяем индекс в str_pool_buf
        int buf_idx = -1;
        const char* bp = str_pool_buf;
        const char* bend = str_pool_buf + *str_pool_pos;
        int count = 0;

        while (bp < bend) {
            if (bp[0] == '<' && bp[1] == 'l' && bp[2] == ':') {
                bp += 3;
                char len_str[5] = {bp[0], bp[1], bp[2], bp[3], '\0'};
                int blen = (int)strtoul(len_str, NULL, 16);
                bp += 4;
                if (*bp != '>') break;
                bp++;
                if (blen == len && memcmp(bp, str, len) == 0) {
                    buf_idx = count;
                    break;
                }
                bp += blen;
                count++;
            } else break;
        }

        if (buf_idx < 0) {
            buf_idx = count;
            int hdr = snprintf(str_pool_buf + *str_pool_pos, STR_POOL_BUF_SIZE - *str_pool_pos, "<l:%04X>", len);
            *str_pool_pos += hdr;
            memcpy(str_pool_buf + *str_pool_pos, str, len);
            *str_pool_pos += len;
        }

        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<s:%08X>", buf_idx);
        *has_code = 1;
        free(str);
        return 1;
    }

    // Константа &math.pi или &pi
    if (**p == '&') {
        (*p)++; char cn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' &&
               **p != '+' && **p != '-' && **p != '*' && **p != '/' &&
               **p != '%' && **p != '=' && **p != '<' && **p != '>' && **p != ';') {
            cn[i++] = **p; (*p)++;
        }
        cn[i] = '\0';
        char* dot = strchr(cn, '.');
        if (dot) {
            *dot = '\0'; char* ma = cn; char* cname = dot + 1;
            for (int j = 0; j < alias_count; j++) {
                if (strcmp(alias_map[j].alias, ma) == 0) {
                    Value val = load_module_constant(alias_map[j].module_name, cname);
                    if (val.tag != V_UNDEF) {
                        if (val.tag == V_NUMBER) {
                            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(val.hi >> 32));
                            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(val.hi & 0xFFFFFFFF));
                            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", val.lo);
                            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<(:00000000>");
                        } else if (val.tag == V_STRING) {
                            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<s:%08X>", (int)val.hi);
                        }
                        *has_code = 1; return 1;
                    }
                }
            }
            fprintf(stderr, "Module constant '&%s.%s' not found\n", ma, cname);
            return 0;
        }
        // Локальная константа
        for (int j = 0; j < const_count; j++) {
            if (strcmp(const_table[j].name, cn) == 0) {
                Value cv = const_table[j].value;
                if (cv.tag == V_NUMBER) {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(cv.hi >> 32));
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(cv.hi & 0xFFFFFFFF));
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", cv.lo);
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<(:00000000>");
                } else if (cv.tag == V_STRING) {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<s:%08X>", (int)cv.hi);
                }
                *has_code = 1; return 1;
            }
        }
        fprintf(stderr, "Constant '&%s' not found\n", cn);
        return 0;
    }
    // get8(var, byte_index) — прочитать 1 байт
    if (is_keyword(*p, "get8", 4)) {

        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — переменная (строка/массив/хеш)
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }

        // Второй аргумент — индекс
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<{:00000000>");
        *has_code = 1;
        return 1;
    }

    // get16(var, byte_index) — прочитать 2 байта
    if (is_keyword(*p, "get16", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<}:00000000>");
        *has_code = 1;
        return 1;
    }

    // get32(var, byte_index) — прочитать 2 байта
    if (is_keyword(*p, "get32", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<~:00000000>");
        *has_code = 1;
        return 1;
    }


    // get64(var, byte_index) — прочитать 2 байта
    if (is_keyword(*p, "get64", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<_:00000000>");
        *has_code = 1;
        return 1;
    }
    // set8(var, byte_index, value) — записать 1 байт
    if (is_keyword(*p, "set8", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<;:00000000>");
        *has_code = 1;
        return 1;
    }
    // set16(var, byte_index, value) — записать 2 байт
    if (is_keyword(*p, "set16", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<,:00000000>");
        *has_code = 1;
        return 1;
    }
    // set32(var, byte_index, value) — записать 4 байт
    if (is_keyword(*p, "set32", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<.:00000000>");
        *has_code = 1;
        return 1;
    }

    // set64(var, byte_index, value) — записать 8 байт

    if (is_keyword(*p, "set64", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<`:00000000>");
        *has_code = 1;
        return 1;
    }

    // Переменная $
    if (**p == '$') {
        char vn[64]; int i = 0; (*p)++;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' &&
            **p != '+' && **p != '-' && **p != '*' && **p != '/' &&
            **p != '%' && **p != '=' && **p != '<' && **p != '>' && **p != ';' && **p != ',' && **p != '.' && **p != ']') {
            vn[i++] = **p; (*p)++;
            }
            vn[i] = '\0';
        if (strchr(vn, '.') || (strlen(vn) == 1 && isupper(vn[0]))) {
            *p -= i;
            *has_code = 0;
            return 1;
        }

        int is_param = 0;
        int local_idx = 0;
        for (int i = 0; i < current_func_param_count; i++) {
            if (strcmp(vn, current_func_params[i]) == 0) {
                is_param = 1;
                local_idx = i;
                break;
            }
        }

        int idx;
        int is_local = 0;
        if (is_param) {
            is_local = 1;
            idx = local_idx;
        } else {
            idx = get_var_index(vn, 0);
            if (g_in_function && var_table[idx].is_local) {
                is_local = 1;
            }
        }

        if (is_local) {
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
        } else {
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X>", idx);
        }
        *has_code = 1;
        return 1;
    }

    // abs(x) — абсолютное значение
    if (is_keyword(*p, "abs", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<B:00000000>");
        *has_code = 1;
        return 1;
    }

    // sign(x) — знак числа (-1, 0, 1)
    if (is_keyword(*p, "sign", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<C:00000000>");
        *has_code = 1;
        return 1;
    }

    // round(x, n, mode)
    if (is_keyword(*p, "round", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — число
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }

        // Второй аргумент — n
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }

        // Третий аргумент — mode
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<X:00000000>");
        *has_code = 1;
        return 1;
    }

    // int(x) — целая часть числа
    if (is_keyword(*p, "int", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<I:00000000>");
        *has_code = 1;
        return 1;
    }

    // Файловые функции: fopen, fclose, freadline, fread, fprint
    if ((is_keyword(*p, "fopen", 5)) ||
        (is_keyword(*p, "fclose", 6)) ||
        (is_keyword(*p, "freadline", 9)) ||
        (is_keyword(*p, "fread", 5)) ||
        (is_keyword(*p, "fprint", 6))||
        (is_keyword(*p, "feof", 4)) ||
        (is_keyword(*p, "ftell", 5)) ||
        (is_keyword(*p, "fseek", 5)) ||
        (is_keyword(*p, "sysread", 7)) ||
        (is_keyword(*p, "syswrite", 8)) ||
        (is_keyword(*p, "frename", 7)) ||
        (is_keyword(*p, "funlink", 7)) ||
        (is_keyword(*p, "socket", 6)) ||
        (is_keyword(*p, "connect", 7)) ||
        (is_keyword(*p, "bind", 4)) ||
        (is_keyword(*p, "listen", 6)) ||
        (is_keyword(*p, "accept", 6)) ||
        (is_keyword(*p, "send", 4)) ||
        (is_keyword(*p, "recv", 4)) ||
        (is_keyword(*p, "sockclose", 9)) ||
        (is_keyword(*p, "serial_open", 11)) ||
        (is_keyword(*p, "serial_read", 11)) ||
        (is_keyword(*p, "serial_close", 12)) ||
        (is_keyword(*p, "popen", 5)) ||
        (is_keyword(*p, "pclose", 6)) ||
        (is_keyword(*p, "pipe_write", 10)) ||
        (is_keyword(*p, "pipe_read", 9)) ||
        (is_keyword(*p, "popen2", 6)) ||
        (is_keyword(*p, "pipe_close", 10))

    ) {

        int func_id = -1;

    if (strncmp(*p, "fopen", 5) == 0) {
        func_id = 0;
        *p += 5;
    }

    else if (strncmp(*p, "fclose", 6) == 0) { func_id = 1; *p += 6; }
    else if (strncmp(*p, "freadline", 9) == 0) { func_id = 2; *p += 9; }
    else if (strncmp(*p, "fread", 5) == 0)  { func_id = 3; *p += 5; }
    else if (strncmp(*p, "fprint", 6) == 0) { func_id = 4; *p += 6; }
    else if (strncmp(*p, "sysread", 7) == 0)  { func_id = 5; *p += 7; }
    else if (strncmp(*p, "syswrite", 8) == 0) { func_id = 6; *p += 8; }
    else if (strncmp(*p, "frename", 7) == 0)  { func_id = 7; *p += 7; }
    else if (strncmp(*p, "funlink", 7) == 0)  { func_id = 8; *p += 7; }
    else if (strncmp(*p, "fseek", 5) == 0)  {
        func_id = 9; *p += 5; }
    else if (strncmp(*p, "ftell", 5) == 0)  { func_id = 10; *p += 5; }
    else if (strncmp(*p, "feof", 4) == 0)  { func_id = 11; *p += 4; }

    else if (strncmp(*p, "socket", 6) == 0)    { func_id = 12; *p += 6; }
    else if (strncmp(*p, "connect", 7) == 0)   { func_id = 13; *p += 7; }
    else if (strncmp(*p, "bind", 4) == 0)      { func_id = 14; *p += 4; }
    else if (strncmp(*p, "listen", 6) == 0)    { func_id = 15; *p += 6; }
    else if (strncmp(*p, "accept", 6) == 0)    { func_id = 16; *p += 6; }
    else if (strncmp(*p, "send", 4) == 0)      { func_id = 17; *p += 4; }
    else if (strncmp(*p, "recv", 4) == 0)      { func_id = 18; *p += 4; }
    else if (strncmp(*p, "sockclose", 9) == 0) { func_id = 19; *p += 9; }

    else if (strncmp(*p, "serial_open", 11) == 0)  { func_id = 29; *p += 11; }
    else if (strncmp(*p, "serial_read", 11) == 0)  { func_id = 30; *p += 11; }
    else if (strncmp(*p, "serial_close", 12) == 0) { func_id = 31; *p += 12; }

    else if (strncmp(*p, "popen2", 6) == 0)      { func_id = 36; *p += 6; }
    else if (strncmp(*p, "popen", 5) == 0)  { func_id = 32; *p += 5; }
    else if (strncmp(*p, "pclose", 6) == 0) { func_id = 33; *p += 6; }
    else if (strncmp(*p, "pipe_write", 10) == 0) { func_id = 34; *p += 10; }
    else if (strncmp(*p, "pipe_read", 9) == 0)  { func_id = 35; *p += 9; }
    else if (strncmp(*p, "pipe_close", 10) == 0) { func_id = 37; *p += 10; }




    while (**p == ' ') (*p)++;
    if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
    (*p)++; while (**p == ' ') (*p)++;

    while (**p != ')') {
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
    }
    if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", func_id);
    *has_code = 1;
    return 1;
    }


    // Массив @
    if (**p == '@') {
        const char* saved_line = *p;
        (*p)++; char vn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != '[' &&
            **p != '+' && **p != '-' && **p != '*' && **p != '/' &&
            **p != '%' && **p != '=' && **p != '<' && **p != '>' && **p != ']' && **p != '.' && **p != ',') {
            vn[i++] = **p; (*p)++;
            }
            vn[i] = '\0';
        int idx = get_var_index(vn, 0);
        int is_local = (g_in_function && var_table[idx].is_local);

        if (**p == '[') {
            (*p)++; while (**p == ' ') (*p)++;

            // Загружаем массив с проверкой is_local
            if (is_local) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
            }

            // Парсим индекс
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            while (**p == ' ') (*p)++;
            if (**p != ']') {
                fprintf(stderr, "Expected ']' (A), got '%c' (code=%d)\n", **p, **p);
                return 0;
            }
            (*p)++;

            // Проверяем, есть ли присваивание
            while (**p == ' ') (*p)++;
            if (**p == '=' && *(*p+1) != '=') {
                // @arr[$i] = value
                (*p)++; while (**p == ' ' ) (*p)++;
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<M:00000000>");
            } else {
                // @arr[$i] — чтение
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<A:00000000>");
            }
            *has_code = 1;
            return 1;
        }

        // Просто @arr (без индекса)
        if (is_local) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
        } else {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
        }
        *has_code = 1;
        return 1;
    }

    // Хеш %
    if (**p == '%') {
        (*p)++; char hn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != '[' &&
            **p != '+' && **p != '-' && **p != '*' && **p != '/' &&
            **p != '%' && **p != '=' && **p != '<' && **p != '>' && **p != ',' && **p != ']'  && **p != '.' && **p != ',') {
            hn[i++] = **p; (*p)++;
            }
            hn[i] = '\0';
        int idx = get_var_index(hn, 0);

        if (**p == '[') {
            (*p)++; while (**p == ' ') (*p)++;

            // Загружаем хеш с проверкой is_local
            int is_local = (g_in_function && var_table[idx].is_local);
            if (is_local) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
            }

            // Парсим индекс
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            while (**p == ' ') (*p)++;
            if (**p != ']') {
                fprintf(stderr, "Expected ']' (B), got '%c' (code=%d)\n", **p, **p);
                return 0;
            }
            (*p)++;

            // Проверяем, есть ли присваивание
            while (**p == ' ') (*p)++;
            if (**p == '=' && *(*p+1) != '=') {
                (*p)++; while (**p == ' ') (*p)++;
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<9:00000000>");
            } else {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<8:00000000>");
            }
            *has_code = 1;
            return 1;
        }

        // Просто %h
        int is_local = (g_in_function && var_table[idx].is_local);
        if (is_local) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
        } else {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
        }
        *has_code = 1;
        return 1;
    }


    // Унарный минус
    if (**p == '-') {
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_atom(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<u:00000000>");
        *has_code = 1;
        return 1;
    }

    // Унарный минус/not/~
    // ДОБАВИТЬ: Унарный not (!)
    if (**p == '!') {
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_atom(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<~:00000000>");  // опкод ~ (not)
        *has_code = 1;
        return 1;
    }

    if (is_keyword(*p, "not", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (!parse_atom(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<~:00000000>");
        *has_code = 1; return 1;
    }

    // array($n) — создание массива заданной длины
    if (is_keyword(*p, "array", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Парсим аргумент (выражение) — кладём количество на стек
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        // Генерируем опкод [ — создать массив из N элементов
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<[:00000000>");
        *has_code = 1;
        return 1;
    }
    // bytearray($n) — создание ByteArray заданной длины
    if (is_keyword(*p, "bytearray", 9)) {
        (*p) += 9; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<=:00000000>");
        *has_code = 1;
        return 1;
    }
    // clone(x) — клонирование массива или хеша
    if (is_keyword(*p, "clone", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Парсим аргумент (массив или хеш)
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        // Генерируем опкод N — clone
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<N:00000000>");
        *has_code = 1;
        return 1;
    }

    // deallocate(x) — освобождение памяти
    if (is_keyword(*p, "deallocate", 10)) {
        (*p) += 10; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Парсим аргумент (переменную)
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        // Генерируем опкод x — deallocate
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 20);
        *has_code = 1;
        return 1;
    }

    // reverse(x) — переворот массива
    if (is_keyword(*p, "reverse", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<R:00000000>");
        *has_code = 1;
        return 1;
    }

    // sort(@arr) — сортировка массива
    if (is_keyword(*p, "sort", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<O:00000000>");
        *has_code = 1;
        return 1;
    }

    // lc(s) — строка в нижний регистр
    if (is_keyword(*p, "lc", 2)) {
        (*p) += 2; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<z:00000000>");
        *has_code = 1;
        return 1;
    }

    // uc(s) — строка в верхний регистр
    if (is_keyword(*p, "uc", 2)) {
        (*p) += 2; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<Z:00000000>");
        *has_code = 1;
        return 1;
    }
    // chr(n) — код символа в символ
    if (is_keyword(*p, "chr", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<5:00000000>");
        *has_code = 1;
        return 1;
    }

    // ord(c) — символ в код
    if (is_keyword(*p, "ord", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<6:00000000>");
        *has_code = 1;
        return 1;
    }

    // chomp(s) — убрать \n в конце строки
    if (is_keyword(*p, "chomp", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<K:00000000>");
        *has_code = 1;
        return 1;
    }

    // chop(s) — убрать последний символ
    if (is_keyword(*p, "chop", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<t:00000000>");
        *has_code = 1;
        return 1;
    }

    // lcfirst(s) — первый символ в нижний регистр
    if (is_keyword(*p, "lcfirst", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<1:00000000>");
        *has_code = 1;
        return 1;
    }

    // ucfirst(s) — первый символ в верхний регистр
    if (is_keyword(*p, "ucfirst", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<2:00000000>");
        *has_code = 1;
        return 1;
    }

    // index(s, sub, pos?) — поиск подстроки
    if (is_keyword(*p, "index", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Парсим аргументы
        int arg_count = 0;
        while (**p != ')') {
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            arg_count++;
            while (**p == ' ') (*p)++;
            if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        }
        if (**p == ',') { (*p)++; }

        if (arg_count < 2 || arg_count > 3) {
            fprintf(stderr, "index() expects 2 or 3 arguments\n");
            return 0;
        }

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<w:%08X>", arg_count);
        *has_code = 1;
        return 1;
    }

    // rindex(s, sub, pos?) — поиск подстроки справа
    if (is_keyword(*p, "rindex", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        int arg_count = 0;
        while (**p != ')') {
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            arg_count++;
            while (**p == ' ') (*p)++;
            if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        }
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        if (arg_count < 2 || arg_count > 3) {
            fprintf(stderr, "rindex() expects 2 or 3 arguments\n");
            return 0;
        }

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<W:%08X>", arg_count);
        *has_code = 1;
        return 1;
    }

    // substr(s, off, len, repl?) — извлечение/замена подстроки
    if (is_keyword(*p, "substr", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        int arg_count = 0;
        while (**p != ')') {
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            arg_count++;
            while (**p == ' ') (*p)++;
            if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        }
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        if (arg_count < 3 || arg_count > 4) {
            fprintf(stderr, "substr() expects 3 or 4 arguments\n");
            return 0;
        }

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<k:%08X>", arg_count);
        *has_code = 1;
        return 1;
    }

    // split(sep, s) — разбить строку в массив
    if (is_keyword(*p, "split", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — разделитель
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }


        while (**p == ' ') (*p)++;

        // Второй аргумент — строка
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<3:00000000>");
        *has_code = 1;
        return 1;
    }

    // join(sep, arr) — собрать массив в строку
    if (is_keyword(*p, "join", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — разделитель
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }


        while (**p == ' ') (*p)++;

        // Второй аргумент — массив
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<4:00000000>");
        *has_code = 1;
        return 1;
    }

    // strcmp(s1, s2) — сравнение строк
    if (is_keyword(*p, "strcmp", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }
        while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<7:00000000>");
        *has_code = 1;
        return 1;
    }


    // haskeys(%h) — проверка наличия ключей
    if (is_keyword(*p, "haskeys", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 21);
        *has_code = 1;
        return 1;
    }


    // getkey(%h, index) — получить ключ по индексу
    if (is_keyword(*p, "getkey", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p != ',') { fprintf(stderr, "Expected ','\n"); return 0; } (*p)++;
        while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 23);
        *has_code = 1;
        return 1;
    }
    if ((is_keyword(*p, "set8", 4)) ||
        (is_keyword(*p, "set16", 5)) ||
        (is_keyword(*p, "set32", 5)) ||
        (is_keyword(*p, "set64", 5)) ||
        (is_keyword(*p, "get8", 4)) ||
        (is_keyword(*p, "get16", 5)) ||
        (is_keyword(*p, "get32", 5)) ||
        (is_keyword(*p, "get64", 5))) {

        if (!parse_term(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        return 1;
        }

    // hadd(%h, value, key?) — добавить элемент в хеш
    if (is_keyword(*p, "hadd", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — хеш
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }


        while (**p == ' ') (*p)++;

        // Второй аргумент — значение
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        int arg_count = 2;
        while (**p == ' ') (*p)++;
        if (**p == ',') {
            (*p)++; while (**p == ' ') (*p)++;
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            arg_count = 3;
        }

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        int hadd_code = (arg_count == 3) ? 25 : 24;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", hadd_code);
        *has_code = 1;
        return 1;
    }

    // hdel(%h) — удалить последний элемент хеша
    if (is_keyword(*p, "hdel", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 26);
        *has_code = 1;
        return 1;
    }



    // frac(x) — дробная часть числа
    if (is_keyword(*p, "frac", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<F:00000000>");
        *has_code = 1;
        return 1;
    }


    // inv(x) — инверсия знака
    if (is_keyword(*p, "inv", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<o:00000000>");
        *has_code = 1;
        return 1;
    }

/*
    // inc($x) — инкремент переменной
    if (is_keyword(*p, "inc", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); return 0; }
        (*p)++; char vn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
        vn[i] = '\0';

        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<0:%08X>", get_var_index(vn, 0));
        *has_code = 1;
        return 1;
    }

    // dec($x) — декремент переменной
    if (is_keyword(*p, "dec", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); return 0; }
        (*p)++; char vn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
        vn[i] = '\0';

        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<@:%08X>", get_var_index(vn, 0));
        *has_code = 1;
        return 1;
    }
*/


    // sqrt(x) — квадратный корень
    if (is_keyword(*p, "sqrt", 4)) {
        (*p) += 4; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<J:00000000>");
        *has_code = 1;
        return 1;
    }


    // setkey(%h, old_key, new_key) — изменить ключ
    if (is_keyword(*p, "setkey", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Первый аргумент — хеш
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        // Разделитель: запятая или пробел
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }
        while (**p == ' ') (*p)++;

        // Второй аргумент — старый ключ
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        // Разделитель: запятая или пробел
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }
        while (**p == ' ') (*p)++;

        // Третий аргумент — новый ключ
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 22);
        *has_code = 1;
        return 1;
    }

    // exp(x) — экспонента
    if (is_keyword(*p, "exp", 3)) {

        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<G:00000000>");
        *has_code = 1;
        return 1;
    }

    // ln(x) — натуральный логарифм
    if (is_keyword(*p, "ln", 2)) {

        (*p) += 2; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<Q:00000000>");
        *has_code = 1;
        return 1;
    }

    // log(x) — десятичный логарифм
    if (is_keyword(*p, "log", 3)) {

        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<T:00000000>");
        *has_code = 1;
        return 1;
    }


    // hvalues(%h) — получить массив значений хеша
    if (is_keyword(*p, "hvalues", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 27);
        *has_code = 1;
        return 1;
    }

    // hkeys(%h) — получить массив ключей хеша
    if (is_keyword(*p, "hkeys", 5)) {
        (*p) += 5; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 28);
        *has_code = 1;
        return 1;
    }


    // len(x) — длина массива или строки
    if (is_keyword(*p, "len", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Смотрим, что за аргумент
        if (**p == '@') {
            // len(@arr)
            (*p)++; char vn[64]; int i = 0;
            while (**p && **p != ')' && **p != ' ') vn[i++] = **p, (*p)++; vn[i] = '\0';
            int idx = get_var_index(vn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);
            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X><L:00000000>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X><L:00000000>", idx);
            }
            *has_code = 1;
        } else if (**p == '%') {
            // len(%hash)
            (*p)++; char hn[64]; int i = 0;
            while (**p && **p != ')' && **p != ' ') hn[i++] = **p, (*p)++; hn[i] = '\0';
            int idx = get_var_index(hn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);
            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X><L:00000000>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X><L:00000000>", idx);
            }
            *has_code = 1;
        } else {
            // len(выражение)
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<L:00000000>");
            *has_code = 1;
        }

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; }
        (*p)++;
        return 1;
    }

    // undef — литерал или undef(x) — проверка
    if (is_keyword(*p, "undef", 5)) {
        (*p) += 5;
        while (**p == ' ') (*p)++;

        if (**p == '(') {
            // undef(x) — проверка
            (*p)++; while (**p == ' ') (*p)++;
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            while (**p == ' ') (*p)++;
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<V:00000000>");
        } else {
            // undef — литерал
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<U:00000000>");
        }
        *has_code = 1;
        return 1;
    }

    // ifundef(x, default) — замена если undef
    if (is_keyword(*p, "ifundef", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }
        while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<Y:00000000>");
        *has_code = 1;
        return 1;
    }


    // exists(%h, key) — проверка существования ключа/индекса
    if (is_keyword(*p, "exists", 6)) {
        (*p) += 6; while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ',') { (*p)++; }


        while (**p == ' ') (*p)++;
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

        while (**p == ' ') (*p)++;
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<*:%08X>", 20);
        *has_code = 1;
        return 1;
    }


    // postfix{ ... } — постфиксное выражение
    if (is_keyword(*p, "postfix", 7)) {
        (*p) += 7; while (**p == ' ') (*p)++;
        if (**p != '{') { fprintf(stderr, "Expected '{'\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        // Собираем токены до '}'
        char tokens[256][64];
        int tcount = 0;

        while (**p && **p != '}') {
            while (**p == ' ' || **p == '\n') (*p)++;
            if (**p == '}') break;

            int i = 0;
            if (**p == '"' || **p == '\'') {
                char quote = **p;
                tokens[tcount][i++] = **p; (*p)++;
                while (**p && **p != quote) {
                    tokens[tcount][i++] = **p; (*p)++;
                }
                if (**p == quote) {
                    tokens[tcount][i++] = **p; (*p)++;
                }
            } else if ((**p >= '0' && **p <= '9') || **p == '.') {
                while (**p && **p != ' ' && **p != '\n' && **p != '}') {
                    tokens[tcount][i++] = **p; (*p)++;
                }
            } else if (**p == '$' || **p == '@' || **p == '%' || **p == '&') {
                while (**p && **p != ' ' && **p != '\n' && **p != '}') {
                    tokens[tcount][i++] = **p; (*p)++;
                }
            } else {
                // Оператор или имя
                while (**p && **p != ' ' && **p != '\n' && **p != '}') {
                    tokens[tcount][i++] = **p; (*p)++;
                }
            }
            tokens[tcount][i] = '\0';
            tcount++;
            while (**p == ' ' || **p == '\n') (*p)++;
        }

        if (**p != '}') { fprintf(stderr, "Expected '}'\n"); return 0; }
        (*p)++;

        // Компилируем токены в обратную польскую запись
        // Стек для операндов (уже на стеке VM)
        for (int i = 0; i < tcount; i++) {
            char* tok = tokens[i];

            if (strcmp(tok, "+") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<a:00000000>");
            } else if (strcmp(tok, "-") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<-:00000000>");
            } else if (strcmp(tok, "*") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<m:00000000>");
            } else if (strcmp(tok, "/") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "</:00000000>");
            } else if (strcmp(tok, "%") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<%:00000000>");
            } else if (strcmp(tok, "**") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<^:00000000>");
            } else if (strcmp(tok, "==") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<e:00000000>");
            } else if (strcmp(tok, "!=") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<n:00000000>");
            } else if (strcmp(tok, "<") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<l:00000000>");
            } else if (strcmp(tok, ">") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<g:00000000>");
            } else if (strcmp(tok, "<=") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<b:00000000>");
            } else if (strcmp(tok, ">=") == 0) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<]:00000000>");
            } else if (is_keyword(tok, "and", 3) && strlen(tok) == 3) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<{:00000000>");
            } else if (is_keyword(tok, "or", 2) && strlen(tok) == 2) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<}:00000000>");
            } else if (is_keyword(tok, "not", 3) && strlen(tok) == 3) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<~:00000000>");
            } else if (tok[0] == '$') {
                int idx = get_var_index(tok + 1, 0);
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
            } else if (tok[0] == '@') {
                int idx = get_var_index(tok + 1, 0);
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
            } else if (tok[0] == '%') {
                int idx = get_var_index(tok + 1, 0);
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
            } else if ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '.') {
                // Парсим число прямо здесь
                const char* saved = tok;
                int has_dot = 0;
                const char* tp = tok;
                while ((*tp >= '0' && *tp <= '9') || *tp == '.') {
                    if (*tp == '.') { if (has_dot) break; has_dot = 1; }
                    tp++;
                }
                int nlen = tp - saved;
                char* ns = malloc(nlen + 1);
                memcpy(ns, saved, nlen);
                ns[nlen] = '\0';

                int64_t hi;
                uint32_t lo;
                if (has_dot) {
                    char* dot = strchr(ns, '.');
                    *dot = '\0';
                    hi = strtoll(ns, NULL, 10);
                    char* frac = dot + 1;
                    int fl = strlen(frac);
                    if (fl > 9) frac[9] = '\0';
                    uint64_t fv = strtoull(frac, NULL, 10);
                    uint64_t denom = 1;
                    for (int j = 0; j < fl && j < 9; j++) denom *= 10;
                    lo = (uint32_t)((((__int128)fv << 32) / denom) & 0xFFFFFFFF);
                } else {
                    hi = strtoll(ns, NULL, 10);
                    lo = 0;
                }
                free(ns);

                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(hi >> 32));
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", (uint32_t)(hi & 0xFFFFFFFF));
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<i:%08X>", lo);
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<(:00000000>");
            } else if (tok[0] == '"' || tok[0] == '\'') {
                int len = strlen(tok) - 2;
                char* str = malloc(len + 1);
                memcpy(str, tok + 1, len);
                str[len] = '\0';
                int idx = add_string(str);
                *str_pool_pos += snprintf(str_pool_buf + *str_pool_pos, STR_POOL_BUF_SIZE - *str_pool_pos, "<l:%04X>%s", len, str);
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<s:%08X>", idx);
                free(str);
            }
        }

        *has_code = 1;
        return 1;
    }


    // Вызов функции или встроенная
    if (is_alpha(**p)) {
        const char* saved = *p;
        char fn[128]; int i = 0; char mod[64] = {0}; int has_dot = 0;
        while (**p && **p != '(' && **p != ' ' && **p != '\n') {
            if (**p == '.') { has_dot = 1; fn[i] = '\0'; strcpy(mod, fn); i = 0; (*p)++; continue; }
            fn[i++] = **p; (*p)++;
        }
        fn[i] = '\0';
        if (**p == '(') {
            (*p)++; while (**p == ' ') (*p)++;
            while (**p != ')') {
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
            }
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
            int fid = -1;
            if (has_dot) {
                fid = resolve_external_function(mod, fn);
                if (fid < 0) {
                    for (int j = 0; j < func_count; j++) {
                    }
                    fprintf(stderr, "Cannot resolve external function '%s.%s'\n", mod, fn);
                    return 0;
                }
            } else {
                for (int j = 0; j < func_count; j++) {
                    if (strcmp(func_table[j].name, fn) == 0) { fid = j; break; }
                }
                if (fid < 0) { fprintf(stderr, "Function '%s' not found\n", fn); return 0; }
            }
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<c:%08X>", fid);
            *has_code = 1; return 1;
        }
        *p = saved;
    }

    // Скобки
    if (**p == '(') {
        (*p)++; if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++; if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
        return 1;
    }

    fprintf(stderr, "Unknown expression at '%c'\n", **p);
    return 0;
}

// Остальные функции парсера выражений без изменений
static int parse_cmp(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_expr(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    char op = '\0';
    if (**p == '=' && *(*p+1)=='=') { op='e'; (*p)+=2; }
    else if (**p == '!' && *(*p+1)=='=') { op='n'; (*p)+=2; }
    else if (**p == '<' && *(*p+1)=='=') { op='b'; (*p)+=2; }
    else if (**p == '>' && *(*p+1)=='=') { op=']'; (*p)+=2; }
    else if (**p == '<') { op='l'; (*p)++; }
    else if (**p == '>') { op='g'; (*p)++; }
    else return 1;
    while (**p == ' ') (*p)++;
    if (!parse_expr(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<%c:00000000>", op);
    return 1;
}

static int parse_expr(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_term(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (**p == '+' || **p == '-' || (**p == '^' && !is_alpha(*(*p+1)))) {
        char op;
        if (**p == '^') {
            op = '^';  // конкатенация
        } else {
            op = (**p == '+') ? 'a' : '-';
        }
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_term(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<%c:00000000>", op);
    }
    return 1;
}

// parse_pow — возведение в степень (правоассоциативно, приоритет выше */%)
static int parse_pow(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_atom(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

    while (**p == ' ') (*p)++;
    if (**p == '^') {
        (*p)++; while (**p == ' ') (*p)++;
        // Правоассоциативно: рекурсивно вызываем parse_pow для правой части
        if (!parse_pow(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<^:00000000>");
    }

    return 1;
}

static int parse_term(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_pow(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;

    // Методные вызовы: .method(args)
    while (**p == '.') {
        (*p)++;
        char method[64];
        int i = 0;
        while (**p && **p != '(' && **p != ' ' && **p != '\n' && **p != '.') {
            method[i++] = **p; (*p)++;
        }
        method[i] = '\0';

        while (**p == ' ') (*p)++;
        if (**p != '(') { fprintf(stderr, "Expected '('\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;

        while (**p != ')') {
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
            while (**p == ' ') (*p)++;
            if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
        }
        if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;

        int found = 0;
        if (is_keyword(method, "abs", 3) && strlen(method) == 3) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<B:00000000>"); found = 1;
        } else if (is_keyword(method, "sign", 4) && strlen(method) == 4) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<C:00000000>"); found = 1;
        } else if (is_keyword(method, "int", 3) && strlen(method) == 3) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<I:00000000>"); found = 1;
        } else if (is_keyword(method, "frac", 4) && strlen(method) == 4) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<F:00000000>"); found = 1;
        } else if (is_keyword(method, "inv", 3) && strlen(method) == 3) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<o:00000000>"); found = 1;
        } else if (is_keyword(method, "sqrt", 4) && strlen(method) == 4) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<J:00000000>"); found = 1;
        } else if (is_keyword(method, "lc", 2) && strlen(method) == 2) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<z:00000000>"); found = 1;
        } else if (is_keyword(method, "uc", 2) && strlen(method) == 2) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<Z:00000000>"); found = 1;
        } else if (is_keyword(method, "chomp", 5) && strlen(method) == 5) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<K:00000000>"); found = 1;
        } else if (is_keyword(method, "chop", 4) && strlen(method) == 4) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<t:00000000>"); found = 1;
        } else if (is_keyword(method, "lcfirst", 7) && strlen(method) == 7) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<1:00000000>"); found = 1;
        } else if (is_keyword(method, "ucfirst", 7) && strlen(method) == 7) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<2:00000000>"); found = 1;
        } else if (is_keyword(method, "length", 6) && strlen(method) == 6) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<L:00000000>"); found = 1;
        } else if (is_keyword(method, "sort", 4) && strlen(method) == 4) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<O:00000000>"); found = 1;
        } else if (is_keyword(method, "reverse", 7) && strlen(method) == 7) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<R:00000000>"); found = 1;
        } else if (is_keyword(method, "clone", 5) && strlen(method) == 5) {
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<N:00000000>"); found = 1;
        }

        if (!found) {
            for (int j = 0; j < func_count; j++) {
                if (strcmp(func_table[j].name, method) == 0) {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<c:%08X>", j);
                    found = 1;
                    break;
                }
            }
        }

        if (!found) {
            fprintf(stderr, "Unknown method '%s'\n", method);
            return 0;
        }
        *has_code = 1;
    }

    while (**p == ' ') (*p)++;
    while (**p == '*' || **p == '/' || **p == '%' ||
        (**p == '<' && *(*p+1)=='<') || (**p == '>' && *(*p+1)=='>')) {
        char op;
    if (**p == '<' && *(*p+1)=='<') { op = 'B'; (*p) += 2; }
    else if (**p == '>' && *(*p+1)=='>') { op = 'R'; (*p) += 2; }
    else { op = (**p == '*') ? 'm' : ((**p == '/') ? '/' : '%'); (*p)++; }
    while (**p == ' ') (*p)++;
    if (!parse_pow(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<%c:00000000>", op);
        }

        return 1;
}

static int parse_bit_and(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_cmp(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (**p == '&' && *(*p+1) != '&') {
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_cmp(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<&:00000000>");
    }
    return 1;
}

static int parse_bit_xor(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_bit_and(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (**p == '^' && *(*p+1) == '^') {
        (*p) += 2; while (**p == ' ') (*p)++;
        if (!parse_bit_and(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<\\:00000000>");
    }
    return 1;
}

static int parse_bit_or(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_bit_xor(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (**p == '|' && *(*p+1) != '|') {
        (*p)++; while (**p == ' ') (*p)++;
        if (!parse_bit_xor(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<|:00000000>");
    }
    return 1;
}

static int parse_and(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {
    if (!parse_bit_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (is_keyword(*p, "and", 3)) {
        (*p) += 3; while (**p == ' ') (*p)++;
        if (!parse_bit_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<{:00000000>");
    }
    return 1;
}

static int parse_or(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code) {

    if (!parse_and(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
    while (**p == ' ') (*p)++;
    while (is_keyword(*p, "or", 2)) {
        (*p) += 2; while (**p == ' ') (*p)++;
        if (!parse_and(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<}:00000000>");
    }
    while (**p == ' ') (*p)++;
    if (**p == '?') {
        (*p)++; while (**p == ' ') (*p)++;
        int fjp = *code_pos;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<f:00000000>");
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        while (**p == ' ') (*p)++;
        if (**p != ':') { fprintf(stderr, "Expected ':' in ternary\n"); return 0; }
        (*p)++; while (**p == ' ') (*p)++;
        int ejp = *code_pos;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:00000000>");
        patch_jump(code_buf, fjp, *code_pos);
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        patch_jump(code_buf, ejp, *code_pos);
    }

    return 1;
}

// === Загрузка константы из .ocm ===
static Value load_module_constant(const char* module_name, const char* const_name) {
    char filename[256];
    FILE* f = NULL;
    snprintf(filename, sizeof(filename), "%s.ocm", module_name);
    f = fopen(filename, "rb");
    if (!f) {
        snprintf(filename, sizeof(filename), "./modules/%s.ocm", module_name);
        f = fopen(filename, "rb");
    }
    if (!f) return val_undef();

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    if (size < 6 || memcmp(data, "<OYCE;", 6) != 0) {
        free(data);
        return val_undef();
    }

    // Парсим заголовок
    const uint8_t* ptr = data + 6;
    const char* he = strchr((char*)ptr, '>');
    if (!he) { free(data); return val_undef(); }

    size_t hl = he - (char*)ptr;
    char* hb = malloc(hl + 1);
    memcpy(hb, ptr, hl);
    hb[hl] = '\0';

    uint32_t n_off = 0;
    char* tok = strtok(hb, ";");
    while (tok) {
        if (tok[0] == 'N') n_off = strtoul(tok + 2, NULL, 16);
        tok = strtok(NULL, ";");
    }
    free(hb);

    if (!n_off) { free(data); return val_undef(); }

    // Ищем константу в новом формате: <d:NAME:HI:LO> или <s:NAME:LEN>str
    const uint8_t* cp = data + n_off;
    while (cp < data + size) {
        if (cp[0] == '<' && (cp[1] == 'd' || cp[1] == 's') && cp[2] == ':') {
            char type = cp[1];
            cp += 3;

            // Читаем имя константы до ':'
            char name[128]; int i = 0;
            while (cp < data + size && *cp != ':') name[i++] = *cp++;
            name[i] = '\0';
            if (*cp == ':') cp++;

            if (strcmp(name, const_name) == 0) {
                Value r;
                if (type == 'd') {
                    // Читаем целую часть (16 hex)
                    char hitmp[17];
                    memcpy(hitmp, cp, 16); hitmp[16] = '\0';
                    int64_t hi = strtoll(hitmp, NULL, 16);
                    cp += 16;
                    if (*cp == ':') cp++;

                    // Читаем дробную часть (8 hex)
                    char lotmp[9];
                    memcpy(lotmp, cp, 8); lotmp[8] = '\0';
                    uint32_t lo = strtoul(lotmp, NULL, 16);

                    r = make_number(hi, lo);
                } else {
                    // Читаем длину строки (4 hex)
                    char lentmp[5];
                    memcpy(lentmp, cp, 4); lentmp[4] = '\0';
                    int len = strtoul(lentmp, NULL, 16);
                    cp += 4;
                    if (*cp == '>') cp++;

                    // Читаем строку
                    char* str = malloc(len + 1);
                    memcpy(str, cp, len);
                    str[len] = '\0';
                    int idx = add_string(str);
                    r = make_string(idx);
                    free(str);
                }
                free(data);
                return r;
            }

            // Пропускаем до '>' или конца
            while (cp < data + size && *cp != '>') cp++;
            if (*cp == '>') cp++;
            // Пропускаем \n если есть
            if (cp < data + size && *cp == '\n') cp++;
        } else {
            cp++;
        }
    }

    free(data);
    return val_undef();
}

// Парсинг метки цикла (LABEL:)
static int parse_loop_label(const char** p, char* label) {
    label[0] = '\0';
    const char* look = *p;
    while (*look == ' ' || *look == '\n' || *look == '\t' || *look == '\r') look++;
    if (is_alpha(*look)) {
        const char* ls = look;
        while (*ls && *ls != ':' && *ls != ' ' && *ls != '\n') ls++;
        if (*ls == ':') {
            *p = ls + 1;
            int len = ls - look;
            memcpy(label, look, len);
            label[len] = '\0';
            while (**p == ' ') (*p)++;
            return 1;
        }
    }
    return 0;
}
// ============================================================
// ПАРСИНГ ОДНОГО STATEMENT
// ============================================================
static int parse_statement(const char** p, char* code_buf, int* code_pos, char* str_pool_buf, int* str_pool_pos, int* has_code, int source_mode) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
    if (**p == '#') { while (**p && **p != '\n') (*p)++; return 1; }
    if (**p == '\0' || **p == '}') return 1;
    if (**p == ';') { (*p)++; return 1; }


    // last [LABEL]
    if (is_keyword(*p, "last", 4)) {
        *p += 4;
        char label[64] = {0};

        while (**p == ' ') (*p)++;
        if (is_alpha(**p)) {
            int i = 0;
            while (**p && **p != ' ' && **p != '\n' && **p != ';' && **p != '}') {
                label[i++] = **p; (*p)++;
            }
            label[i] = '\0';
        }

        if (loop_depth == 0) { fprintf(stderr, "last outside loop\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }

        int target = loop_depth - 1;
        if (label[0]) {
            for (int d = loop_depth - 1; d >= 0; d--) {
                if (strcmp(loop_stack[d].label, label) == 0) {
                    target = d;
                    break;
                }
            }
        }

        loop_stack[target].last_jumps[loop_stack[target].last_count++] = *code_pos;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:00000000>");
        return 1;
    }

    // next [LABEL]
    if (is_keyword(*p, "next", 4)) {
        *p += 4;
        char label[64] = {0};

        while (**p == ' ') (*p)++;
        if (is_alpha(**p)) {
            int i = 0;
            while (**p && **p != ' ' && **p != '\n' && **p != ';' && **p != '}') {
                label[i++] = **p; (*p)++;
            }
            label[i] = '\0';
        }

        if (loop_depth == 0) { fprintf(stderr, "next outside loop\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }

        int target = loop_depth - 1;
        if (label[0]) {
            for (int d = loop_depth - 1; d >= 0; d--) {
                if (strcmp(loop_stack[d].label, label) == 0) {
                    target = d;
                    break;
                }
            }
        }

        loop_stack[target].next_jumps[loop_stack[target].next_count++] = *code_pos;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:00000000>");
        return 1;
    }

    // redo [LABEL]
    if (is_keyword(*p, "redo", 4)) {
        *p += 4;
        char label[64] = {0};

        while (**p == ' ') (*p)++;
        if (is_alpha(**p)) {
            int i = 0;
            while (**p && **p != ' ' && **p != '\n' && **p != ';' && **p != '}') {
                label[i++] = **p; (*p)++;
            }
            label[i] = '\0';
        }

        if (loop_depth == 0) { fprintf(stderr, "redo outside loop\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }

        int target = loop_depth - 1;
        if (label[0]) {
            for (int d = loop_depth - 1; d >= 0; d--) {
                if (strcmp(loop_stack[d].label, label) == 0) {
                    target = d;
                    break;
                }
            }
        }

        int tgt = loop_stack[target].body_start ? loop_stack[target].body_start : loop_stack[target].loop_start;
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:%08X>", tgt);
        return 1;
    }

    // return
    if (is_keyword(*p, "return", 6)) {
        *p += 6; while (**p == ' ') (*p)++;
        if (**p != '}' && **p != '\n') {
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
        }
        *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<y:00000000>");
        return 1;
    }

    // inc($x) или inc $x
    if (is_keyword(*p, "inc", 3)) {
        const char* saved_line = *p;
        (*p) += 3; while (**p == ' ') (*p)++;

        // Поддержка скобочного синтаксиса inc($x)
        if (**p == '(') {
            (*p)++; while (**p == ' ') (*p)++;
            if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++; char vn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
            vn[i] = '\0';
            while (**p == ' ') (*p)++;
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++;

            int idx = get_var_index(vn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);

            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 1, 0);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<a:00000000>");
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<0:%08X>", idx);
            }

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // Старый синтаксис inc $x
        if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); return 0; }
        (*p)++; char vn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
        vn[i] = '\0';

        int idx = get_var_index(vn, 0);
        int is_local = (g_in_function && var_table[idx].is_local);

        if (is_local) {

            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 1, 0);
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<a:00000000>");
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
        } else {

            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<0:%08X>", idx);
        }

        if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
        return 1;
    }

    // dec($x) или dec $x
    if (is_keyword(*p, "dec", 3)) {
        const char* saved_line = *p;
        (*p) += 3; while (**p == ' ') (*p)++;

        // Поддержка скобочного синтаксиса dec($x)
        if (**p == '(') {
            (*p)++; while (**p == ' ') (*p)++;
            if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++; char vn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
            vn[i] = '\0';
            while (**p == ' ') (*p)++;
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++;

            int idx = get_var_index(vn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);

            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 1, 0);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<-:00000000>");
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<@:%08X>", idx);
            }

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // Старый синтаксис dec $x
        if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
        (*p)++; char vn[64]; int i = 0;
        while (**p && **p != ' ' && **p != '\n' && **p != ')' && **p != ',') vn[i++] = **p, (*p)++;
        vn[i] = '\0';

        int idx = get_var_index(vn, 0);
        int is_local = (g_in_function && var_table[idx].is_local);

        if (is_local) {
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 1, 0);
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<-:00000000>");
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
        } else {
            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<@:%08X>", idx);
        }

        if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
        return 1;
    }

    // Файловые функции как statement'ы
    if ((is_keyword(*p, "fopen", 5)) ||
        (is_keyword(*p, "fclose", 6)) ||
        (is_keyword(*p, "freadline", 9)) ||
        (is_keyword(*p, "fread", 5)) ||
        (is_keyword(*p, "fprint", 6)) ||
        (is_keyword(*p, "sysread", 7)) ||
        (is_keyword(*p, "syswrite", 8)) ||
        (is_keyword(*p, "frename", 7)) ||
        (is_keyword(*p, "funlink", 7)) ||
        (is_keyword(*p, "fseek", 5)) ||
        (is_keyword(*p, "ftell", 5)) ||
        (is_keyword(*p, "feof", 4)) ||
        (is_keyword(*p, "socket", 6)) ||
        (is_keyword(*p, "bind", 4)) ||
        (is_keyword(*p, "connect", 7)) ||
        (is_keyword(*p, "listen", 6)) ||
        (is_keyword(*p, "accept", 6)) ||
        (is_keyword(*p, "send", 4)) ||
        (is_keyword(*p, "recv", 4)) ||
        (is_keyword(*p, "sockclose", 9)) ||
        (is_keyword(*p, "popen", 5)) ||
        (is_keyword(*p, "pclose", 6)) ||
        (is_keyword(*p, "pipe_write", 10)) ||
        (is_keyword(*p, "pipe_read", 9)) ||
        (is_keyword(*p, "pipe_close", 10)) ||
        (is_keyword(*p, "popen2", 6))
    )


    {

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
        return 1;
    }

    // setkey как statement
    if (is_keyword(*p, "setkey", 6)) {
        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
        return 1;
    }

    // hadd, hdel, getkey как отдельные statement'ы
    if ((is_keyword(*p, "hadd", 4)) ||
        (is_keyword(*p, "hdel", 4)) ||
        (is_keyword(*p, "getkey", 6))) {

        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
        return 1;
    }

        // print
        if (is_keyword(*p, "print", 5)) {

            const char* saved_line = *p; *p += 5; while (**p == ' ') (*p)++;
            if (**p != '(') { fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;

            // Аргументы могут разделяться пробелами
            int arg_count = 0;
            while (**p != ')') {
                if (arg_count > 0) {
                    while (**p == ' ') (*p)++;
                    if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                }
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                arg_count++;
                while (**p == ' ') (*p)++;
            }
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<P:00000000>");
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // if / elseif / else
        if (is_keyword(*p, "if", 2)) {

            const char* saved_line = *p;
            *p += 2;
            while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
            if (**p != '(') { fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
            while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
            ctx_push(CTX_IF); CtxFrame* f = ctx_top();
            f->entry = *code_pos;

            // Условие if
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
            while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
            while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;

            int jp = *code_pos;
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<f:00000000>");

            // Тело if
            if (**p != '{') { fprintf(stderr, "Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
            while (**p && **p != '}') {
                if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
            }
            if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Прыжок в конец всей конструкции из тела if
            f->jump_outs[f->jump_out_count++] = *code_pos;
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:00000000>");

            // Сюда прыгаем если условие ложно (начало elseif/else)
            f->exit_false = *code_pos;
            patch_jump(code_buf, jp, f->exit_false);

            // Цепочка elseif
            while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
            while (is_keyword(*p, "elseif", 6)) {
                *p += 6;
                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
                if (**p != '(') { fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;

                // Условие elseif
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
                if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;

                int jp2 = *code_pos;
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<f:00000000>");

                // Тело elseif
                if (**p != '{') { fprintf(stderr, "Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                while (**p && **p != '}') {
                    if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                }
                if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

                // Прыжок в конец всей конструкции из тела elseif
                f->jump_outs[f->jump_out_count++] = *code_pos;
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:00000000>");

                // Обновляем точку выхода для следующего elseif/else
                f->exit_false = *code_pos;
                patch_jump(code_buf, jp2, f->exit_false);

                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
            }

            // Финальный else (опционально)
            if (is_keyword(*p, "else", 4)) {
                *p += 4;
                while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++;
                if (**p != '{') { fprintf(stderr, "Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                f->has_else = 1;

                while (**p && **p != '}') {
                    if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                }
                if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
            }

            // Конец всей конструкции
            f->exit_true = *code_pos;

            // Патчим все прыжки в конец
            for (int i = 0; i < f->jump_out_count; i++) {
                patch_jump(code_buf, f->jump_outs[i], f->exit_true);
            }

            ctx_pop();
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // while
        char loop_label[64];
        parse_loop_label(p, loop_label);
        if (is_keyword(*p, "while", 5)) {
            const char* saved_line = *p; *p += 5; while (**p == ' ') (*p)++;
            if (**p != '(') { fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;
            int loop_start = *code_pos;
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)){fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
            while (**p == ' ') (*p)++; if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;
            int jp = *code_pos;
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<f:00000000>");
            if (**p != '{') { fprintf(stderr, "Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Сохраняем информацию о цикле
            loop_stack[loop_depth].loop_start = loop_start;
            loop_stack[loop_depth].loop_continue = loop_start;
            loop_stack[loop_depth].loop_end = 0;
            loop_stack[loop_depth].last_count = 0;
            loop_stack[loop_depth].next_count = 0;
            loop_stack[loop_depth].redo_count = 0;
            strcpy(loop_stack[loop_depth].label, loop_label);
            // Патчим redo-прыжки (loop_start уже известен)
            for (int i = 0; i < loop_stack[loop_depth].redo_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth].redo_jumps[i], loop_start);
            }
            loop_stack[loop_depth].redo_count = 0;

            loop_depth++;

            // Тело цикла
            while (**p && **p != '}') {
                if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
            }
            if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Генерируем прыжок на начало цикла
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:%08X>", loop_start);
            int loop_end_pos = *code_pos;
            patch_jump(code_buf, jp, loop_end_pos);

            // Патчим все last-прыжки (после тела)
            for (int i = 0; i < loop_stack[loop_depth - 1].last_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth - 1].last_jumps[i], loop_end_pos);
            }

            // Патчим все next-прыжки (после тела, для while — на loop_start)
            for (int i = 0; i < loop_stack[loop_depth - 1].next_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth - 1].next_jumps[i], loop_start);
            }
            loop_stack[loop_depth - 1].next_count = 0;

            loop_depth--;
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // for
        parse_loop_label(p, loop_label);
        if (is_keyword(*p, "for", 3)) {
            const char* saved_line = *p;
            *p += 3; while (**p == ' ') (*p)++;
            int is_in_style = 0;
            const char* la = *p; while (*la == ' ') la++;
            if (*la == '$') {
                const char* t = la+1;
                while (*t && *t != ' ' && *t != '\n') t++;
                while (*t == ' ') t++;
                if (is_keyword(t, "in", 2)) is_in_style = 1;
            }

            if (is_in_style) {
                while (**p == ' ') (*p)++; if (**p != '$') { fprintf(stderr, "Expected '$var'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                char vn[64]; int i=0; while (**p && **p!=' ' && **p!='\n') vn[i++]=**p, (*p)++; vn[i]='\0'; int ii=get_var_index(vn,0);
                while (**p==' ') (*p)++; if (strncmp(*p,"in",2)!=0||is_alpha((*p)[2])) { fprintf(stderr,"Expected 'in'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } *p+=2; while (**p==' ') (*p)++;
                if (**p!='@') { fprintf(stderr,"Expected '@array'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; char an[64]; i=0;
                while (**p&&**p!=' '&&**p!='{'&&**p!='\n') an[i++]=**p, (*p)++; an[i]='\0'; int ai=get_var_index(an,0);
                while (**p==' ') (*p)++; if (**p!='{') { fprintf(stderr,"Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p==' '||**p=='\n') (*p)++;
                char idn[72], lnn[72]; snprintf(idn,sizeof(idn),"$__idx_%s",vn); snprintf(lnn,sizeof(lnn),"$__len_%s",vn);
                int idv=get_var_index(idn,1), lnv=get_var_index(lnn,1);

                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><q:%08X>", 0, idv);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X><L:00000000><q:%08X>", ai, lnv);
                int ls=*code_pos;
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X><d:%08X><l:00000000>", idv, lnv);

                int jp = *code_pos;
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<f:00000000>");
                int body_start = *code_pos;
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X><d:%08X><A:00000000><q:%08X>", ai, idv, ii);

                // Сохраняем информацию о цикле
                loop_stack[loop_depth].loop_start = ls;
                loop_stack[loop_depth].loop_end = 0;
                loop_stack[loop_depth].last_count = 0;
                loop_stack[loop_depth].next_count = 0;
                loop_stack[loop_depth].redo_count = 0;
                loop_stack[loop_depth].body_start = body_start;
                strcpy(loop_stack[loop_depth].label, loop_label);
                for (int i = 0; i < loop_stack[loop_depth].redo_count; i++) {
                    patch_jump(code_buf, loop_stack[loop_depth].redo_jumps[i], ls);
                }
                loop_stack[loop_depth].redo_count = 0;

                loop_depth++;

                while (**p && **p != '}') {
                    if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                }
                if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

                // Инкремент
                int incr_pos = *code_pos;
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idv);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 1, 0);
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<a:00000000>");
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idv);

                // Заполняем loop_continue для next
                loop_stack[loop_depth - 1].loop_continue = incr_pos;
                for (int i = 0; i < loop_stack[loop_depth - 1].next_count; i++) {
                    patch_jump(code_buf, loop_stack[loop_depth - 1].next_jumps[i], incr_pos);
                }
                loop_stack[loop_depth - 1].next_count = 0;

                // Прыжок на начало проверки условия
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<j:%08X>", ls);
                int loop_end_pos = *code_pos;
                patch_jump(code_buf, jp, loop_end_pos);

                // Патчим все last-прыжки
                for (int i = 0; i < loop_stack[loop_depth - 1].last_count; i++) {
                    patch_jump(code_buf, loop_stack[loop_depth - 1].last_jumps[i], loop_end_pos);
                }

                loop_depth--;
                if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
                return 1;
            }

            // C-style for
            if (**p != '(') {
                fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                (*p)++;
                while (**p == ' ') (*p)++;
                if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                while (**p == ' ') (*p)++; if (**p == ';') (*p)++; while (**p == ' ') (*p)++;
                int loop_start = *code_pos;
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
            int jp = *code_pos;
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<f:00000000>");
            int body_start = *code_pos;
            while (**p == ' ') (*p)++; if (**p == ';') (*p)++; while (**p == ' ') (*p)++;
            const char* inc_start = *p; int inc_depth = 0;
            while (**p && (**p != ')' || inc_depth > 0)) { if (**p == '(') inc_depth++; if (**p == ')') inc_depth--; (*p)++; }
            int inc_len = *p - inc_start; char* inc_str = malloc(inc_len + 1);
            memcpy(inc_str, inc_start, inc_len); inc_str[inc_len] = '\0';
            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); free(inc_str); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;
            if (**p != '{') { fprintf(stderr, "Expected '{'\n"); free(inc_str); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Сохраняем информацию о цикле
            loop_stack[loop_depth].loop_start = loop_start;
            loop_stack[loop_depth].loop_continue = 0;
            loop_stack[loop_depth].loop_end = 0;
            loop_stack[loop_depth].last_count = 0;
            loop_stack[loop_depth].next_count = 0;
            loop_stack[loop_depth].redo_count = 0;
            loop_stack[loop_depth].body_start = body_start;

            for (int i = 0; i < loop_stack[loop_depth].redo_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth].redo_jumps[i], loop_start);
            }
            loop_stack[loop_depth].redo_count = 0;

            loop_depth++;

            while (**p && **p != '}') {
                if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {
                    free(inc_str); return 0;
                }
            }
            if (**p != '}') { fprintf(stderr, "Expected '}'\n"); free(inc_str); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Инкремент (компилируется после тела)
            int incr_start = *code_pos;
            const char* inc_p = inc_str; while (*inc_p == ' ') inc_p++;
            if (!parse_statement(&inc_p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {
                free(inc_str); return 0;
            }
            free(inc_str);

            // Заполняем loop_continue для next
            loop_stack[loop_depth - 1].loop_continue = incr_start;
            for (int i = 0; i < loop_stack[loop_depth - 1].next_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth - 1].next_jumps[i], incr_start);
            }
            loop_stack[loop_depth - 1].next_count = 0;

            // Прыжок на начало проверки условия
            *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<j:%08X>", loop_start);
            int loop_end_pos = *code_pos;
            patch_jump(code_buf, jp, loop_end_pos);

            // Патчим все last-прыжки
            for (int i = 0; i < loop_stack[loop_depth - 1].last_count; i++) {
                patch_jump(code_buf, loop_stack[loop_depth - 1].last_jumps[i], loop_end_pos);
            }

            loop_depth--;
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }


        // use
        if (is_keyword(*p, "use", 3)) {
            const char* saved_line = *p;
            *p += 3; while (**p == ' ') (*p)++;
            if (**p != '"' && **p != '\'') { fprintf(stderr, "Expected module name in quotes\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            char quote = **p; (*p)++; char mn[64]; int i=0;
            while (**p && **p != quote) mn[i++] = **p, (*p)++; mn[i]='\0'; if (**p == quote) (*p)++;
            while (**p == ' ') (*p)++; if (!is_keyword(*p, "as", 2)) { fprintf(stderr, "Expected 'as'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            *p += 2; while (**p == ' ') (*p)++;
            char al[64]; i=0; while (**p && **p != ' ' && **p != '\n' && **p != ';') al[i++] = **p, (*p)++; al[i]='\0';
            // Проверяем, нет ли уже такого алиаса
            int alias_exists = 0;
            for (int j = 0; j < alias_count; j++) {
                if (strcmp(alias_map[j].alias, al) == 0) {
                    alias_exists = 1;
                    break;
                }
            }

            if (!alias_exists) {
                strcpy(alias_map[alias_count].alias, al);
                strcpy(alias_map[alias_count].module_name, mn);
                alias_count++;
            };
            strcpy(alias_map[alias_count].module_name, mn);
            alias_count++;
            // Проверяем, не загружен ли модуль
            int already_loaded = 0;
            for (int j = 0; j < loaded_module_count; j++) {
                if (strcmp(loaded_module_names[j], mn) == 0) {
                    already_loaded = 1;
                    break;
                }
            }

            if (!already_loaded) {
                compile_module(mn);
            }
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // export
        if (is_keyword(*p, "export", 6)) {
            const char* saved_line = *p;
            *p += 6; while (**p == ' ') (*p)++;
            while (**p && **p != '\n') {
                char fn[64]; int i=0;
                while (**p && **p != ',' && **p != ' ' && **p != '\n') fn[i++] = **p, (*p)++; fn[i]='\0';
                for (int j=0; j<func_count; j++) if (strcmp(func_table[j].name, fn) == 0) func_table[j].export_flag = 1;
                while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
            }
            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // fun
        if (is_keyword(*p, "fun", 3)) {

            const char* saved_line = *p;
            *p += 3; while (**p == ' ') (*p)++;
            char fn[128]; int i=0; while (**p && **p != '(' && **p != ' ') fn[i++] = **p, (*p)++; fn[i]='\0';

            int name_idx = add_string(fn);
            while (**p == ' ') (*p)++; if (**p != '(') { fprintf(stderr, "Expected '('\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;

            int pc=0, lc=0;

            saved_global_var_table = var_table;
            saved_global_var_count = var_count;
            saved_global_var_capacity = var_capacity;

            var_table = NULL;
            var_count = 0;
            var_capacity = 0;
            g_in_function = 1;

            // Временный массив для имён параметров
            char* param_names[16];
            int param_count = 0;

            while (**p != ')') {

                if (**p == '$' || **p == '@' || **p == '%') {

                    (*p)++;
                    char pn[64]; int j = 0;
                    while (**p && **p != ',' && **p != ')' && **p != ' ') pn[j++] = **p, (*p)++;
                    pn[j] = '\0';

                    param_names[param_count] = my_strdup(pn);
                    get_var_index(pn, 1);  // регистрируем как локальную
                    pc++;
                    param_count++;
                }
                else (*p)++;
                while (**p == ' ') (*p)++;
                if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
            }

            // Сохраняем параметры для использования в теле функции
            current_func_param_count = param_count;
            for (int i = 0; i < param_count; i++) {
                current_func_params[i] = param_names[i];
            }

            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++; while (**p == ' ') (*p)++;
            if (**p != '{') { fprintf(stderr, "Expected '{'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

            // Добавляем функцию в func_table ДО компиляции тела (для рекурсии)
            FuncDef* fd = &func_table[func_count];
            strcpy(fd->name, fn);
            fd->module_name[0] = '\0';
            fd->name_idx = name_idx;
            fd->param_count = pc;
            fd->local_count = lc;  // lc уже считается правильно
            fd->code_offset = -1;
            fd->export_flag = 0;
            fd->func_code = NULL;
            fd->func_code_len = 0;
            func_count++;

            // Входим в функцию — все новые переменные будут локальными
            g_in_function = 1;

            // Компилируем тело функции во временный буфер
            char func_code_buf[CODE_BUF_SIZE];
            int has_return = 0;
            lc = pc;
            char* save_code_buf = code_buf;
            int save_code_pos = *code_pos;
            int save_str_pool_pos = *str_pool_pos;

            code_buf = func_code_buf;
            *code_pos = 0;
            int line = 0;
            while (**p && **p != '}') {
                while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
                if (**p == '#') { while (**p && **p != '\n') (*p)++; continue; }
                if (**p == '}') break;
                if (is_keyword(*p, "return", 6)) {
                    *p += 6; while (**p == ' ') (*p)++;
                    if (**p != '}' && **p != '\n') {
                        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                    }
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<y:00000000>"); has_return = 1;
                    continue;
                }
                if (!parse_statement(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code, source_mode)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
            }
            if (**p != '}') { fprintf(stderr, "Expected '}'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
            if (!has_return) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<y:00000000>");
            }

            // Восстанавливаем основной буфер
            int func_len = *code_pos;

            // Выходим из функции
            g_in_function = 0;

            code_buf = save_code_buf;
            *code_pos = save_code_pos;


            // Обновляем код функции
            fd->func_code = malloc(func_len);
            memcpy(fd->func_code, func_code_buf, func_len);
            fd->func_code_len = func_len;
            fd->local_count = var_count;  // var_count сейчас содержит все локальные переменные (параметры + my)

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);

            // Восстановление:
            g_in_function = 0;
            for (int i = 0; i < var_count; i++) free(var_table[i].name);
            free(var_table);
            var_table = saved_global_var_table;
            var_count = saved_global_var_count;
            var_capacity = saved_global_var_capacity;
            saved_global_var_table = NULL;

            // Очищаем параметры
            for (int i = 0; i < current_func_param_count; i++) {
                free(current_func_params[i]);
            }
            current_func_param_count = 0;

            return 1;
        }

        // &NAME = value (константа)
        if (**p == '&') {
            const char* saved_line = *p; (*p)++;
            char name[64]; int i = 0;
            while (**p && **p != ' ' && **p != '=' && **p != '\n') { name[i++] = **p; (*p)++; }
            name[i] = '\0';
            while (**p == ' ') (*p)++;
            if (**p != '=') {
                *p = saved_line;
            } else {
                (*p)++; while (**p == ' ') (*p)++;
                Value val;
                if (**p == '"' || **p == '\'') {
                    char quote = **p; (*p)++; const char* start = *p;
                    while (**p && **p != quote) (*p)++; int len = *p - start;
                    char* str = malloc(len+1); memcpy(str, start, len); str[len]='\0';
                    if (**p == quote) (*p)++;
                    int idx = add_string(str); val = make_string(idx); free(str);
                } else if ((**p >= '0' && **p <= '9') || **p == '.') {
                    const char* saved = *p; int has_dot = 0;
                    while ((**p >= '0' && **p <= '9') || **p == '.') {
                        if (**p == '.') { if (has_dot) break; has_dot = 1; } (*p)++;
                    }
                    int len = *p - saved; char* ns = malloc(len+1); memcpy(ns, saved, len); ns[len]='\0';
                    if (has_dot) {
                        char* dot = strchr(ns, '.'); *dot = '\0'; int64_t hi = strtoll(ns, NULL, 10);
                        char* frac = dot + 1; int fl = strlen(frac); if (fl > 9) frac[9] = '\0';
                        uint64_t fv = strtoull(frac, NULL, 10); uint64_t denom = 1;
                        for (int j = 0; j < fl && j < 9; j++) denom *= 10;
                        uint32_t lo = (uint32_t)((((__int128)fv << 32) / denom) & 0xFFFFFFFF);
                        val = make_number(hi, lo);
                    } else { val = make_number(strtoll(ns, NULL, 10), 0); }
                    free(ns);
                } else { fprintf(stderr, "Expected literal value\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                const_table[const_count].name = my_strdup(name);
                const_table[const_count].value = val; const_count++;
                add_const_to_pool(name, val);
                if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
                return 1;
            }
        }
        // @arr = (elem1, elem2, ...) или @arr[$i] = value
        if (**p == '@') {
            const char* saved_line = *p;
            (*p)++;
            char vn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '=' && **p != '\n' && **p != '[') {
                vn[i++] = **p; (*p)++;
            }
            vn[i] = '\0';

            // Проверяем, это присваивание элементу @arr[$i] = value
            if (**p == '[') {
                int idx = get_var_index(vn, 0);
                (*p)++; while (**p == ' ') (*p)++;

                // Загружаем массив
                int is_local = (g_in_function && var_table[idx].is_local);
                if (is_local) {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
                } else {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
                }

                // Парсим индекс
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                while (**p == ' ') (*p)++;
                if (**p != ']') { fprintf(stderr, "Expected ']' (C)\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

                // Проверяем =
                while (**p == ' ') (*p)++;
                if (**p != '=') { fprintf(stderr, "Expected '='\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                (*p)++; while (**p == ' ') (*p)++;

                // Парсим значение
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<M:00000000>");
                if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
                return 1;
            }

            // Иначе @arr = (elem1, elem2, ...) или @arr = expression
            while (**p == ' ') (*p)++;
            if (**p != '=') { fprintf(stderr, "Expected '='\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++; while (**p == ' ') (*p)++;

            if (**p == '(') {
                (*p)++; while (**p == ' ') (*p)++;
                int count = 0;
                while (**p && **p != ')') {
                    if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                    count++;
                    while (**p == ' ') (*p)++;
                    if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                }

                if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<S:%08X>", count);

                int idx = get_var_index(vn, 0);
                int is_local = (g_in_function && var_table[idx].is_local);
                if (is_local) {
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<|:%08X>", idx);
                } else {
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<):%08X>", idx);
                }

                if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
                return 1;
            }

            // @arr = expression (не список)
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

            int idx = get_var_index(vn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);
            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<|:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<):%08X>", idx);
            }

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // %hash = array(N) или %hash = (...), или %hash[$i] = value
        if (**p == '%') {
            const char* saved_line = *p; (*p)++;
            char hn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '=' && **p != '\n' && **p != '[' && **p != ')') {
                hn[i++] = **p; (*p)++;
            }
            hn[i] = '\0';
            int idx = get_var_index(hn, 0);

            // Если после имени идёт ')', это не присваивание — откатываем
            if (**p == ')') {
                return 0;
            }
            // Проверяем, это %h[$i] = value ?
            if (**p == '[') {
                (*p)++; while (**p == ' ') (*p)++;

                // Загружаем хеш
                int is_local = (g_in_function && var_table[idx].is_local);
                if (is_local) {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<d:%08X>", idx);
                } else {
                    *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<v:%08X>", idx);
                }

                // Парсим индекс
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                while (**p == ' ') (*p)++;
                if (**p != ']') { fprintf(stderr, "Expected ']'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;

                // Должно быть =
                while (**p == ' ') (*p)++;
                if (**p != '=') { fprintf(stderr, "Expected '='\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                (*p)++; while (**p == ' ') (*p)++;

                // Парсим значение
                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<9:00000000>");
                return 1;
            }

            // Иначе %hash = expression
            while (**p == ' ') (*p)++;
            if (**p != '=') { fprintf(stderr, "Expected '='\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++; while (**p == ' ') (*p)++;

            int is_local = (g_in_function && var_table[idx].is_local);

            // Проверяем, список в скобках или выражение
            if (**p == '(') {
                (*p)++; while (**p == ' ') (*p)++;

                // Смотрим вперёд — есть ли => (полный хеш)
                const char* look = *p;
                int has_keys = 0;
                int depth = 0;
                while (*look && (*look != ')' || depth > 0)) {
                    if (*look == '(') depth++;
                    if (*look == ')') depth--;
                    if (depth == 0 && *look == '=' && *(look + 1) == '>') { has_keys = 1; break; }
                    look++;
                }

                if (has_keys) {
                    // Полный хеш: key => value, ...
                    // Создаём пустой хеш на стеке
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 0, 0);
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<H:00000000>");

                    // Сохраняем хеш в переменную
                    if (is_local) {
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<&:%08X>", idx);
                    } else {
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<#:%08X>", idx);
                    }

                    while (**p != ')') {
                        while (**p == ' ') (*p)++;
                        if (**p == ')') break;

                        // Загружаем хеш
                        if (is_local) {
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
                        } else {
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<v:%08X>", idx);
                        }

                        // Ключ
                        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                        while (**p == ' ') (*p)++;
                        if (**p != '=' || *(*p+1) != '>') { fprintf(stderr, "Expected '=>'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                        (*p) += 2; while (**p == ' ') (*p)++;

                        // Значение
                        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

                        // Вставка в хеш
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<h:00000000>");

                        while (**p == ' ') (*p)++;
                        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                    }
                } else {
                    // Лёгкий хеш: просто значения
                    int count = 0;
                    while (**p != ')') {
                        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                        count++;
                        while (**p == ' ') (*p)++;
                        if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                    }
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<S:%08X>", count);
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<H:00000000>");

                    if (is_local) {
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<&:%08X>", idx);
                    } else {
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<#:%08X>", idx);
                    }
                }

                if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                return 1;
            }

            // %hash = expression (array(N) или другое)
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

            if (is_local) {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<&:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<#:%08X>", idx);
            }

            return 1;
        }

        // my $var / my @arr / my %hash
        if (is_keyword(*p, "my", 2)) {
            const char* saved_line = *p;
            *p += 2; while (**p == ' ') (*p)++;

            char type = 0;  // '$', '@', '%'
            if (**p == '$') { type = '$'; (*p)++; }
            else if (**p == '@') { type = '@'; (*p)++; }
            else if (**p == '%') { type = '%'; (*p)++; }
            else { fprintf(stderr, "Expected '$', '@' or '%%' after 'my'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }

            char vn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '=' && **p != '\n' && **p != ';') {
                vn[i++] = **p; (*p)++;
            }
            vn[i] = '\0';

            if (type == '$') {
                int idx = get_var_index(vn, 1);
                while (**p == ' ') (*p)++;
                if (**p == '=' && *(*p+1) != '=') {
                    (*p)++; while (**p == ' ') (*p)++;
                    if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
                }
            } else if (type == '@') {
                int idx = get_var_index(vn, 1);
                while (**p == ' ') (*p)++;
                if (**p == '=' && *(*p+1) != '=') {
                    (*p)++; while (**p == ' ') (*p)++;
                    if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                    *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<|:%08X>", idx);
                }
            } else if (type == '%') {
                int idx = get_var_index(vn, 1);
                while (**p == ' ') (*p)++;
                if (**p == '=' && *(*p+1) != '=') {
                    (*p)++; while (**p == ' ') (*p)++;

                    if (**p == '(') {
                        (*p)++; while (**p == ' ') (*p)++;

                        const char* look = *p;
                        int has_keys = 0;
                        int depth = 0;
                        while (*look && (*look != ')' || depth > 0)) {
                            if (*look == '(') depth++;
                            if (*look == ')') depth--;
                            if (depth == 0 && *look == '=' && *(look + 1) == '>') { has_keys = 1; break; }
                            look++;
                        }

                        if (has_keys) {
                            // Полный хеш
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<i:%08X><i:%08X><i:%08X><(:00000000>", 0, 0, 0);
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<H:00000000>");
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<&:%08X>", idx);

                            while (**p != ')') {
                                while (**p == ' ') (*p)++;
                                if (**p == ')') break;
                                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<d:%08X>", idx);
                                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                                while (**p == ' ') (*p)++;
                                if (**p != '=' || *(*p+1) != '>') { fprintf(stderr, "Expected '=>'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                                (*p) += 2; while (**p == ' ') (*p)++;
                                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) return 0;
                                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<h:00000000>");
                                while (**p == ' ') (*p)++;
                                if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                            }
                            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); return 0; } (*p)++;
                        } else {
                            // Лёгкий хеш
                            int count = 0;
                            while (**p != ')') {
                                if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                                count++;
                                while (**p == ' ') (*p)++;
                                if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                            }
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<S:%08X>", count);
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<H:00000000>");
                            *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<&:%08X>", idx);
                            if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                        }
                    } else {
                        // Простое выражение
                        if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                        *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<&:%08X>", idx);
                    }
                }
            }

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // $x = ...
        if (**p == '$') {
            const char* saved_line = *p; (*p)++;
            char vn[64]; int i = 0;
            while (**p && **p != ' ' && **p != '=' && **p != '\n') { vn[i++] = **p; (*p)++; }
            vn[i] = '\0';
            while (**p == ' ') (*p)++; if (**p != '=') { fprintf(stderr, "Expected '='\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
            (*p)++; while (**p == ' ') (*p)++;
            if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) {fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}

            int idx = get_var_index(vn, 0);
            int is_local = (g_in_function && var_table[idx].is_local);

            if (is_local) {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<q:%08X>", idx);
            } else {
                *code_pos += snprintf(code_buf+*code_pos, CODE_BUF_SIZE-*code_pos, "<p:%08X>", idx);
            }

            if (source_mode) emit_source_comment(code_buf, code_pos, saved_line, source_mode);
            return 1;
        }

        // Вызов функции как statement (в самом конце)
        if (is_alpha(**p)) {
            const char* saved = *p;
            char fn[128]; int i = 0; char mod[64] = {0}; int has_dot = 0;
            while (**p && **p != '(' && **p != ' ' && **p != '\n') {
                if (**p == '.') { has_dot = 1; fn[i] = '\0'; strcpy(mod, fn); i = 0; (*p)++; continue; }
                fn[i++] = **p; (*p)++;
            }
            fn[i] = '\0';
            if (**p == '(') {
                (*p)++; while (**p == ' ') (*p)++;
                while (**p != ')') {
                    if (!parse_or(p, code_buf, code_pos, str_pool_buf, str_pool_pos, has_code)) { fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0;}
                    while (**p == ' ') (*p)++; if (**p == ',') { (*p)++; while (**p == ' ') (*p)++; }
                }
                if (**p != ')') { fprintf(stderr, "Expected ')'\n"); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; } (*p)++;
                int fid = -1;
                if (has_dot) {
                    fid = resolve_external_function(mod, fn);
                    if (fid < 0) {
                        fprintf(stderr, "Cannot resolve external function '%s.%s'\n", mod, fn);
                        return 0;
                    }
                } else {
                    for (int j = 0; j < func_count; j++) {
                        if (strcmp(func_table[j].name, fn) == 0) { fid = j; break; }
                    }
                    if (fid < 0) { fprintf(stderr, "Function '%s' not found\n", fn); fprintf(stderr, "ERROR at '%.50s'\n", *p); return 0; }
                }
                *code_pos += snprintf(code_buf + *code_pos, CODE_BUF_SIZE - *code_pos, "<c:%08X>", fid);
                *has_code = 1;
                return 1;
            }
            *p = saved;
        }


        (*p)++;
        return 1;
}

// ============================================================
// ГЛАВНАЯ ФУНКЦИЯ КОМПИЛЯЦИИ
// ============================================================
void compile_to_bytecode(const char* source, uint8_t** out_code, int* out_size,
                         int source_mode, int extended_mode) {

    (void)extended_mode;

    char code_buf[CODE_BUF_SIZE] = {0};
    int code_pos = 0;
    int has_code = 0;

    loop_depth = 0;
    var_table = NULL;
    var_count = 0;
    var_capacity = 0;
    ctx_depth = 0; const_pool_pos = 0; tdf_pos = 0;
    const_count = 0; alias_count = 0; func_count = 0;

    const char* p = source;
    int stmt_num = 0;
/*
    add_string("");  // индекс 0 = пустая строка = "нет модуля"

    // Добавляем пустую строку в выходной пул
    str_pool_pos += snprintf(str_pool_buf + str_pool_pos,
                             STR_POOL_BUF_SIZE - str_pool_pos,
                             "<l:0000>");
                             */


    while (*p) {

        if (!parse_statement(&p, code_buf, &code_pos, str_pool_buf, &str_pool_pos, &has_code, source_mode)) {

            *out_code = NULL; *out_size = 0; return;
        }
        stmt_num++;

    }

    //code_pos += snprintf(code_buf + code_pos, CODE_BUF_SIZE - code_pos, "<E:00000000>");
    // Основной код завершается здесь, но <E> добавим после функций
    // Сначала ставим <E:...> для основного кода
    int main_code_end = code_pos;
    code_pos += snprintf(code_buf + code_pos, CODE_BUF_SIZE - code_pos, "<E:00000000>");

    // Дописываем код всех функций
    // Дописываем код всех функций ПОСЛЕ <E:...>
    for (int i = 0; i < func_count; i++) {
        func_table[i].code_offset = code_pos;
        memcpy(code_buf + code_pos, func_table[i].func_code, func_table[i].func_code_len);
        code_pos += func_table[i].func_code_len;
    }

    // Пересобираем ТДФ с учётом export_flag
    tdf_pos = 0;
    for (int i = 0; i < func_count; i++) {
        const char* tdf_name = func_table[i].name;
        // Для внешних функций отрезаем префикс алиаса
        if (func_table[i].module_name[0]) {
            char* dot = strchr(func_table[i].name, '.');
            if (dot) tdf_name = dot + 1;
        }
        add_func_to_tdf(tdf_name,
                        func_table[i].module_name[0] ? func_table[i].module_name : NULL,
                        func_table[i].param_count,
                        func_table[i].local_count,
                        func_table[i].code_offset,
                        func_table[i].export_flag);
    }
    code_pos += snprintf(code_buf + code_pos, CODE_BUF_SIZE - code_pos, "<E:00000000>");

    int code_start = HEADER_LEN + 5;
    int string_pool_start = code_start + code_pos;
    int const_pool_start = string_pool_start + str_pool_pos;
    int tdf_start = const_pool_start + const_pool_pos;
    int total_size = tdf_start + tdf_pos;

    *out_code = malloc(total_size);
    memset(*out_code, 0, total_size);

    snprintf((char*)*out_code, HEADER_LEN + 1,
             "<OYCE;V=0.06.000;S=%04X;N=%04X;F=%04X;C=%04X;G=%04X;X=%04X>",
             string_pool_start, const_pool_start, tdf_start, code_start, var_count, func_count);

    memset(*out_code + HEADER_LEN, ' ', 5);
    memcpy(*out_code + code_start, code_buf, code_pos);
    memcpy(*out_code + string_pool_start, str_pool_buf, str_pool_pos);
    memcpy(*out_code + const_pool_start, const_pool_buf, const_pool_pos);
    memcpy(*out_code + tdf_start, tdf_buf, tdf_pos);

    *out_size = total_size;
}
