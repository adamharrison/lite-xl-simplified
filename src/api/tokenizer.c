#include "api.h"
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#define PATTERN_REGEX 1
#define MAX_TOKEN_MATCHES 256

struct pattern {
  int flags;
  pcre2_code* re;
  char str[];
};

struct symbol_type {
  char str[16];
};

struct rule {
  struct pattern* patterns[3];
  struct syntax* subsyntax;
  bool inline_syntax;
  unsigned char symbol_type_length;
  struct symbol_type* symbol_types;
  int flags;
};

struct symbol {
  char key[64];
  char value[16];
};

struct syntax {
  unsigned char rule_length;
  unsigned char max_stateful_length;
  struct rule* rules;
  unsigned int symbol_length;
  struct symbol* symbols;
};

static int sort_symbols(const void* a, const void* b) { return strcmp(((struct symbol*)a)->key, ((struct symbol*)b)->key); }
static const char* next_utf8_character(const char* p) { while ((*(++p) & 0xC0) == 0x80) { } return p; }
// This pattern matching function should be identical lua's with the following enhancements:
// %a, %w, %l, %u, %c match any UTF-8 character over codepoint 128.
size_t match_pattern_internal(const char* pattern, const char* token, size_t token_length, size_t offset, size_t* matched_lengths, bool next_match_only) {
  size_t last_offset = offset;
  const char* start_pattern = pattern, *end_pattern = pattern + strlen(pattern);
  const char* start_token = token + offset, *end_token = token + token_length;
  int open_square = 0, inverted_character_class = 0;
  const char* frontier_token = NULL, *frontier_pattern = NULL;
  const char *start_character_class = start_pattern;
  const char* last_greedy_match = NULL;
  int last_greedy_match_count = 0;
  int match_min = 1, match_max = 1, match_count = 0, matched_idx = 0;
  bool matches = false, finished_pattern = false, must_terminate_token = false; 
  if (*start_pattern == '^') {
    if (offset != 0)
      return 0;
    ++start_pattern;
  }
  if (*(end_pattern - 1) == '$') {
    must_terminate_token = true;
    --end_pattern;
  }
  while (start_pattern < end_pattern) {
    if (start_token == end_token) { 
      if (finished_pattern) {
        if (match_count >= match_min)
          break;
        return 0;
      } else if (last_greedy_match) {
        start_token = last_greedy_match;
        last_greedy_match = NULL;
        ++start_pattern;
        continue;
      }
      return 0;
    }
    if (open_square && matches && (*start_pattern != ']' || *(start_pattern-1) == '%')) {
      ++start_pattern;
      continue;
    }
    if (*start_pattern != ']' && !open_square)
      start_character_class = start_pattern;
    switch (*start_pattern) {
      case 0: return 0; break;
      case '.': matches = true; ++start_pattern; break;
      case '(': if (open_square > 0) goto default_match; start_pattern++; continue;
      case ')': 
        if (open_square > 0) 
          goto default_match; 
        if (matched_lengths)
          matched_lengths[matched_idx++] = start_token - (token + offset);
        offset += start_token - (token + last_offset);
        last_offset = offset;
        start_character_class = ++start_pattern;
        continue;
      case '[': 
        if (open_square++ == 0) 
          start_character_class = start_pattern++;
        if (*start_pattern == '^') {
          inverted_character_class = 1; 
          start_pattern++;
        }
        continue;
      case ']': --open_square; ++start_pattern; break;
      case '%': {
        char type = *(start_pattern+1);
        int lower = tolower(type);
        switch (lower) { // %w and other character classes defined here.
          case 0: return 0; break;
          case 'a': matches = isalpha(*start_token) || type > 128; break;
          case 'w': matches = isalnum(*start_token) || type > 128; break;
          case 'l': matches = islower(*start_token) || type > 128; break;
          case 'u': matches = isupper(*start_token) || type > 128; break;
          case 'd': matches = isdigit(*start_token); break;
          case 'g': matches = isgraph(*start_token); break;
          case 'c': matches = iscntrl(*start_token); break;
          case 'p': matches = ispunct(*start_token); break;
          case 's': matches = isspace(*start_token); break;
          case 'x': matches = isxdigit(*start_token); break;
          case 'z': matches = *start_token == 0; break;
          case 'f': frontier_pattern = start_pattern; start_pattern += 2; frontier_token = start_token; continue; break;
          default: matches = (type == *start_token); break;
        }
        if (lower != type) matches = !matches; // if capitalized, invert the character class
        start_pattern += 2;
      } break;
      default: 
      default_match: 
        matches = (*(start_pattern++) == *start_token); 
      break;
    }
    if (open_square)
      continue;
    if (inverted_character_class)
      matches = !matches;
    if (next_match_only)
      return matches > 0 ? 1 : 0;
    
    bool recent_match = matches;
    if (matches) {
      ++match_count;
      matches = false;
    }
    if (!open_square) {
      if (frontier_pattern) {
        bool inverted_frontier = *(frontier_pattern + 3) == '^';
        if (start_token - 1 < token && inverted_frontier)
          return 0;
        bool out_of_set_prev = (((start_token - 1 < token && !inverted_frontier)) || !match_pattern_internal(frontier_pattern + 2, token, token_length, start_token - token - 1, NULL, true));
        bool in_set_current = match_pattern_internal(frontier_pattern + 2, token, token_length, start_token - token, NULL, true);
        if (!out_of_set_prev || !in_set_current)
          return 0;
        start_token = frontier_token - 1;
        frontier_token = NULL;
        frontier_pattern = NULL;
      }
      bool greedy = false;
      switch (*start_pattern) {
        case '-': match_min = 0; match_max = INT_MAX; ++start_pattern; greedy = false; break;
        case '*': match_min = 0; match_max = INT_MAX; ++start_pattern; greedy = true; break;
        case '+': match_min = 1; match_max = INT_MAX; ++start_pattern; greedy = true; break;
        case '?': match_min = 0; match_max = 1      ; ++start_pattern; greedy = true; break;
      }
      finished_pattern = start_pattern == end_pattern;
      if (recent_match && match_count < match_max) {
        bool matches_next = match_pattern_internal(start_pattern, token, token_length, start_token - token, NULL, true);
        if (greedy || !matches_next) {
          if (matches_next) {
            last_greedy_match = start_token;
            last_greedy_match_count = match_count;
          }
          start_pattern = start_character_class; 
        } else
          finished_pattern = ++start_pattern == end_pattern;
        start_token = next_utf8_character(start_token);
        continue;
      }
    } 
    if (open_square) {
      start_pattern++;
      continue;
    }
    if (match_count < match_min || match_count > match_max)
      return 0;
    match_min = 1;
    match_max = 1;
    if (match_count > 0 && recent_match)
      start_token = next_utf8_character(start_token);
    match_count = 0;
    start_character_class = start_pattern;
    inverted_character_class = 0;
  }  
  if (matched_lengths)
    matched_lengths[matched_idx++] = start_token - (token + offset);
  if (must_terminate_token && start_token != end_token)
    return 0;
  return matched_idx;
}

