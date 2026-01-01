#pragma once

#define DEBUG 

#ifdef DEBUG
  #include <stdio.h>
  #include <time.h>

  #define DEBUG_PRINTF(fmt, ...) do {                                      \
      time_t now = time(NULL);                                             \
      fprintf(stderr, "\n### %s:%d %s() @ %s",                              \
              __FILE__, __LINE__, __func__, ctime(&now));                  \
      fprintf(stderr, (fmt), ##__VA_ARGS__);                               \
  } while (0)
#else
  #define DEBUG_PRINTF(...) do { } while (0)
#endif