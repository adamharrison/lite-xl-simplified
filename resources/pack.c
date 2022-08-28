#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compar(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }
int main(int argc, char* argv[]) {
  printf("#if LITE_ALL_IN_ONE\nconst char* internal_packed_files[] = {\n");
  qsort(&argv[1], argc - 1, sizeof(char*), compar);
  for (int i = 1; i < argc; ++i) {
    FILE* file = fopen(argv[i], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
      continue;
    int length = ftell(file);
    if (length == -1)
      continue;
    fseek(file, 0, SEEK_SET);
    unsigned char* contents = malloc(length+1);
    fread(contents, sizeof(char), length, file);
    contents[length] = 0;
    printf("\"%%INTERNAL%%/%s\",", argv[i]);
    printf("\"");
    for (int j = 0; j < length; ++j)
      printf("\\x%02x", contents[j]);
    printf("\",");
    printf("(void*)%d,\n", length);
    free(contents);
    fclose(file);
  }
  printf("(void*)0, (void*)0, (void*)0\n};\n#endif");
  return 0;
}
