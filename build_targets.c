/*
File:   build_targets.c
Author: Taylor Robbins
Date:   05\24\2026
Description: 
	** None
*/

typedef struct TargetDefinition TargetDefinition;
struct TargetDefinition
{
	const char* name;
	const char* srcPath;
	bool isCpp;
	bool isDll;
};

TYPED_ARRAY(Array_Targets, TargetDefinition, targets);

Array_Targets GetTargetDefinitions()
{
	TargetDefinition defs[] = {
		{ .name="raddbg",             .srcPath="[ROOT]/src/raddbg/raddbg_main.c",              },
		{ .name="radlink",            .srcPath="[ROOT]/src/linker/lnk.c",                      },
		{ .name="radbin",             .srcPath="[ROOT]/src/radbin/radbin_main.c",              },
		{ .name="debugstringperf",    .srcPath="[ROOT]/src/scratch/debugstringperf.c",         },
		{ .name="torture",            .srcPath="[ROOT]/src/torture/torture_main.c",            },
		// { .name="raddump",            .srcPath="[ROOT]/src/raddump/raddump_main.c",            }, //TODO:  Cannot open include file: 'third_party/md5/md5.c'
		// { .name="ryan_scratch",       .srcPath="[ROOT]/src/scratch/ryan_scratch.c",            }, //TODO: error C2198: 'DI_SearchItemArray di_search_item_array_from_target_query(Access *,RDI_SectionKind,String8,U64,B32 *)': too few arguments for call
		// { .name="textperf",           .srcPath="[ROOT]/src/scratch/textperf.c",                }, //TODO: error C2146: syntax error: missing ')' before identifier 'window'
		// { .name="convertperf",        .srcPath="[ROOT]/src/scratch/convertperf.c",             }, //TODO: error C1083: Cannot open source file: '..\\src\\scratch\\convertperf.c'
		// { .name="parse_inline_sites", .srcPath="[ROOT]/src/scratch/parse_inline_sites.c",      }, //TODO: error C1083: Cannot open include file: 'os/os_inc.h'
		// { .name="strip_lib_debug",    .srcPath="[ROOT]/src/strip_lib_debug/strip_lib_debug.c", }, //TODO: error C2440: 'initializing': cannot convert from 'int' to 'String8'
		// { .name="mule_main",          .srcPath="[ROOT]/src/mule/mule_inline.cpp",              .isCpp=true, }, //TODO: Multiple source files
		// { .name="mule_module",        .srcPath="[ROOT]/src/mule/mule_module.cpp",              .isCpp=true, .isDll=true }, //TODO: Not compiling with clang yet
		// { .name="mule_hotload",       .srcPath="[ROOT]/src/mule/mule_hotload_main.c",          .isDll=true }, //TODO: .exe AND .dll created from one target
		// { .name="dwarf_expr_test",    .srcPath="[ROOT]/src/torture/dwarf_expr_test.c",         }, //TODO: error C1083: Cannot open source file: '..\\src\\torture\\dwarf_expr_test.c'
		// { .name="mule_peb_trample",   .srcPath="[ROOT]/src/mule/mule_peb_trample.c",           }, //TODO: Some sort of file shuffling is going on
	};
	Array_Targets result = EMPTY;
	AddValuesArray_Targets(&result, ArrayCount(defs), &defs[0]);
	return result;
}