size_t match_pattern(struct pattern* pattern, const char* token, size_t length, size_t offset, size_t* matched_lengths) {
  if ((pattern->flags & 0x1) == PATTERN_REGEX) {
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(pattern->re, NULL);
    int rc = pcre2_match(pattern->re, (PCRE2_SPTR)&token[offset], length - offset, 0, 0, md, NULL);
    if (rc < 0) {
      pcre2_match_data_free(md);
      return 0;
    }
    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(md);
    if (ovector[0] > ovector[1]) {
      pcre2_match_data_free(md);
      return 0;
    }
    for (int i = 0; i < rc*2 && i < MAX_TOKEN_MATCHES; i++)
      matched_lengths[i] = ovector[i]+offset+1;
    pcre2_match_data_free(md);
    return rc*2;
  }
  return match_pattern_internal(pattern->str, token, length, offset, matched_lengths, false);
}

struct subsyntax_info {
  unsigned char rule_idx;
  unsigned char subsyntax_idx;
  struct syntax* subsyntax;
  struct syntax* parent;
  unsigned char parent_rule_idx;
};



static struct subsyntax_info get_subsyntax_details(struct syntax* syntax, unsigned long long state) {
  unsigned char current_state, i, parent_rule_idx = 0;
  struct syntax* parent = NULL;
  for (i = 0; i < 4; ++i) {
    current_state = (state >> (i * 8)) & 0xFF;
    if (current_state && syntax->rules[current_state -1].subsyntax) {
      parent_rule_idx = current_state - 1;
      parent = syntax;
      syntax = syntax->rules[current_state - 1].subsyntax;
    } else
      break;
  }
  return (struct subsyntax_info){ current_state, i, syntax, parent, parent_rule_idx };
}

