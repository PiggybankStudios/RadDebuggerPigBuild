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
		{ .name="raddbg",             .srcPath="[ROOT]/raddbg/raddbg_main.c",              },
		{ .name="radlink",            .srcPath="[ROOT]/linker/lnk.c",                      },
		{ .name="radbin",             .srcPath="[ROOT]/radbin/radbin_main.c",              },
		{ .name="raddump",            .srcPath="[ROOT]/raddump/raddump_main.c",            },
		{ .name="ryan_scratch",       .srcPath="[ROOT]/scratch/ryan_scratch.c",            },
		{ .name="textperf",           .srcPath="[ROOT]/scratch/textperf.c",                },
		{ .name="convertperf",        .srcPath="[ROOT]/scratch/convertperf.c",             },
		{ .name="debugstringperf",    .srcPath="[ROOT]/scratch/debugstringperf.c",         },
		{ .name="parse_inline_sites", .srcPath="[ROOT]/scratch/parse_inline_sites.c",      },
		{ .name="strip_lib_debug",    .srcPath="[ROOT]/strip_lib_debug/strip_lib_debug.c", },
		{ .name="mule_main",          .srcPath="[ROOT]/src/mule/mule_inline.cpp",          .isCpp=true, },
		{ .name="mule_module",        .srcPath="[ROOT]/src/mule/mule_module.cpp",          .isCpp=true, .isDll=true },
		{ .name="mule_hotload",       .srcPath="[ROOT]/src/mule/mule_hotload_main.c",      .isDll=true },
		{ .name="torture",            .srcPath="[ROOT]/src/torture/torture_main.c",        },
		{ .name="dwarf_expr_test",    .srcPath="[ROOT]/src/torture/dwarf_expr_test.c",     },
		{ .name="mule_peb_trample",   .srcPath="[ROOT]/src/mule/mule_peb_trample.c",       },
	};
	Array_Targets result = EMPTY;
	AddValuesArray_Targets(&result, ArrayCount(defs), &defs[0]);
	return result;
}