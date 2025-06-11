#include <crescent/types.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>

#ifdef CONFIG_UBSAN

struct source {
	const char* file_name;
	u32 line;
	u32 column;
};

struct type_desc {
	u16 kind;
	u16 info;
	char name[];
};

struct type_mismatch_info {
	struct source source;
	struct type_desc* type;
	u8 log_alignment;
	u8 type_check_kind;
};

struct out_of_bounds_info {
	struct source source;
	const struct type_desc* array_type;
	const struct type_desc* index_type;
};

struct non_null_return_info {
	struct source attr;
};

struct pointer_overflow_info {
	struct source source;
};

struct invalid_value_info {
	struct source source;
	const struct type_desc* type;
};

struct unreachable_info {
	struct source source;
};

struct non_null_arg_info {
	struct source source;
	struct source attr_source;
	int arg_index;
};

struct shift_out_of_bounds_info {
	struct source source;
	const struct type_desc* lhs_type;
	const struct type_desc* rhs_type;
};

struct overflow_info {
	struct source source;
	const struct type_desc* type;
};

struct vla_bound_info {
	struct source source;
	const struct type_desc* type;
};

struct invalid_builtin_info {
	struct source source;
	u8 kind;
};

struct function_type_mismatch_info {
	struct source source;
	const struct type_desc* type;
};

enum type_check_kinds {
	LOAD,
	STORE,
	REFERENCE_BINDING,
	MEMBER_ACCESS,
	MEMBER_CALL,
	CONSTRUCTOR_CALL,
	DOWNCAST_POINTER,
	DOWNCAST_REFERENCE,
	UPCAST,
	UPCAST_TO_VIRTUAL_BASE,
	NONNULL_ASSIGN,
	DYNAMIC_OPERATION,
};

enum error_types {
	NULL_POINTER_USE_WITH_NULLABILITY,
	NULL_POINTER_USE,
	MISALIGNED_POINTER_USE,
	INSUFFICIENT_OBJECT_SIZE,
};

_Noreturn static void ubsan_panic(void) {
	panic("ubsan triggered crash");
}

static const char* const type_check_kinds[] = {
	"load of",
	"store to",
	"reference binding to",
	"member access within",
	"member call on",
	"constructor call on",
	"downcast of",
	"downcast of"
};

void __ubsan_handle_type_mismatch_v1(struct type_mismatch_info* mismatch, const void* ptr);
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_info* mismatch, const void* ptr) {
	size_t align = 1 << mismatch->log_alignment;
	int err_type;
	if (ptr == 0)
		err_type = mismatch->type_check_kind == NONNULL_ASSIGN ? NULL_POINTER_USE_WITH_NULLABILITY : NULL_POINTER_USE;
	else
		err_type = (uintptr_t)ptr & (align - 1) ? MISALIGNED_POINTER_USE : INSUFFICIENT_OBJECT_SIZE;

	switch (err_type) {
	case NULL_POINTER_USE:
	case NULL_POINTER_USE_WITH_NULLABILITY:
		printk(PRINTK_CRIT "ubsan: %s:%u: %s NULL pointer of type %s\n",
				mismatch->source.file_name, mismatch->source.line, 
				type_check_kinds[mismatch->type_check_kind], mismatch->type->name);
		break;
	case MISALIGNED_POINTER_USE:
		printk(PRINTK_CRIT "ubsan: %s:%u: misaligned address %p for type %s\n", 
				mismatch->source.file_name, mismatch->source.line, ptr, mismatch->type->name);
		break;
	case INSUFFICIENT_OBJECT_SIZE:
		printk(PRINTK_CRIT "ubsan: %s:%u: %s address %p with insufficient space for an object of type %s\n",
				mismatch->source.file_name, mismatch->source.line, 
				type_check_kinds[mismatch->type_check_kind], ptr, mismatch->type->name);
		break;
	}

	ubsan_panic();
}

void __ubsan_handle_out_of_bounds(struct out_of_bounds_info* oob, size_t index);
void __ubsan_handle_out_of_bounds(struct out_of_bounds_info* oob, size_t index) {
	printk(PRINTK_CRIT "ubsan: %s:%u: index %zu out of bounds for type %s\n", 
			oob->source.file_name, oob->source.line, index, oob->array_type->name);
	ubsan_panic();
}

