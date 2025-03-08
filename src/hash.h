#pragma once
#include <xxh3.h>

#define HASH_64bits(str,len) XXH3_64bits(str, len)