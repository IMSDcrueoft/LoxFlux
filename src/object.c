/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "object.h"
#include "vm.h"
#include "hash.h"
#include "memory.h"
#include "gc.h"

#if DEBUG_LOG_GC
const C_STR objTypeInfo[] = {
	[OBJ_CLASS] = {"class"},
	[OBJ_CLOSURE] = {"closure"},
	[OBJ_FUNCTION] = {"function"},
	[OBJ_NATIVE] = {"native"},
	[OBJ_UPVALUE] = {"upValue"},
	[OBJ_STRING] = {"string"},
	[OBJ_STRING_BUILDER] = {"stringBuilder"},
	[OBJ_ARRAY] = {"array"},
	[OBJ_ARRAY_F64] = {"arrayF64"},
	[OBJ_ARRAY_F32] = {"arrayF32"},
	[OBJ_ARRAY_U32] = {"arrayU32"},
	[OBJ_ARRAY_I32] = {"arrayI32"},
	[OBJ_ARRAY_U16] = {"arrayU16"},
	[OBJ_ARRAY_I16] = {"arrayI16"},
	[OBJ_ARRAY_U8] = {"arrayU8"},
	[OBJ_ARRAY_I8] = {"arrayI8"},
};
#endif

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

#define ALLOCATE_FLEX_OBJ(type,objectType,byteSize) \
    (type*)allocateObject(byteSize, objectType)

HOT_FUNCTION
static Obj* allocateObject(size_t size, ObjType type) {
	Obj* object = NULL;

	//link the objects
	switch (type) {
	case OBJ_FUNCTION:
	case OBJ_NATIVE:
	case OBJ_STRING:
		object = (Obj*)reallocate_no_gc(NULL, 0, size);
		object->type = type;
		object->isMarked = !usingMark;
		object->next = vm.objects_no_gc;
		vm.objects_no_gc = object;
		break;
	default:
		object = (Obj*)reallocate(NULL, 0, size);
		object->type = type;
		object->isMarked = !usingMark;
		object->next = vm.objects;
		vm.objects = object;
		break;
	}

#if DEBUG_LOG_GC
	printf("[gc] %p allocate %zu for (%s)\n", (Unknown_ptr)object, size, objTypeInfo[type]);
#endif

	return object;
}

HOT_FUNCTION
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

HOT_FUNCTION
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

ObjClass* newClass(ObjString* name)
{
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	return klass;
}

HOT_FUNCTION
ObjInstance* newInstance(ObjClass* klass) {
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	instance->fields.type = TABLE_NORMAL;
	table_init(&instance->fields);
	return instance;
}

ObjArray* newArray(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(Value, size);

	return array;
}

ObjArray* newArrayF64(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_F64);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(double, size);

	return array;
}

ObjArray* newArrayF32(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_F32);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(float, size);

	return array;
}

ObjArray* newArrayU32(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_U32);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(uint32_t, size);

	return array;
}

ObjArray* newArrayI32(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_I32);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(int32_t, size);

	return array;
}

ObjArray* newArrayU16(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_U16);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(uint16_t, size);

	return array;
}

ObjArray* newArrayI16(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_I16);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(int16_t, size);

	return array;
}

ObjArray* newArrayU8(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_U8);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(uint8_t, size);

	return array;
}

ObjArray* newArrayI8(uint64_t size)
{
	//align to 8
	size = max(8, (size + 7) & ~7);
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY_I8);
	array->capacity = size;
	array->length = 0;
	array->payload = (char*)ALLOCATE(int8_t, size);

	return array;
}

