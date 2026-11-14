#include "cJSON.h"
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cjson_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

static int cjson_strcasecmp(const char *s1, const char *s2) {
    if (!s1) return (s1 == s2) ? 0 : 1;
    if (!s2) return 1;
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

static cJSON *cJSON_New_Item(void) {
    cJSON *node = (cJSON *)calloc(1, sizeof(cJSON));
    return node;
}

void cJSON_Delete(cJSON *item) {
    cJSON *next = NULL;
    while (item != NULL) {
        next = item->next;
        if (item->child != NULL) {
            cJSON_Delete(item->child);
        }
        if (item->valuestring != NULL) {
            free(item->valuestring);
        }
        if (item->string != NULL) {
            free(item->string);
        }
        free(item);
        item = next;
    }
}

/* Parser implementation */
typedef struct {
    const char *content;
    size_t length;
    size_t offset;
    size_t depth;
} parse_buffer;

static void skip_whitespace(parse_buffer *buf) {
    while (buf->offset < buf->length) {
        unsigned char c = (unsigned char)buf->content[buf->offset];
        if (c <= 32) {
            buf->offset++;
        } else {
            break;
        }
    }
}

static int parse_value(cJSON *item, parse_buffer *buf);

static int parse_string(cJSON *item, parse_buffer *buf) {
    if (buf->offset >= buf->length || buf->content[buf->offset] != '\"') {
        return 0;
    }
    buf->offset++; /* skip opening quote */
    size_t start = buf->offset;
    size_t alloc_len = 0;
    while (buf->offset < buf->length && buf->content[buf->offset] != '\"') {
        if (buf->content[buf->offset] == '\\') {
            buf->offset++;
            if (buf->offset >= buf->length) return 0;
        }
        buf->offset++;
        alloc_len++;
    }
    if (buf->offset >= buf->length || buf->content[buf->offset] != '\"') {
        return 0;
    }
    size_t end = buf->offset;
    buf->offset++; /* skip closing quote */

    char *out = (char *)malloc(alloc_len + 1);
    if (!out) return 0;

    size_t i = start;
    size_t j = 0;
    while (i < end) {
        if (buf->content[i] != '\\') {
            out[j++] = buf->content[i++];
        } else {
            i++;
            if (i >= end) {
                free(out);
                return 0;
            }
            char esc = buf->content[i++];
            switch (esc) {
                case 'b': out[j++] = '\b'; break;
                case 'f': out[j++] = '\f'; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                case '\"': out[j++] = '\"'; break;
                case '\\': out[j++] = '\\'; break;
                case '/': out[j++] = '/'; break;
                case 'u': {
                    if (i + 4 > end) {
                        free(out);
                        return 0;
                    }
                    unsigned int uc = 0;
                    for (int k = 0; k < 4; k++) {
                        char hc = buf->content[i++];
                        uc <<= 4;
                        if (hc >= '0' && hc <= '9') uc |= (unsigned int)(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') uc |= (unsigned int)(hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') uc |= (unsigned int)(hc - 'A' + 10);
                        else {
                            free(out);
                            return 0;
                        }
                    }
                    if (uc < 0x80) {
                        out[j++] = (char)uc;
                    } else if (uc < 0x800) {
                        out[j++] = (char)(0xC0 | (uc >> 6));
                        out[j++] = (char)(0x80 | (uc & 0x3F));
                    } else {
                        out[j++] = (char)(0xE0 | (uc >> 12));
                        out[j++] = (char)(0x80 | ((uc >> 6) & 0x3F));
                        out[j++] = (char)(0x80 | (uc & 0x3F));
                    }
                    break;
                }
                default:
                    free(out);
                    return 0;
            }
        }
    }
    out[j] = '\0';
    item->type = cJSON_String;
    item->valuestring = out;
    return 1;
}

static int parse_number(cJSON *item, parse_buffer *buf) {
    char num_str[64];
    size_t i = 0;
    while (buf->offset < buf->length && i < sizeof(num_str) - 1) {
        char c = buf->content[buf->offset];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') {
            num_str[i++] = c;
            buf->offset++;
        } else {
            break;
        }
    }
    if (i == 0) return 0;
    num_str[i] = '\0';

    char *endptr = NULL;
    double val = strtod(num_str, &endptr);
    if (endptr != num_str + i || isnan(val) || isinf(val)) return 0;

    item->type = cJSON_Number;
    item->valuedouble = val;
    if (val >= (double)INT_MAX) {
        item->valueint = INT_MAX;
    } else if (val <= (double)INT_MIN) {
        item->valueint = INT_MIN;
    } else {
        item->valueint = (int)val;
    }
    return 1;
}

