/*
* MIT License
* Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
* See LICENSE file in the root directory for full license text.
*/
#include "nativeBuiltin.h"
#include "vm.h"

//String

static Value lengthNative(int argCount, Value* args)
{
	if (argCount >= 1 && IS_STRING(args[0])) {
		return NUMBER_VAL(AS_STRING(args[0])->length);
	}
	else {
		return NAN_VAL;
	}
}

static Value charLenUTF8Native(int argCount, Value* args) {
	if (argCount >= 1 && IS_STRING(args[0])) {
		//get utf8 length
		ObjString* utf8_string = AS_STRING(args[0]);
		uint32_t char_count = 0;

		for (uint32_t i = 0; i < utf8_string->length; ) {
			uint8_t c = utf8_string->chars[i];

			if ((c & 0x80) == 0) {       // 1 (0xxxxxxx)
				i += 1;
			}
			else if ((c & 0xE0) == 0xC0) { // 2 (110xxxxx)
				i += 2;
			}
			else if ((c & 0xF0) == 0xE0) { // 3 (1110xxxx)
				i += 3;
			}
			else if ((c & 0xF8) == 0xF0) { // 4 (11110xxx)
				i += 4;
			}
			else {
				return NAN_VAL;//invalid utf8
			}
			char_count++;
		}
		return NUMBER_VAL((double)char_count);
	}
	else {
		return NAN_VAL;
	}
}

static Value charAtNative(int argCount, Value* args) {
	if (argCount >= 2 && IS_STRING(args[0]) && IS_NUMBER(args[1])) {
		ObjString* utf8_string = AS_STRING(args[0]);//won't be null

		uint32_t char_count = 0;

		double indexf = AS_NUMBER(args[1]);
		if (indexf < 0 || indexf > utf8_string->length) return NIL_VAL;//out of range
		uint32_t index = (uint32_t)indexf;

		for (uint32_t i = 0, start; i < utf8_string->length; ) {
			uint8_t c = utf8_string->chars[i];
			start = i;

			if ((c & 0x80) == 0) {       // 1 (0xxxxxxx)
				i += 1;
			}
			else if ((c & 0xE0) == 0xC0) { // 2 (110xxxxx)
				i += 2;
			}
			else if ((c & 0xF0) == 0xE0) { // 3 (1110xxxx)
				i += 3;
			}
			else if ((c & 0xF8) == 0xF0) { // 4 (11110xxx)
				i += 4;
			}
			else {
				return NAN_VAL;//invalid utf8
			}

			if (char_count == index) {
				return OBJ_VAL(copyString(utf8_string->chars + start, i - start, false));
			}

			char_count++;
		}
	}

	return NIL_VAL;
}

COLD_FUNCTION
void importNative_string() {
	defineNative_string("length", lengthNative);
	defineNative_string("charLen", charLenUTF8Native);
	defineNative_string("charAt", charAtNative);
}