COLD_FUNCTION
void reserveArray(ObjArray* array, uint64_t size)
{
	size = max(8, (size + 7) & ~7);
	if (size < array->capacity) return;
	if (size > UINT32_MAX) {
		fprintf(stderr, "Array size overflow");
		exit(1);
	}

#define GROW_TYPED_ARRY(type, ptr, size) reallocate(ptr, sizeof(type) * array->capacity, sizeof(type) * size)
	Unknown_ptr newPayload = NULL;

	switch (array->obj.type) {
	case OBJ_ARRAY:
		newPayload = GROW_TYPED_ARRY(Value, array->payload, size);
		break;
	case OBJ_ARRAY_F64:
		newPayload = GROW_TYPED_ARRY(double, array->payload, size);
		break;
	case OBJ_ARRAY_F32:
		newPayload = GROW_TYPED_ARRY(float, array->payload, size);
		break;
	case OBJ_ARRAY_U32:
		newPayload = GROW_TYPED_ARRY(uint32_t, array->payload, size);
		break;
	case OBJ_ARRAY_I32:
		newPayload = GROW_TYPED_ARRY(int32_t, array->payload, size);
		break;
	case OBJ_ARRAY_U16:
		newPayload = GROW_TYPED_ARRY(uint16_t, array->payload, size);
		break;
	case OBJ_ARRAY_I16:
		newPayload = GROW_TYPED_ARRY(int16_t, array->payload, size);
		break;
	case OBJ_ARRAY_U8:
		newPayload = GROW_TYPED_ARRY(uint8_t, array->payload, size);
		break;
	case OBJ_ARRAY_I8:
		newPayload = GROW_TYPED_ARRY(int8_t, array->payload, size);
		break;
	case OBJ_STRING_BUILDER:
		newPayload = GROW_TYPED_ARRY(char, array->payload, size);
		break;
	}

	array->payload = (char*)newPayload;
	array->capacity = size;
#undef GROW_TYPED_ARRY
}

COLD_FUNCTION
Value getStringValue(ObjString* string, uint32_t index)
{
	//There are only a maximum of 256 characters
	return OBJ_VAL(copyString(string->chars + index, 1, false));
}

COLD_FUNCTION
Value getStringBuilderValue(ObjArray* stringBuilder, uint32_t index) {
	//There are only a maximum of 256 characters
	return OBJ_VAL(copyString(stringBuilder->payload + index, 1, false));
}

//it don't check index so be careful
Value getTypedArrayElement(ObjArray* array, uint32_t index)
{
	switch (array->obj.type) {
	case OBJ_ARRAY_F64:
		return NUMBER_VAL(ARRAY_ELEMENT(array, double, index));
	case OBJ_ARRAY_F32:
		return NUMBER_VAL(ARRAY_ELEMENT(array, float, index));
	case OBJ_ARRAY_U32:
		return NUMBER_VAL(ARRAY_ELEMENT(array, uint32_t, index));
	case OBJ_ARRAY_I32:
		return NUMBER_VAL(ARRAY_ELEMENT(array, int32_t, index));
	case OBJ_ARRAY_U16:
		return NUMBER_VAL(ARRAY_ELEMENT(array, uint16_t, index));
	case OBJ_ARRAY_I16:
		return NUMBER_VAL(ARRAY_ELEMENT(array, int16_t, index));
	case OBJ_ARRAY_U8:
		return NUMBER_VAL(ARRAY_ELEMENT(array, uint8_t, index));
	case OBJ_ARRAY_I8:
		return NUMBER_VAL(ARRAY_ELEMENT(array, int8_t, index));
	case OBJ_STRING_BUILDER:
		return NUMBER_VAL(ARRAY_ELEMENT(array, uint8_t, index));
	default:
		return NIL_VAL;
	}
}

