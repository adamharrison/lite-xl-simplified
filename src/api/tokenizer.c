#include "api.h"
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

struct pattern {
  int flags;
  char str[];
};

struct symbol_type {
  char str[12];
};

struct rule {
  struct pattern* patterns[3];
  struct syntax* subsyntax;
  unsigned char symbol_type_length;
  struct symbol_type* symbol_types;
  int flags;
};

struct symbol {
  char key[64];
  char value[12];
};

struct syntax {
  unsigned char rule_length;
  unsigned char max_stateful_length;
  struct rule* rules;
  unsigned int symbol_length;
  struct symbol* symbols;
};

static int sort_symbols(const void* a, const void* b) { return strcmp(((struct symbol*)a)->key, ((struct symbol*)b)->key); }

// This pattern matching function should be identical lua's with the following enhancements:
// %a, %w, %l, %u, %c match any UTF-8 character over codepoint 128.
size_t match_pattern_internal(const char* start_pattern, const char* token, size_t token_length, size_t offset, size_t* matched_lengths, bool next_match_only) {
  size_t last_offset = offset;
  const char* start_token = token + offset, *end_token = token + token_length;
  int open_square = 0, inverted_character_class = 0;
  const char* positive_lookahead_assertion = NULL;
  const char *start_character_class = start_pattern;
  int match_min = 1, match_max = 1, match_count = 0, matched_idx = 0;
  bool matches = false; 
  while (*start_pattern) {
    if (start_token == end_token) { 
      if (match_count >= match_min)
        break;
      return 0;
    }
    if (open_square && matches && (*start_pattern != ']' || *(start_pattern-1) == '%')) {
      ++start_pattern;
      continue;
    }
    switch (*start_pattern) {
      case 0: return 0; break;
      case '.': matches = true; ++start_pattern; break;
      case '^': if (start_token != token) return 0; start_character_class = ++start_pattern; continue;
      case '$': if (start_token < end_token) return 0; break;
      case '(': if (open_square > 0) goto default_match; start_pattern++; continue;
      case ')': 
        if (open_square > 0) 
          goto default_match; 
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
          case 'c': matches = iscntrl(*start_token); break;
          case 'p': matches = ispunct(*start_token); break;
          case 's': matches = isspace(*start_token); break;
          case 'x': matches = isxdigit(*start_token); break;
          case 'f': start_pattern += 2; positive_lookahead_assertion = start_token; continue; break;
          default: matches = (type == *start_token); break;
        }
        if (lower != type) matches = !matches; // if capitalized, invert the character class
        start_pattern += 2;
      } break;
      default: 
      default_match: matches = (*(start_pattern++) == *start_token); break;
    }
    if (open_square)
      continue;
    if (inverted_character_class)
      matches = !matches;
    if (next_match_only)
      return matches ? 1 : 0;
    bool recent_match = matches;
    if (matches) {
      ++match_count;
      matches = false;
    }
    if (!open_square) {
      if (positive_lookahead_assertion) {
         start_token = positive_lookahead_assertion - 1;
         positive_lookahead_assertion = NULL;
      }
      bool greedy = false;
      switch (*start_pattern) {
        case '-': match_min = 0; match_max = INT_MAX; ++start_pattern; greedy = false; break;
        case '*': match_min = 0; match_max = INT_MAX; ++start_pattern; greedy = true; break;
        case '+': match_min = 1; match_max = INT_MAX; ++start_pattern; greedy = true; break;
        case '?': match_min = 0; match_max = 1      ; ++start_pattern; greedy = true; break;
      }
      if (recent_match && match_count < match_max) {
        if (greedy || !match_pattern_internal(start_pattern + 1, start_token, token_length - (start_token - token), offset, NULL, true)) {
          start_pattern = start_character_class; 
          ++start_token;
          continue; 
        }
      } else
        open_square = 0;
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
      ++start_token;
    match_count = 0;
    start_character_class = start_pattern;
    inverted_character_class = 0;
  }
  if (matched_lengths)
    matched_lengths[matched_idx++] = start_token - (token + offset);
  return matched_idx;
}

size_t match_pattern(struct pattern* pattern, const char* token, size_t length, size_t offset, size_t* matched_lengths) {
  return match_pattern_internal(pattern->str, token, length, offset, matched_lengths, false);
}