static int parse_array(cJSON *item, parse_buffer *buf) {
    if (buf->depth >= 512) return 0;
    buf->depth++;
    buf->offset++; /* skip '[' */
    skip_whitespace(buf);
    if (buf->offset < buf->length && buf->content[buf->offset] == ']') {
        buf->offset++;
        buf->depth--;
        item->type = cJSON_Array;
        return 1;
    }

    cJSON *head = NULL;
    cJSON *current = NULL;
    while (buf->offset < buf->length) {
        cJSON *elem = cJSON_New_Item();
        if (!elem) {
            cJSON_Delete(head);
            return 0;
        }
        skip_whitespace(buf);
        if (!parse_value(elem, buf)) {
            cJSON_Delete(elem);
            cJSON_Delete(head);
            return 0;
        }
        if (!head) {
            head = elem;
            current = elem;
        } else {
            current->next = elem;
            elem->prev = current;
            current = elem;
        }
        skip_whitespace(buf);
        if (buf->offset < buf->length && buf->content[buf->offset] == ',') {
            buf->offset++;
            skip_whitespace(buf);
            continue;
        } else if (buf->offset < buf->length && buf->content[buf->offset] == ']') {
            buf->offset++;
            buf->depth--;
            item->type = cJSON_Array;
            item->child = head;
            return 1;
        } else {
            cJSON_Delete(head);
            return 0;
        }
    }
    cJSON_Delete(head);
    return 0;
}

static int parse_object(cJSON *item, parse_buffer *buf) {
    if (buf->depth >= 512) return 0;
    buf->depth++;
    buf->offset++; /* skip '{' */
    skip_whitespace(buf);
    if (buf->offset < buf->length && buf->content[buf->offset] == '}') {
        buf->offset++;
        buf->depth--;
        item->type = cJSON_Object;
        return 1;
    }

    cJSON *head = NULL;
    cJSON *current = NULL;
    while (buf->offset < buf->length) {
        cJSON *member = cJSON_New_Item();
        if (!member) {
            cJSON_Delete(head);
            return 0;
        }
        skip_whitespace(buf);
        if (!parse_string(member, buf)) {
            cJSON_Delete(member);
            cJSON_Delete(head);
            return 0;
        }
        member->string = member->valuestring;
        member->valuestring = NULL;

        skip_whitespace(buf);
        if (buf->offset >= buf->length || buf->content[buf->offset] != ':') {
            cJSON_Delete(member);
            cJSON_Delete(head);
            return 0;
        }
        buf->offset++; /* skip ':' */
        skip_whitespace(buf);
        if (!parse_value(member, buf)) {
            cJSON_Delete(member);
            cJSON_Delete(head);
            return 0;
        }

        if (!head) {
            head = member;
            current = member;
        } else {
            current->next = member;
            member->prev = current;
            current = member;
        }

        skip_whitespace(buf);
        if (buf->offset < buf->length && buf->content[buf->offset] == ',') {
            buf->offset++;
            skip_whitespace(buf);
            continue;
        } else if (buf->offset < buf->length && buf->content[buf->offset] == '}') {
            buf->offset++;
            buf->depth--;
            item->type = cJSON_Object;
            item->child = head;
            return 1;
        } else {
            cJSON_Delete(head);
            return 0;
        }
    }
    cJSON_Delete(head);
    return 0;
}

static int parse_value(cJSON *item, parse_buffer *buf) {
    skip_whitespace(buf);
    if (buf->offset >= buf->length) return 0;

    const char *rem = buf->content + buf->offset;
    size_t rem_len = buf->length - buf->offset;

    if (rem_len >= 4 && strncmp(rem, "null", 4) == 0) {
        item->type = cJSON_NULL;
        buf->offset += 4;
        return 1;
    }
    if (rem_len >= 4 && strncmp(rem, "true", 4) == 0) {
        item->type = cJSON_True;
        item->valueint = 1;
        item->valuedouble = 1.0;
        buf->offset += 4;
        return 1;
    }
    if (rem_len >= 5 && strncmp(rem, "false", 5) == 0) {
        item->type = cJSON_False;
        item->valueint = 0;
        item->valuedouble = 0.0;
        buf->offset += 5;
        return 1;
    }
    if (*rem == '\"') {
        return parse_string(item, buf);
    }
    if (*rem == '-' || (*rem >= '0' && *rem <= '9')) {
        return parse_number(item, buf);
    }
    if (*rem == '[') {
        return parse_array(item, buf);
    }
    if (*rem == '{') {
        return parse_object(item, buf);
    }
    return 0;
}

cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length) {
    if (!value || buffer_length == 0) return NULL;
    parse_buffer buf;
    buf.content = value;
    buf.length = buffer_length;
    buf.offset = 0;
    buf.depth = 0;

    cJSON *item = cJSON_New_Item();
    if (!item) return NULL;

    if (!parse_value(item, &buf)) {
        cJSON_Delete(item);
        return NULL;
    }
    skip_whitespace(&buf);
    if (buf.offset < buf.length && buf.content[buf.offset] != '\0') {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

cJSON *cJSON_Parse(const char *value) {
    if (!value) return NULL;
    return cJSON_ParseWithLength(value, strlen(value));
}

/* Printer implementation */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} str_buf;

static int sb_ensure(str_buf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return 1;
    size_t new_cap = (sb->cap == 0) ? 256 : sb->cap * 2;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    char *new_data = (char *)realloc(sb->data, new_cap);
    if (!new_data) return 0;
    sb->data = new_data;
    sb->cap = new_cap;
    return 1;
}

static int sb_append_n(str_buf *sb, const char *s, size_t n) {
    if (!sb_ensure(sb, n)) return 0;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_append(str_buf *sb, const char *s) {
    return sb_append_n(sb, s, strlen(s));
}

static int sb_append_char(str_buf *sb, char c) {
    if (!sb_ensure(sb, 1)) return 0;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}

static int print_string_ptr(const char *str, str_buf *sb) {
    if (!str) return sb_append(sb, "\"\"");
    if (!sb_append_char(sb, '\"')) return 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '\"': if (!sb_append(sb, "\\\"")) return 0; break;
            case '\\': if (!sb_append(sb, "\\\\")) return 0; break;
            case '\b': if (!sb_append(sb, "\\b")) return 0; break;
            case '\f': if (!sb_append(sb, "\\f")) return 0; break;
            case '\n': if (!sb_append(sb, "\\n")) return 0; break;
            case '\r': if (!sb_append(sb, "\\r")) return 0; break;
            case '\t': if (!sb_append(sb, "\\t")) return 0; break;
            default:
                if (c < 32) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned int)c);
                    if (!sb_append(sb, hex)) return 0;
                } else {
                    if (!sb_append_char(sb, (char)c)) return 0;
                }
                break;
        }
    }
    return sb_append_char(sb, '\"');
}

static int print_number(const cJSON *item, str_buf *sb) {
    double d = item->valuedouble;
    if (isnan(d) || isinf(d)) {
        return sb_append(sb, "0");
    }
    char num_buf[64];
    double intpart;
    if (modf(d, &intpart) == 0.0 && d >= -9007199254740992.0 && d <= 9007199254740992.0) {
        snprintf(num_buf, sizeof(num_buf), "%.0f", d);
    } else {
        snprintf(num_buf, sizeof(num_buf), "%.6g", d);
    }
    return sb_append(sb, num_buf);
}

static int print_value(const cJSON *item, int depth, int fmt, str_buf *sb);

static int print_indent(int depth, str_buf *sb) {
    for (int i = 0; i < depth; i++) {
        if (!sb_append(sb, "  ")) return 0;
    }
    return 1;
}

static int print_array(const cJSON *item, int depth, int fmt, str_buf *sb) {
    if (!sb_append_char(sb, '[')) return 0;
    cJSON *child = item->child;
    if (!child) return sb_append_char(sb, ']');
    if (fmt && !sb_append_char(sb, '\n')) return 0;
    while (child) {
        if (fmt && !print_indent(depth + 1, sb)) return 0;
        if (!print_value(child, depth + 1, fmt, sb)) return 0;
        if (child->next) {
            if (!sb_append_char(sb, ',')) return 0;
        }
        if (fmt && !sb_append_char(sb, '\n')) return 0;
        child = child->next;
    }
    if (fmt && !print_indent(depth, sb)) return 0;
    return sb_append_char(sb, ']');
}

