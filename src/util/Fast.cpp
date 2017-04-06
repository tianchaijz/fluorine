#include "fluorine/util/Fast.hpp"

// https://github.com/mnp/libfast-mktime/blob/master/fast-mktime.c
time_t cached_mktime(struct tm *tm) {
  static struct tm cache   = {};
  static time_t time_cache = 0;
  time_t result;
  time_t carry;

  /* the epoch time portion of the request */
  carry = 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;

  if (cache.tm_mday == tm->tm_mday && cache.tm_mon == tm->tm_mon &&
      cache.tm_year == tm->tm_year) {
    result = time_cache + carry;
  } else {
    cache.tm_mday  = tm->tm_mday;
    cache.tm_mon   = tm->tm_mon;
    cache.tm_year  = tm->tm_year;
    time_cache     = mktime(&cache);
    cache.tm_isdst = 0;

    result = (-1 == time_cache) ? -1 : time_cache + carry;
  }

  return result;
}
