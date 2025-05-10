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
static Obj* allocateObject(uint64_t size, ObjType type) {
	Obj* object = NULL;

	//link the objects
	switch (type) {
	case OBJ_FUNCTION:
	case OBJ_NATIVE:
	case OBJ_STRING:
		object = (Obj*)reallocate_no_gc(NULL, 0, size);
		OBJ_PTR_SET_NEXT(object, vm.objects_no_gc);
		object->type = type;
		object->isMarked = !vm.gcMark;
		vm.objects_no_gc = object;
		break;
	default:
		object = (Obj*)reallocate(NULL, 0, size);
		OBJ_PTR_SET_NEXT(object, vm.objects);
		object->type = type;
		object->isMarked = !vm.gcMark;
		vm.objects = object;
		break;
	}

#if DEBUG_LOG_GC
	printf("[gc] %p allocate %zu for (%s)\n", (void*)object, size, objTypeInfo[type]);
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
	function->id = vm.functionID++;//unique id
	function->name = NULL;
	chuck_init(&function->chunk);
	return function;
}

HOT_FUNCTION
ObjClosure* newClosure(ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (uint32_t i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;

	return closure;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method)
{
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
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
	klass->initializer = NIL_VAL;
	klass->methods.type = TABLE_NORMAL;
	table_init(&klass->methods);
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

HOT_FUNCTION
ObjArray* newArray(ObjType type) {
	ObjArray* array = ALLOCATE_OBJ(ObjArray, type);
	array->capacity = 0;
	array->length = 0;
	array->payload = NULL;
	return array;
}

HOT_FUNCTION
void reserveArray(ObjArray* array, uint64_t size)
{
	size = (size + 7) & ~7;
	if (size < array->capacity) return;
	if (size > ARRAYLIKE_MAX) {
		fprintf(stderr, "ArrayLike size overflow");
		exit(1);
	}

#define GROW_TYPED_ARRAY(type, ptr, size) reallocate(ptr, sizeof(type) * array->capacity, sizeof(type) * size)
	void* newPayload = NULL;

	switch (OBJ_GET_TYPE(array->obj)) {
	case OBJ_ARRAY:
		newPayload = GROW_TYPED_ARRAY(Value, array->payload, size);
		break;
	case OBJ_ARRAY_F64:
		newPayload = GROW_TYPED_ARRAY(double, array->payload, size);
		break;
	case OBJ_ARRAY_F32:
		newPayload = GROW_TYPED_ARRAY(float, array->payload, size);
		break;
	case OBJ_ARRAY_U32:
		newPayload = GROW_TYPED_ARRAY(uint32_t, array->payload, size);
		break;
	case OBJ_ARRAY_I32:
		newPayload = GROW_TYPED_ARRAY(int32_t, array->payload, size);
		break;
	case OBJ_ARRAY_U16:
		newPayload = GROW_TYPED_ARRAY(uint16_t, array->payload, size);
		break;
	case OBJ_ARRAY_I16:
		newPayload = GROW_TYPED_ARRAY(int16_t, array->payload, size);
		break;
	case OBJ_ARRAY_U8:
		newPayload = GROW_TYPED_ARRAY(uint8_t, array->payload, size);
		break;
	case OBJ_ARRAY_I8:
		newPayload = GROW_TYPED_ARRAY(int8_t, array->payload, size);
		break;
	case OBJ_STRING_BUILDER:
		newPayload = GROW_TYPED_ARRAY(char, array->payload, size);
		break;
	}

	array->payload = newPayload;
	array->capacity = size;
#undef GROW_TYPED_ARRAY
}

//it don't check index so be careful
HOT_FUNCTION
Value getTypedArrayElement(ObjArray* array, uint32_t index)
{
	switch (OBJ_GET_TYPE(array->obj)) {
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

HOT_FUNCTION
void setTypedArrayElement(ObjArray* array, uint32_t index, Value val)
{
	val = IS_NUMBER(val) ? val : NUMBER_VAL(0);
	switch (OBJ_GET_TYPE(array->obj)) {
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
			tableSet_string(&vm.strings, string);
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
			tableSet_string(&vm.strings, string);
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
		tableSet_string(&vm.strings, string);
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
		printf("<script> (%d)", function->id);
	}
	else if (function->name->length == 0) {
		printf("<lambda> (%d)", function->id);
	}
	else {
		printf("<fn %s> (%d)", function->name->chars, function->id);
	}
}

static void printArrayLike(ObjArray* array, bool isExpand) {
	if (OBJ_GET_TYPE(array->obj) == OBJ_STRING_BUILDER) {
		printf("%s", (STR)array->payload);
		return;
	}

	if (!isExpand) {
		switch (OBJ_GET_TYPE(array->obj)) {
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
			char buffer[40];

			printf("[ ");
			for (uint32_t i = 0; i < array->length;) {
				switch (OBJ_GET_TYPE(array->obj)) {
				case OBJ_ARRAY: //don't expand now ,or it's too slow and too much
					printValue(ARRAY_ELEMENT(array, Value, i));
					break;
				case OBJ_ARRAY_F64: {
					convert_adaptive_double(ARRAY_ELEMENT(array, double, i), buffer, sizeof(buffer));
					printf("%s", buffer);
					break;
				}
				case OBJ_ARRAY_F32: {
					convert_adaptive_double(ARRAY_ELEMENT(array, float, i), buffer, sizeof(buffer));
					printf("%s", buffer);
					break;
				}
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
	case OBJ_BOUND_METHOD:
		printFunction(AS_BOUND_METHOD(value)->method->function);
		break;
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
	default: {
		if (isIndexableArray(value)) {
			ObjArray* array = AS_ARRAY(value);
			printArrayLike(array, isExpand);
		}
		break;
	}
	}
}

StringEntry* getStringEntryInPool(ObjString* string)
{
	return tableGetStringEntry(&vm.strings, string);
}

NumberEntry* getNumberEntryInPool(Value* value)
{
	return tableGetNumberEntry(&vm.numbers, value);
}