static int print_object(const cJSON *item, int depth, int fmt, str_buf *sb) {
    if (!sb_append_char(sb, '{')) return 0;
    cJSON *child = item->child;
    if (!child) return sb_append_char(sb, '}');
    if (fmt && !sb_append_char(sb, '\n')) return 0;
    while (child) {
        if (fmt && !print_indent(depth + 1, sb)) return 0;
        if (!print_string_ptr(child->string ? child->string : "", sb)) return 0;
        if (!sb_append(sb, fmt ? ": " : ":")) return 0;
        if (!print_value(child, depth + 1, fmt, sb)) return 0;
        if (child->next) {
            if (!sb_append_char(sb, ',')) return 0;
        }
        if (fmt && !sb_append_char(sb, '\n')) return 0;
        child = child->next;
    }
    if (fmt && !print_indent(depth, sb)) return 0;
    return sb_append_char(sb, '}');
}

static int print_value(const cJSON *item, int depth, int fmt, str_buf *sb) {
    if (!item) return 0;
    switch (item->type & 0xFF) {
        case cJSON_NULL:   return sb_append(sb, "null");
        case cJSON_False:  return sb_append(sb, "false");
        case cJSON_True:   return sb_append(sb, "true");
        case cJSON_Number: return print_number(item, sb);
        case cJSON_String: return print_string_ptr(item->valuestring, sb);
        case cJSON_Array:  return print_array(item, depth, fmt, sb);
        case cJSON_Object: return print_object(item, depth, fmt, sb);
        default:           return 0;
    }
}

char *cJSON_Print(const cJSON *item) {
    if (!item) return NULL;
    str_buf sb = {NULL, 0, 0};
    if (!print_value(item, 0, 1, &sb)) {
        free(sb.data);
        return NULL;
    }
    return sb.data;
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    if (!item) return NULL;
    str_buf sb = {NULL, 0, 0};
    if (!print_value(item, 0, 0, &sb)) {
        free(sb.data);
        return NULL;
    }
    return sb.data;
}

/* Accessors */
int cJSON_GetArraySize(const cJSON *array) {
    if (!array) return 0;
    int count = 0;
    cJSON *c = array->child;
    while (c) {
        count++;
        c = c->next;
    }
    return count;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    if (!array || index < 0) return NULL;
    cJSON *c = array->child;
    while (c && index > 0) {
        index--;
        c = c->next;
    }
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string) {
    if (!object || !string) return NULL;
    cJSON *c = object->child;
    while (c && c->string) {
        if (cjson_strcasecmp(c->string, string) == 0) return c;
        c = c->next;
    }
    return NULL;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string) {
    if (!object || !string) return NULL;
    cJSON *c = object->child;
    while (c && c->string) {
        if (strcmp(c->string, string) == 0) return c;
        c = c->next;
    }
    return NULL;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string) {
    return cJSON_GetObjectItem(object, string) != NULL ? 1 : 0;
}

/* Type checks */
int cJSON_IsInvalid(const cJSON * const item) { return item ? (item->type == cJSON_Invalid) : 0; }
int cJSON_IsFalse(const cJSON * const item)   { return item ? (item->type == cJSON_False) : 0; }
int cJSON_IsTrue(const cJSON * const item)    { return item ? (item->type == cJSON_True) : 0; }
int cJSON_IsBool(const cJSON * const item)    { return item ? (item->type == cJSON_True || item->type == cJSON_False) : 0; }
int cJSON_IsNull(const cJSON * const item)    { return item ? (item->type == cJSON_NULL) : 0; }
int cJSON_IsNumber(const cJSON * const item)  { return item ? (item->type == cJSON_Number) : 0; }
int cJSON_IsString(const cJSON * const item)  { return item ? (item->type == cJSON_String) : 0; }
int cJSON_IsArray(const cJSON * const item)   { return item ? (item->type == cJSON_Array) : 0; }
int cJSON_IsObject(const cJSON * const item)  { return item ? (item->type == cJSON_Object) : 0; }

/* Creators */
cJSON *cJSON_CreateNull(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_NULL;
    return item;
}

cJSON *cJSON_CreateTrue(void) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_True;
        item->valueint = 1;
        item->valuedouble = 1.0;
    }
    return item;
}

cJSON *cJSON_CreateFalse(void) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_False;
        item->valueint = 0;
        item->valuedouble = 0.0;
    }
    return item;
}