void setTypedArrayElement(ObjArray* array, uint32_t index, Value val)
{
	val = IS_NUMBER(val) ? val : NUMBER_VAL(0);
	switch (array->obj.type) {
	case OBJ_ARRAY_F64:
		ARRAY_ELEMENT(array, double, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_F32:
		ARRAY_ELEMENT(array, float, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_U32:
		ARRAY_ELEMENT(array, uint32_t, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_I32:
		ARRAY_ELEMENT(array, int32_t, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_U16:
		ARRAY_ELEMENT(array, uint16_t, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_I16:
		ARRAY_ELEMENT(array, int16_t, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_U8:
		ARRAY_ELEMENT(array, uint8_t, index) = AS_NUMBER(val);
		break;
	case OBJ_ARRAY_I8:
		ARRAY_ELEMENT(array, int8_t, index) = AS_NUMBER(val);
		break;
	}
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

			//stack_push(OBJ_VAL(string));
			tableSet(&vm.strings, string, BOOL_VAL(true));
			//stack_pop();
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
			//stack_push(OBJ_VAL(string));
			tableSet(&vm.strings, string, BOOL_VAL(true));
			//stack_pop();
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
		//stack_push(OBJ_VAL(string));
		tableSet(&vm.strings, string, BOOL_VAL(true));
		//stack_pop();
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
	else if (function->name->length == 0) {
		printf("<lambda>");
		return;
	}

	printf("<fn %s>", function->name->chars);
}

static void printArrayLike(ObjArray* array, bool isExpand) {
	if (array->obj.type == OBJ_STRING_BUILDER) {
		printf("%s", array->payload);
		return;
	}

	if (!isExpand) {
		switch (array->obj.type) {
		case OBJ_ARRAY:
			printf("<array>");
			break;
		case OBJ_ARRAY_F64:
			printf("<array-f64>");
			break;
		case OBJ_ARRAY_F32:
			printf("<array-f32>");
			break;
		case OBJ_ARRAY_U32:
			printf("<array-u32>");
			break;
		case OBJ_ARRAY_I32:
			printf("<array-i32>");
			break;
		case OBJ_ARRAY_U16:
			printf("<array-u16>");
			break;
		case OBJ_ARRAY_I16:
			printf("<array-i16>");
			break;
		case OBJ_ARRAY_U8:
			printf("<array-u8>");
			break;
		case OBJ_ARRAY_I8:
			printf("<array-i8>");
			break;
		}
	}
	else {
		if (array->length > 0) {
			printf("[ ");
			for (uint32_t i = 0; i < array->length;) {
				switch (array->obj.type) {
				case OBJ_ARRAY: //don't expand now ,or it's too slow and too much
					printValue(ARRAY_ELEMENT(array, Value, i));
					break;
				case OBJ_ARRAY_F64:
					print_adaptive_double(ARRAY_ELEMENT(array, double, i));
					break;
				case OBJ_ARRAY_F32:
					print_adaptive_double(ARRAY_ELEMENT(array, float, i));
					break;
				case OBJ_ARRAY_U32:
					printf("%d", ARRAY_ELEMENT(array, uint32_t, i));
					break;
				case OBJ_ARRAY_I32:
					printf("%d", ARRAY_ELEMENT(array, int32_t, i));
					break;
				case OBJ_ARRAY_U16:
					printf("%d", ARRAY_ELEMENT(array, uint16_t, i));
					break;
				case OBJ_ARRAY_I16:
					printf("%d", ARRAY_ELEMENT(array, int16_t, i));
					break;
				case OBJ_ARRAY_U8:
					printf("%d", ARRAY_ELEMENT(array, uint8_t, i));
					break;
				case OBJ_ARRAY_I8:
					printf("%d", ARRAY_ELEMENT(array, int8_t, i));
					break;
				}

				if (++i < array->length) {
					printf(", ");
				}
			}
			printf(" ]");
		}
		else {
			printf("[]");
		}
	}
}

void printObject(Value value, bool isExpand) {
	switch (OBJ_TYPE(value)) {
	case OBJ_CLASS: {
		ObjString* name = AS_CLASS(value)->name;
		if (name != NULL) {
			printf("%s (class)", name->chars);
		}
		else {
			printf("$anon (class)");
		}
		break;
	}
	case OBJ_INSTANCE: {
		ObjClass* klass = AS_INSTANCE(value)->klass;
		if (klass != NULL) {
			printf("%s (instance)", klass->name->chars);
		}
		else {
			printf("$anon (instance)");
		}
		break;
	}
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
		printf("%s", AS_STRING(value)->chars);
		break;
	case OBJ_UPVALUE:
		printf("upvalue");
		break;
	default:
		if (isIndexableArray(value)) {
			ObjArray* array = AS_ARRAY(value);
			printArrayLike(array, isExpand);
		}
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