static int emit_token(lua_State* L, struct syntax* self, struct symbol_type* symbol_type, int offset, int start, const char* str, int idx, const char** last_token) {
  if (offset - start > 0) {
    const char* symbol = "normal";
    if (symbol_type) {
      struct symbol key, *target = NULL; 
      if (offset - start < sizeof(key.key)) {
        strncpy(key.key, &str[start], offset - start);
        key.key[offset - start] = 0;
        target = bsearch(&key, self->symbols, self->symbol_length, sizeof(struct symbol), sort_symbols);
      }
      symbol = target ? target->value : symbol_type->str;
    }
    if (*last_token && strcmp(*last_token, symbol) == 0) {
      lua_rawgeti(L, -1, (idx - 1) * 2 + 2);
      size_t length = lua_tointeger(L, -1) + (offset - start);
      lua_pop(L, 1);
      lua_pushinteger(L, length);
      lua_rawseti(L, -2, (idx - 1) * 2 + 2);
      return 0;
    }
    lua_pushstring(L, symbol);
    lua_rawseti(L, -2, idx * 2 + 1);
    lua_pushinteger(L, offset - start);
    lua_rawseti(L, -2, idx * 2 + 2);
    *last_token = symbol;
    return 1;
  }
  return 0;
}

static int total_lines_tokenized = 0;

static int tokenize_line(lua_State* L, struct syntax* self, struct syntax* initial_target, const char* line, size_t length, unsigned long long* state, bool quick) {
  size_t offset = 0, last_emission = 0, amount_matched = 0, i;
  size_t matched_lengths[MAX_TOKEN_MATCHES];
  const char* last_symbol = NULL;
  struct subsyntax_info info = get_subsyntax_details(initial_target, *state);
  struct syntax* target = info.subsyntax;
  loop_start:
  while (offset < length) { 
    int rule_length = quick ? target->max_stateful_length : target->rule_length;

    if (info.subsyntax_idx) {
      size_t match_lengths = 0;
      if (info.parent->rules[info.parent_rule_idx].patterns[2])
        match_lengths = match_pattern(info.parent->rules[info.parent_rule_idx].patterns[2], line, length, offset, matched_lengths);
      if (match_lengths == 0 && info.parent->rules[info.parent_rule_idx].patterns[1]) {
        match_lengths = match_pattern(info.parent->rules[info.parent_rule_idx].patterns[1], line, length, offset, matched_lengths);
        if (match_lengths > 0) {
          *state = (*state & ~(0xFFFF << ((info.subsyntax_idx - 1) * 8)));
          info = get_subsyntax_details(initial_target, *state);
          target = info.subsyntax;
        }
      }
    }

    
    if (!info.rule_idx) {
      for (i = 0; i < rule_length; ++i) {
        size_t match_lengths = match_pattern(target->rules[i].patterns[0], line, length, offset, matched_lengths);
        if (match_lengths > 0) {
          if (last_emission < offset) {
            amount_matched += !quick ? emit_token(L, target, NULL, offset, last_emission, line, amount_matched, &last_symbol) : 0;
            last_emission = offset;
          }
          if (!target->rules[i].subsyntax && target->rules[i].patterns[1]) {
            info.rule_idx = i + 1;
            *state = (*state & ~(0xFF << (info.subsyntax_idx * 8))) | ((i + 1) << (info.subsyntax_idx * 8));
            offset += matched_lengths[0];
          } else {
            for (int j = 0; j < match_lengths; ++j) {
              amount_matched += !quick ? emit_token(L, target, j < target->rules[i].symbol_type_length ? &target->rules[i].symbol_types[j] : NULL, last_emission + matched_lengths[j], last_emission, line, amount_matched, &last_symbol) : 0;
              last_emission += matched_lengths[j];
              offset += matched_lengths[j];
            }
            if (target->rules[i].subsyntax) {
              *state = (*state & ~(0xFF << (info.subsyntax_idx * 8))) | ((i + 1) << (info.subsyntax_idx * 8));
              info = get_subsyntax_details(initial_target, *state);
              target = info.subsyntax;
              goto loop_start;
            }
          }
          break;
        }
      }
      if (i == rule_length) { // move to the end of the word, if we don't have a rule.
        while (offset < length && isalnum(line[offset++]));
      }
    } else {
      size_t match_lengths = info.rule_idx && target->rules[info.rule_idx - 1].patterns[2] ? match_pattern(target->rules[info.rule_idx - 1].patterns[2], line, length, offset, matched_lengths) : 0;
      if (match_lengths) {
        offset += matched_lengths[0] + 1;
      } else {
        match_lengths = match_pattern(target->rules[info.rule_idx - 1].patterns[1], line, length, offset, matched_lengths);
        if (match_lengths > 0) {
          for (int j = 0; j < match_lengths; ++j) {
            offset += matched_lengths[j];
            amount_matched += !quick ? emit_token(L, target, j < target->rules[info.rule_idx - 1].symbol_type_length ? &target->rules[info.rule_idx - 1].symbol_types[j] : NULL, offset, last_emission, line, amount_matched, &last_symbol) : 0;
            last_emission = offset;
          }
          *state = (*state & ~(0xFF << (info.subsyntax_idx * 8)));
          info = get_subsyntax_details(initial_target, *state);
          target = info.subsyntax;
        } else if (offset < length)
          ++offset;
      }
    }
  }
  amount_matched += !quick ? emit_token(L, target, info.rule_idx > 0 ? &target->rules[info.rule_idx - 1].symbol_types[0] : NULL, offset, last_emission, line, amount_matched, &last_symbol) : 0;
  *state = (*state & ~(0xFF << (info.subsyntax_idx * 8))) | (info.rule_idx << (info.subsyntax_idx * 8));
  total_lines_tokenized += 1;
  return amount_matched;
}


