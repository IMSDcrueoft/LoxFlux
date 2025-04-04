/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "object.h"
#include "vm.h"
#include "hash.h"
#include "memory.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

#define ALLOCATE_FLEX_OBJ(type,objectType,byteSize) \
    (type*)allocateObject(byteSize, objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    //link the objects
    object->next = vm.objects;
    object->isMarked = false;
    vm.objects = object;

#if DEBUG_LOG_GC
    printf("[gc] %p allocate %zu for type-%d\n", (Unknown_ptr)object, size, type);
#endif

    return object;
}

ObjUpvalue* newUpvalue(Value* slot)
{
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
	upvalue->next = NULL;
    return upvalue;
}

ObjFunction* newFunction() {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    chuck_init(&function->chunk);
    return function;
}

ObjClosure* newClosure(ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int32_t i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
  
    return closure;
}

//create native function
ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

//if find deduplicate one return it else null
static inline ObjString* deduplicateString(C_STR chars, uint32_t length, uint64_t hash) {
	return tableFindString(&vm.strings, chars, length, hash);
}

//will check '\\' '\"'
ObjString* copyString(C_STR chars, uint32_t length, bool escapeChars)
{
    uint32_t heapSize = sizeof(ObjString) + 1;
    ObjString* string = NULL;
    uint64_t hash;

    if (!escapeChars) {
        hash = HASH_64bits(chars, length);

        //find deduplicate one
        string = deduplicateString(chars, length, hash);
        if (string == NULL) {
            //create string
			heapSize += length;
			string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, heapSize);

			memcpy(string->chars, chars, length);
			string->chars[length] = '\0';
			string->length = length;

			string->hash = hash;
            string->symbol = INVALID_OBJ_STRING_SYMBOL;

            stack_push(OBJ_VAL(string));
            tableSet(&vm.strings, string, BOOL_VAL(true));
            stack_pop();
        }

        return string;
    }
    else {
        uint32_t actualLength = 0;

        for (uint32_t i = 0; i < length; ++i) {
            if (chars[i] == '\\') {
                if (i + 1 < length) {
                    switch (chars[i + 1]) {
                    case '\\':
                    case '"':
                        ++i;
                        break;
                    default:
                        ++actualLength;
                        ++i;
                        break;
                    }
                }
            }

            ++actualLength;
		}

		heapSize += actualLength;
		string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, heapSize);

        for (uint32_t readIndex = 0, writeIndex = 0; readIndex < length;) {
            uint32_t start = readIndex;

            while (readIndex < length && chars[readIndex] != '\\') {
                ++readIndex;
            }

            if (start < readIndex) {
                memcpy(string->chars + writeIndex, chars + start, readIndex - start);
                writeIndex += readIndex - start;
            }

            if ((readIndex + 1) < length) {
                switch (chars[readIndex + 1]) {
                case '\\':
                case '\"':
                    string->chars[writeIndex] = chars[readIndex + 1];
                    ++writeIndex;
                    break;
                default:
                    string->chars[writeIndex] = '\\';
                    ++writeIndex;
                    string->chars[writeIndex] = chars[readIndex + 1];
                    ++writeIndex;
                    break;
                }
                readIndex += 2;
            }
        }

		string->chars[actualLength] = '\0';
		string->length = actualLength;

        //the string escaped
        hash = HASH_64bits(string->chars, string->length);
        string->hash = hash;
        string->symbol = INVALID_OBJ_STRING_SYMBOL;

        //find deduplicate one
        ObjString* interned = deduplicateString(string->chars, string->length, string->hash);
        if (interned == NULL) {
            stack_push(OBJ_VAL(string));
            tableSet(&vm.strings, string, BOOL_VAL(true));
            stack_pop();
            return string;
        }
        else {
            //free memory
            FREE(ObjString, string);
            return interned;
        }
	}
}

ObjString* connectString(ObjString* strA, ObjString* strB) {
    uint32_t heapSize = sizeof(ObjString) + strA->length + strB->length + 1;
    ObjString* string = ALLOCATE_FLEX_OBJ(ObjString, OBJ_STRING, heapSize);

    memcpy(string->chars, strA->chars, strA->length);
    memcpy(string->chars + strA->length, strB->chars, strB->length);
    string->chars[strA->length + strB->length] = '\0';
    string->length = strA->length + strB->length;

    uint64_t hash = HASH_64bits(string->chars, string->length);
    string->hash = hash;
    string->symbol = INVALID_OBJ_STRING_SYMBOL;

    //do deduplicate
    ObjString* interned = deduplicateString(string->chars, string->length, string->hash);
	if (interned == NULL) {
        stack_push(OBJ_VAL(string));
		tableSet(&vm.strings, string, BOOL_VAL(true));
        stack_pop();
		return string;
	}
	else {
		//free memory
		FREE(ObjString, string);
		return interned;
	}
}

static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }

    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
        printFunction(AS_CLOSURE(value)->function);
        break;
    case OBJ_FUNCTION:
        printFunction(AS_FUNCTION(value));
        break;
    case OBJ_NATIVE:
        printf("<native fn>");
        break;
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    case OBJ_UPVALUE:
        printf("upvalue");
        break;
    }
}

Entry* getStringEntryInPool(ObjString* string)
{
	return tableGetStringEntry(&vm.strings, string);
}

NumberEntry* getNumberEntryInPool(Value* value)
{
	return tableGetNumberEntry(&vm.numbers, value);
}