struct subsyntax_info {
  unsigned char rule_idx;
  unsigned char subsyntax_idx;
};

static struct subsyntax_info get_subsyntax_details(unsigned long long state) {
  if (state >= 1 << 24) {
    return (struct subsyntax_info){ (state >> 24) & 0xFF, 3 };
  } if (state >= 1 << 16)
    return (struct subsyntax_info){ (state >> 16) & 0xFF, 2 };
  if (state >= 1 << 8)
    return (struct subsyntax_info){ (state >> 8) & 0xFF, 1 };
  return (struct subsyntax_info){ state & 0xFF, 0 };
}

static int emit_token(lua_State* L, struct syntax* self, struct symbol_type* symbol_type, int offset, int start, const char* str, int idx) {
  if (offset - start > 0) {
    if (symbol_type) {
      struct symbol key, *target = NULL; 
      if (offset - start < sizeof(key.key)) {
        strncpy(key.key, &str[start], offset - start);
        key.key[offset - start] = 0;
        target = bsearch(&key, self->symbols, self->symbol_length, sizeof(struct symbol), sort_symbols);
      }
      lua_pushstring(L, target ? target->value : symbol_type->str);
    } else
      lua_pushliteral(L, "normal");
    lua_rawseti(L, -2, idx * 2 + 1);
    lua_pushinteger(L, offset - start);
    lua_rawseti(L, -2, idx * 2 + 2);
    return 1;
  }
  return 0;
}

static int total_lines_tokenized = 0;

static int tokenize_line(lua_State* L, struct syntax* self, struct syntax* target, const char* line, size_t length, unsigned long long* state, bool quick) {
  struct subsyntax_info info = get_subsyntax_details(*state);
  size_t offset = 0, i;
  size_t last_emission = offset;
  int amount_matched = 0;
  size_t matched_lengths[256];
  int rule_length = quick ? target->max_stateful_length : target->rule_length;
  while (offset < length) {
    if (!info.rule_idx) {
      for (i = 0; i < rule_length; ++i) {
        size_t match_lengths = match_pattern(target->rules[i].patterns[0], line, length, offset, matched_lengths);
        if (match_lengths > 0) {
          // fprintf(stderr, "MATCH: `%s` `%s`\n", target->rules[i].patterns[0]->str, &line[offset]);
          if (last_emission < offset) {
            amount_matched += !quick ? emit_token(L, self, NULL, offset, last_emission, line, amount_matched) : 0;
            last_emission = offset;
          }
          if (target->rules[i].patterns[1]) {
            info.rule_idx = i + 1;
            offset += matched_lengths[0];
          } else {
            amount_matched += !quick ? emit_token(L, self, NULL, offset, last_emission, line, amount_matched) : 0;
            if (target->rules[i].subsyntax)
              *state = (*state & ~(0xFF << (info.subsyntax_idx * 8))) | (i << (info.subsyntax_idx * 8));
              
            last_emission = offset;
            for (int j = 0; j < match_lengths; ++j) {
              amount_matched += !quick ? emit_token(L, self, j < target->rules[i].symbol_type_length ? &target->rules[i].symbol_types[j] : NULL, last_emission + matched_lengths[j], last_emission, line, amount_matched) : 0;
              last_emission += matched_lengths[j];
              offset += matched_lengths[j];
            }
          }
          break;
        }
      }
      if (i == rule_length && isalnum(line[++offset-1])) { // move to the end of the word, if we don't have a rule.
        while (offset < length && isalnum(line[offset]))
          ++offset;
      }
    } else {
      size_t match_lengths = target->rules[info.rule_idx - 1].patterns[2] ? match_pattern(target->rules[info.rule_idx - 1].patterns[2], line, length, offset, matched_lengths) : 0;
      if (match_lengths) {
        offset += matched_lengths[0] + 1;
      } else {
        match_lengths = match_pattern(target->rules[info.rule_idx - 1].patterns[1], line, length, offset, matched_lengths);
        if (match_lengths > 0) {
          for (int j = 0; j < match_lengths; ++j) {
            offset += matched_lengths[j];
            amount_matched += !quick ? emit_token(L, self, j < target->rules[info.rule_idx - 1].symbol_type_length ? &target->rules[info.rule_idx - 1].symbol_types[j] : NULL, offset, last_emission, line, amount_matched) : 0;
            last_emission = offset;
          }
          info.rule_idx = 0;
        } else {
          ++offset;
        }
      }
    }
  }
  amount_matched += !quick ? emit_token(L, self, info.rule_idx > 0 ? &target->rules[info.rule_idx - 1].symbol_types[0] : NULL, offset, last_emission, line, amount_matched) : 0;
  *state = (*state & ~(0xFF << (info.rule_idx * 8))) | (info.rule_idx << (info.subsyntax_idx * 8));
  total_lines_tokenized += 1;
  return amount_matched;
}