void __ubsan_handle_nonnull_return_v1(struct non_null_return_info* nonnull, struct source* source);
void __ubsan_handle_nonnull_return_v1(struct non_null_return_info* nonnull, struct source* source) {
	(void)nonnull;
	printk(PRINTK_CRIT "ubsan: %s:%u: NULL pointer returned from function that should never return NULL\n",
			source->file_name, source->line);
	ubsan_panic();
}

void __ubsan_handle_pointer_overflow(struct pointer_overflow_info* overflow, const void* base, const void* result);
void __ubsan_handle_pointer_overflow(struct pointer_overflow_info* overflow, const void* base, const void* result) {
	printk(PRINTK_CRIT "ubsan: %s:%u: Pointer %p overflowed to %p\n", 
			overflow->source.file_name, overflow->source.line, base, result);
	ubsan_panic();
}

void __ubsan_handle_load_invalid_value(struct invalid_value_info* invalid, void* from);
void __ubsan_handle_load_invalid_value(struct invalid_value_info* invalid, void* from) {
	(void)from;
	printk(PRINTK_CRIT "ubsan: %s:%u: Loaded invalid value for type %s\n", 
			invalid->source.file_name, invalid->source.line, invalid->type->name);
	ubsan_panic();
}

void __ubsan_handle_builtin_unreachable(struct unreachable_info* unreachable);
void __ubsan_handle_builtin_unreachable(struct unreachable_info* unreachable) {
	printk(PRINTK_CRIT "ubsan: %s:%u: Reached an unreachable block\n",
			unreachable->source.file_name, unreachable->source.line);
	ubsan_panic();
}

void __ubsan_handle_nonnull_arg(struct non_null_arg_info* nonnull);
void __ubsan_handle_nonnull_arg(struct non_null_arg_info* nonnull) {
	printk(PRINTK_CRIT "ubsan: %s:%u: nonull argument at arg index %u is NULL\n",
			nonnull->source.file_name, nonnull->source.line, nonnull->arg_index);
	ubsan_panic();
}

void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_info* oob, const void* lhs, const void* rhs);
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_info* oob, const void* lhs, const void* rhs) {
	(void)lhs;
	(void)rhs;
	printk(PRINTK_CRIT "ubsan: %s:%u: shift index >= type width\n",
			oob->source.file_name, oob->source.line);
	ubsan_panic();
}

void __ubsan_handle_divrem_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs);
void __ubsan_handle_divrem_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs) {
	(void)lhs;
	printk(PRINTK_CRIT "ubsan: %s:%u: had %s\n",
			overflow->source.file_name, overflow->source.line,
			(rhs != 0) ? "divrem overflow" : "division by zero");
	ubsan_panic();
}

void __ubsan_handle_vla_bound_not_positive(struct vla_bound_info* info, size_t bound);
void __ubsan_handle_vla_bound_not_positive(struct vla_bound_info* info, size_t bound) {
	(void)bound;
	printk(PRINTK_CRIT "ubsan: %s:%u: vla has a non-positive size\n", 
			info->source.file_name, info->source.line);
	ubsan_panic();
}

void __ubsan_handle_invalid_builtin(struct invalid_builtin_info* info);
void __ubsan_handle_invalid_builtin(struct invalid_builtin_info* info) {
	(void)info;
	if (info->kind == 2) {
		printk(PRINTK_CRIT "ubsan: %s:%u: assumption violated during execution\n",
				info->source.file_name, info->source.line);
	} else {
		printk(PRINTK_CRIT "ubsan: %s:%u: Passing zero to __builtin_%s() which is not valid\n",
				info->source.file_name, info->source.line, info->kind == 1 ? "clz" : "ctz");
	}
	ubsan_panic();
}

void __ubsan_handle_function_type_mismatch(struct function_type_mismatch_info* info, const void* ptr);
void __ubsan_handle_function_type_mismatch(struct function_type_mismatch_info* info, const void* ptr) {
	printk(PRINTK_CRIT "ubsan: %s:%u: call to function %p through pointer to incorrect function type %s\n",
			info->source.file_name, info->source.line, ptr, info->type->name);
	ubsan_panic();
}

#endif /* CONFIG_UBSAN */