cJSON *cJSON_CreateBool(int boolean) {
    return boolean ? cJSON_CreateTrue() : cJSON_CreateFalse();
}

cJSON *cJSON_CreateNumber(double num) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        if (num >= (double)INT_MAX) item->valueint = INT_MAX;
        else if (num <= (double)INT_MIN) item->valueint = INT_MIN;
        else item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_String;
        item->valuestring = cjson_strdup(string ? string : "");
        if (!item->valuestring) {
            free(item);
            return NULL;
        }
    }
    return item;
}

cJSON *cJSON_CreateArray(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Array;
    return item;
}

cJSON *cJSON_CreateObject(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Object;
    return item;
}

cJSON *cJSON_Duplicate(const cJSON *item, int recurse) {
    if (!item) return NULL;
    cJSON *newitem = cJSON_New_Item();
    if (!newitem) return NULL;
    newitem->type = item->type;
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring) {
        newitem->valuestring = cjson_strdup(item->valuestring);
        if (!newitem->valuestring) {
            cJSON_Delete(newitem);
            return NULL;
        }
    }
    if (item->string) {
        newitem->string = cjson_strdup(item->string);
        if (!newitem->string) {
            cJSON_Delete(newitem);
            return NULL;
        }
    }
    if (!recurse) return newitem;

    cJSON *child = item->child;
    cJSON *newchild = NULL;
    cJSON *last = NULL;
    while (child) {
        newchild = cJSON_Duplicate(child, 1);
        if (!newchild) {
            cJSON_Delete(newitem);
            return NULL;
        }
        if (!newitem->child) {
            newitem->child = newchild;
        } else {
            last->next = newchild;
            newchild->prev = last;
        }
        last = newchild;
        child = child->next;
    }
    return newitem;
}

/* Mutators */
int cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    if (!array || !item || array == item) return 0;
    cJSON *c = array->child;
    if (!c) {
        array->child = item;
        item->prev = NULL;
        item->next = NULL;
    } else {
        while (c->next) c = c->next;
        c->next = item;
        item->prev = c;
        item->next = NULL;
    }
    return 1;
}

int cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!object || !string || !item || object == item) return 0;
    char *new_key = cjson_strdup(string);
    if (!new_key) return 0;
    if (item->string) free(item->string);
    item->string = new_key;
    return cJSON_AddItemToArray(object, item);
}

cJSON *cJSON_AddNullToObject(cJSON * const object, const char * const name) {
    cJSON *n = cJSON_CreateNull();
    if (cJSON_AddItemToObject(object, name, n)) return n;
    cJSON_Delete(n);
    return NULL;
}

cJSON *cJSON_AddTrueToObject(cJSON * const object, const char * const name) {
    cJSON *t = cJSON_CreateTrue();
    if (cJSON_AddItemToObject(object, name, t)) return t;
    cJSON_Delete(t);
    return NULL;
}

cJSON *cJSON_AddFalseToObject(cJSON * const object, const char * const name) {
    cJSON *f = cJSON_CreateFalse();
    if (cJSON_AddItemToObject(object, name, f)) return f;
    cJSON_Delete(f);
    return NULL;
}

cJSON *cJSON_AddBoolToObject(cJSON * const object, const char * const name, const int boolean) {
    cJSON *b = cJSON_CreateBool(boolean);
    if (cJSON_AddItemToObject(object, name, b)) return b;
    cJSON_Delete(b);
    return NULL;
}

cJSON *cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number) {
    cJSON *n = cJSON_CreateNumber(number);
    if (cJSON_AddItemToObject(object, name, n)) return n;
    cJSON_Delete(n);
    return NULL;
}

cJSON *cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string) {
    cJSON *s = cJSON_CreateString(string);
    if (cJSON_AddItemToObject(object, name, s)) return s;
    cJSON_Delete(s);
    return NULL;
}

cJSON *cJSON_AddObjectToObject(cJSON * const object, const char * const name) {
    cJSON *o = cJSON_CreateObject();
    if (cJSON_AddItemToObject(object, name, o)) return o;
    cJSON_Delete(o);
    return NULL;
}

cJSON *cJSON_AddArrayToObject(cJSON * const object, const char * const name) {
    cJSON *a = cJSON_CreateArray();
    if (cJSON_AddItemToObject(object, name, a)) return a;
    cJSON_Delete(a);
    return NULL;
}