static int f_tokenize(lua_State* L) {
  bool quick = !lua_isnil(L, 4) && lua_toboolean(L, 4);
  lua_getfield(L, 1, "native");
  struct syntax* self = lua_touserdata(L, -1);
  size_t length;
  const char* line = luaL_checklstring(L, 2, &length);
  unsigned long long state = luaL_checkinteger(L, 3);
  if (quick)
    lua_pushnil(L);
  else
    lua_newtable(L);
  tokenize_line(L, self, self, line, length, &state, quick);
  lua_pushinteger(L, state);
  return 2;
}

static struct pattern* lua_topattern(lua_State* L, int index, bool regex) {
  size_t len;
  if (!regex) {
    const char* str = luaL_checklstring(L, index, &len);
    struct pattern* pattern = calloc(1 ,sizeof(struct pattern) + len + 1);
    strncpy(pattern->str, str, len);
    pattern->str[len] = 0;
    return pattern;
  }
  size_t regex_len;
  PCRE2_SPTR regex_str = (PCRE2_SPTR)lua_tolstring(L, index, &regex_len);
  PCRE2_SIZE error_offset;
  int error_number;
  pcre2_code* re = pcre2_compile(regex_str, regex_len, PCRE2_UTF, &error_number, &error_offset, NULL);
  if (!re) {
    PCRE2_UCHAR error_message[256];
    pcre2_get_error_message(error_number, error_message, sizeof(error_message));
    luaL_error(L, "error compiling regex '%s': %s at offset %d", regex_str, error_message, error_number);
  }
  struct pattern* pattern = calloc(1 ,sizeof(struct pattern));
  pattern->re = re;
  pattern->flags = PATTERN_REGEX;
  return pattern;
}