struct syntax* get_syntax(struct syntax* self, unsigned long long state) {
  struct syntax* syntax = self;
  for (int i = 0; i < 4; ++i) {
    unsigned char current_state = state >> (i * 16) & 0xFF;
    //if (!(syntax->rules[current_state].type & FLAG_HAS_SUBSYNTAX))
      return syntax;
    syntax = syntax->rules[current_state].subsyntax;
  }
  return syntax;
}

static int f_tokenize(lua_State* L) {
  bool quick = !lua_isnil(L, 4) && lua_toboolean(L, 4);
  lua_getfield(L, 1, "native");
  struct syntax* self = lua_touserdata(L, -1);
  size_t length;
  const char* line = luaL_checklstring(L, 2, &length);
  unsigned long long state = luaL_checkinteger(L, 3);
  struct syntax* target = get_syntax(self, state);
  if (quick)
    lua_pushnil(L);
  else
    lua_newtable(L);
  tokenize_line(L, self, target, line, length, &state, quick);
  lua_pushinteger(L, state);
  return 2;
}

static int f_new_syntax(lua_State* L) {
  size_t len;
  lua_newtable(L);
  luaL_setmetatable(L, "Tokenizer");
  struct syntax* self = lua_newuserdata(L, sizeof(struct syntax));
  lua_setfield(L, -2, "native");
  lua_newtable(L);
  int internal_syntax_table = lua_gettop(L);
  lua_getfield(L, 1, "patterns");
  self->rule_length = lua_rawlen(L, -1);
  self->max_stateful_length = 0;
  self->rules = calloc(self->rule_length, sizeof(struct rule));
  for (size_t i = 0; i < self->rule_length; ++i) {
    lua_rawgeti(L, -1, i+1);
    lua_getfield(L, -1, "pattern");
    if (lua_type(L, -1) == LUA_TTABLE) {
      size_t elements = lua_rawlen(L, -1);
      self->max_stateful_length = i + 1;
      for (size_t j = 0; j < elements; ++j) {
        lua_rawgeti(L, -1, j+1);
        const char* str = luaL_checklstring(L, -1, &len);
        self->rules[i].patterns[j] = calloc(1 ,sizeof(struct pattern) + len + 1);
        strncpy(self->rules[i].patterns[j]->str, str, len);
        lua_pop(L, 1);
      }
    } else {
      const char* str = luaL_checklstring(L, -1, &len);
      self->rules[i].patterns[0] = calloc(1, sizeof(struct pattern) + len + 1);
      strncpy(self->rules[i].patterns[0]->str, str, len);
    }
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
      lua_getfield(L, 1, "name");
      lua_pop(L, 1);
      self->rules[i].symbol_type_length = 1;
      self->rules[i].symbol_types = calloc(self->rules[i].symbol_type_length, sizeof(struct symbol_type));
      const char* str = luaL_checklstring(L, -1, &len);
      strncpy(self->rules[i].symbol_types[0].str, str, sizeof(self->rules[i].symbol_types[0].str));
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "syntax"); // Subyntax support; either inline, or through passed function.
    if (lua_type(L, -1) == LUA_TTABLE) {
      f_new_syntax(L);
      self->rules[i].subsyntax = lua_touserdata(L, -1);
      lua_rawseti(L, internal_syntax_table, luaL_len(L, internal_syntax_table) + 1);
    } else if (lua_type(L, -1) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
      lua_pushvalue(L, 2);
      lua_pushvalue(L, -2);
      lua_call(L, 1, 1);
      if (!lua_isnil(L, -1)) {
        lua_getfield(L, -1, "native");
        self->rules[i].subsyntax = lua_touserdata(L, -1);
      }
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
    if (self->rules[i].subsyntax)
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