static int f_new_syntax(lua_State* L) {
  int argument_index = lua_gettop(L);
  size_t len;
  lua_newtable(L);
  luaL_setmetatable(L, "Tokenizer");
  struct syntax* self = lua_newuserdata(L, sizeof(struct syntax));
  lua_setfield(L, -2, "native");
  lua_newtable(L);
  int internal_syntax_table = lua_gettop(L);
  lua_getfield(L, -4, "patterns");
  self->rule_length = lua_rawlen(L, -1);
  self->max_stateful_length = 0;
  self->rules = calloc(self->rule_length, sizeof(struct rule));
  for (size_t i = 0; i < self->rule_length; ++i) {
    lua_rawgeti(L, -1, i+1);
    lua_getfield(L, -1, "pattern");
    bool regex = false;
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, -1, "regex");
      if (lua_isnil(L, -1))
        return luaL_error(L, "unparseable rule %d", i);
      regex = true;
    }
    if (lua_type(L, -1) == LUA_TTABLE) {
      size_t elements = lua_rawlen(L, -1);
      self->max_stateful_length = i + 1;
      for (size_t j = 0; j < elements; ++j) {
        lua_rawgeti(L, -1, j+1);
        self->rules[i].patterns[j] = lua_topattern(L, -1, j <= 1 ? regex : false);
        lua_pop(L, 1);
      }
    } else if (lua_type(L, -1) == LUA_TSTRING)
      self->rules[i].patterns[0] = lua_topattern(L, -1, regex);
    lua_pop(L, 1);
    lua_getfield(L, -1, "type");
    if (lua_type(L, -1) == LUA_TTABLE) {
      self->rules[i].symbol_type_length = lua_rawlen(L, -1);
      self->rules[i].symbol_types = calloc(self->rules[i].symbol_type_length, sizeof(struct symbol_type));
      for (size_t j = 0; j < self->rules[i].symbol_type_length; ++j) {
        lua_rawgeti(L, -1, j+1);
        const char* str = luaL_checklstring(L, -1, &len);
        strncpy(self->rules[i].symbol_types[j].str, str, sizeof(self->rules[i].symbol_types[j].str));
        lua_pop(L, 1);
      }
    } else {
      lua_getfield(L, -3, "name");
      lua_pop(L, 1);
      self->rules[i].symbol_type_length = 1;
      self->rules[i].symbol_types = calloc(self->rules[i].symbol_type_length, sizeof(struct symbol_type));
      const char* str = luaL_checklstring(L, -1, &len);
      strncpy(self->rules[i].symbol_types[0].str, str, sizeof(self->rules[i].symbol_types[0].str));
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "syntax"); // Subyntax support; either inline, or through passed function.
    if (lua_type(L, -1) == LUA_TTABLE) {
      lua_pushvalue(L, argument_index - 1);
      f_new_syntax(L);
      lua_getfield(L, -1, "native");
      self->rules[i].subsyntax = lua_touserdata(L, -1);
      self->rules[i].inline_syntax = true;
      lua_pop(L, 1);
      lua_rawseti(L, internal_syntax_table, lua_rawlen(L, internal_syntax_table) + 1);
      lua_pop(L, 1);
    } else if (lua_type(L, -1) == LUA_TSTRING && lua_type(L, internal_syntax_table - 2) == LUA_TFUNCTION) {
      lua_pushvalue(L, internal_syntax_table - 2);
      lua_pushvalue(L, -2);
      lua_call(L, 1, 1);
      if (!lua_isnil(L, -1)) {
        lua_getfield(L, -1, "native");
        self->rules[i].subsyntax = lua_touserdata(L, -1);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 2);
  }
  lua_pop(L, 1);
  lua_getfield(L, 1, "symbols");
  luaL_checktype(L, -1, LUA_TTABLE);
  lua_pushnil(L); 
  self->symbol_length = 0;
  while (lua_next(L, -2) != 0) {
    ++self->symbol_length;
    lua_pop(L, 1);
  }
  lua_pushnil(L); 
  int i = 0;
  self->symbols = calloc(sizeof(struct symbol), self->symbol_length);
  while (lua_next(L, -2) != 0) {
    strncpy(self->symbols[i].key, luaL_checkstring(L, -2), sizeof(self->symbols[i].key));
    strncpy(self->symbols[i].value, luaL_checkstring(L, -1), sizeof(self->symbols[i].value));
    ++i;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_setfield(L, -1, "internal_syntaxes");
  qsort(self->symbols, self->symbol_length, sizeof(struct symbol), sort_symbols);
  return 1;
}

static int f_gc(lua_State* L) {
  lua_getfield(L, 1, "native");
  struct syntax* self = lua_touserdata(L, -1);
  for (size_t i = 0; i < self->rule_length; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      if (self->rules[i].patterns[j])
        free(self->rules[i].patterns[j]);
    }
    free(self->rules[i].symbol_types);
    if (self->rules[i].subsyntax && self->rules[i].inline_syntax)
      free(self->rules[i].subsyntax);
  }
  free(self->symbols);
  return 0;
}

static const luaL_Reg tokenizer_lib[] = {
  {"new" , f_new_syntax},
  {"tokenize", f_tokenize},
  {"__gc", f_gc},
  {NULL, NULL}
};

int luaopen_tokenizer(lua_State* L) {
  luaL_newmetatable(L, "Tokenizer");
  luaL_setfuncs(L, tokenizer_lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
