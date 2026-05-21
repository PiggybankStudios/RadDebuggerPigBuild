/*
File:   build_script.c
Author: Taylor Robbins
Date:   05\20\2026
Description: 
	** None
*/

#define PIG_BUILD_PRINT_SYS_CMDS 1
#include "pig_build.h"

#define SRC_FOLDER   "[ROOT]/src"
#define LOCAL_FOLDER "[ROOT]/local"

#if BUILDING_ON_WINDOWS
static bool isMsvcInitialized = false;
#endif

typedef struct BuildOptions BuildOptions;
struct BuildOptions
{
	bool releaseBuild;
	Str compiler;
	bool telemetry;
	bool spall;
	bool asan;
	bool ubsan;
	bool opengl;
	bool useDwarfFormat; //else use codeview (only affects Clang)
	bool pgo;
	struct
	{
		bool raddbg;
		bool radlink;
		bool radbin;
		bool raddump;
		bool ryan_scratch;
		bool textperf;
		bool convertperf;
		bool debugstringperf;
		bool parse_inline_sites;
		bool strip_lib_debug;
		bool mule_main;
		bool mule_module;
		bool mule_hotload;
		bool torture;
		bool dwarf_expr_test;
		bool mule_peb_trample;
	} targets;
};
static BuildOptions options = EMPTY;
static CliArgs commonCompilerFlags = EMPTY;
static CliArgs commonLinkerFlags = EMPTY;

void HandleCmdLineArgs(StrArray* cmdLineArgs);
void FillCompilerAndLinkerArgs();

int main(int argc, const char* argv[])
{
	RecompileIfNeeded(nullptr);
	IF_WINDOWS(isMsvcInitialized = WasMsvcDevBatchRun());
	
	StrArray cmdLineArgs = EMPTY;
	for (u64 aIndex = 1; aIndex < argc; aIndex++) { AddStrNt(&cmdLineArgs, argv[aIndex]); }
	AddStrNt(&cmdLineArgs, "debug");
	// AddStrNt(&cmdLineArgs, "release");
	AddStrNt(&cmdLineArgs, "clang");
	// AddStrNt(&cmdLineArgs, "msvc");
	AddStrNt(&cmdLineArgs, "raddbg");
	HandleCmdLineArgs(&cmdLineArgs);
	
	FillCompilerAndLinkerArgs();
	
	Str localFolderResolved = ResolveRootTo(StrLit(LOCAL_FOLDER), StrLit(".."));
	if (!DoesFolderExist(localFolderResolved)) { mkdir(localFolderResolved.chars, FOLDER_PERMISSIONS); }
	
	StrArray commonTags = EMPTY;
	IF_WINDOWS(AddTag(&commonTags, T_WINDOWS));
	IF_LINUX(AddTag(&commonTags, T_LINUX));
	IF_OSX(AddTag(&commonTags, T_OSX));
	if (StrAnyCaseEquals(options.compiler, StrLit(EXE_MSVC_CL))) { AddTag(&commonTags, T_MSVC_CL_OR_LINK); }
	else { AddTag(&commonTags, T_CLANG); }
	
	// +==============================+
	// | Process logo.rc -> logo.res  |
	// +==============================+
	#if BUILDING_ON_WINDOWS
	if (!DoesFileExist(StrLit("logo.res")))
	{
		CliArgs rcArgs = EMPTY;
		AddArg(&rcArgs, RC_NO_LOGO);
		AddArgNt(&rcArgs, RC_OUTPUT_FILE, "logo.res");
		AddArgNt(&rcArgs, CLI_QUOTED_ARG, "[ROOT]/data/logo.rc");
		InitializeMsvcIf(StrLit(PIG_BUILD_ROOT), &isMsvcInitialized);
		RunCliProgramAndExitOnFailure(StrLit(EXE_MSVC_RC), T_MSVC_RC T_WINDOWS, &rcArgs, StrLit("Failed to compile logo.rc into logo.res!"));
		AssertFileExist(StrLit("logo.res"), true);
	}
	#endif
	
	// +==============================+
	// |       Build raddbg.exe       |
	// +==============================+
	Str raddbgExePath = StrLit("raddbg" EXE_EXT);
	if (options.targets.raddbg)
	{
		bool usingMsvcCompiler = StrAnyCaseEquals(options.compiler, StrLit(EXE_MSVC_CL));
		Str raddbgObjPath = ChangePathExtension(raddbgExePath, StrLit(OBJ_EXT), true);
		Str compilerExe = usingMsvcCompiler ? StrLit(EXE_MSVC_CL) : StrLit(EXE_CLANG);
		Str linkerExe = usingMsvcCompiler ? StrLit(EXE_MSVC_LINK) : StrLit(EXE_CLANG);
		
		//Compile
		{
			CliArgs compilerArgs = EMPTY;
			AddArgList(&compilerArgs, &commonCompilerFlags);
			AddArgNt(&compilerArgs, CLI_QUOTED_ARG, SRC_FOLDER "/raddbg/raddbg_main.c");
			AddTaggedArgNt(&compilerArgs, T_MSVC_CL, CL_OBJ_FILE,       "raddbg" OBJ_EXT);
			AddTaggedArgNt(&compilerArgs, T_CLANG,   CLANG_OUTPUT_FILE, "raddbg" OBJ_EXT);
			
			StrArray compilerTags = EMPTY;
			AddStrLit(&compilerTags, "compiling");
			AddTag(&compilerTags, usingMsvcCompiler ? T_MSVC_CL : T_CLANG);
			AddStrArray(&compilerTags, &commonTags);
			
			InitializeMsvcIf(StrLit(PIG_BUILD_ROOT), &isMsvcInitialized);
			RunCliProgramTagArrayAndExitOnFailure(compilerExe, &compilerTags, &compilerArgs, FormatStr("Failed to compile %.*s!", StrPrint(raddbgObjPath)));
			AssertFileExist(raddbgObjPath, true);
		}
		
		//Link
		{
			CliArgs linkerArgs = EMPTY;
			AddArgList(&linkerArgs, &commonLinkerFlags);
			AddArgNt(&linkerArgs, CLI_QUOTED_ARG, "raddbg" OBJ_EXT);
			IF_WINDOWS(AddArgNt(&linkerArgs, CLI_QUOTED_ARG, "logo.res"));
			AddTaggedArgNt(&linkerArgs, T_MSVC_LINK, LINK_OUTPUT_FILE,  "raddbg" EXE_EXT);
			AddTaggedArgNt(&linkerArgs, T_CLANG,     CLANG_OUTPUT_FILE, "raddbg" EXE_EXT);
			// AddArgList(&linkerArgs, &commonCompilerFlags); //TODO: Do we need to pass any of these to the linker?
			
			StrArray linkerTags = EMPTY;
			AddStrLit(&linkerTags, "linking");
			AddTag(&linkerTags, usingMsvcCompiler ? T_MSVC_LINK : T_CLANG);
			AddStrArray(&linkerTags, &commonTags);
			
			RunCliProgramTagArrayAndExitOnFailure(linkerExe, &linkerTags, &linkerArgs, FormatStr("Failed to link %.*s!", StrPrint(raddbgExePath)));
			AssertFileExist(raddbgExePath, true);
		}
	}
}

// +--------------------------------------------------------------+
// |                    Command-Line Arguments                    |
// +--------------------------------------------------------------+
void HandleCmdLineArgs(StrArray* cmdLineArgs)
{
	memset(&options, 0x00, sizeof(options));
	options.compiler = BUILDING_ON_WINDOWS ? StrLit(EXE_MSVC_CL) : StrLit(EXE_CLANG);
	
	Array_bool handledArgs = EMPTY;
	bool compilerSpecified = false;
	bool buildModeSpecified = false;
	bool targetSpecified = false;
	for (int aIndex = 0; aIndex < cmdLineArgs->length; aIndex++)
	{
		Str argStr = cmdLineArgs->strings[aIndex];
		bool handledArg = true;
		if (!IsEmptyStr(argStr))
		{
			if      (StrAnyCaseEquals(argStr, StrLit("msvc")))               { options.compiler       = StrLit(EXE_MSVC_CL); AssertMsg(compilerSpecified  == false, "Conflicting compiler args!");   compilerSpecified  = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("clang")))              { options.compiler       = StrLit(EXE_CLANG);   AssertMsg(compilerSpecified  == false, "Conflicting compiler args!");   compilerSpecified  = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("release")))            { options.releaseBuild   = true;                AssertMsg(buildModeSpecified == false, "Conflicting build-mode args!"); buildModeSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("debug")))              { options.releaseBuild   = false;               AssertMsg(buildModeSpecified == false, "Conflicting build-mode args!"); buildModeSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("telemetry")))          { options.telemetry      = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("spall")))              { options.spall          = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("asan")))               { options.asan           = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("ubsan")))              { options.ubsan          = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("opengl")))             { options.opengl         = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("dwarf")))              { options.useDwarfFormat = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("pgo")))                { options.pgo            = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("raddbg")))             { options.targets.raddbg             = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("radlink")))            { options.targets.radlink            = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("radbin")))             { options.targets.radbin             = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("raddump")))            { options.targets.raddump            = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("ryan_scratch")))       { options.targets.ryan_scratch       = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("textperf")))           { options.targets.textperf           = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("convertperf")))        { options.targets.convertperf        = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("debugstringperf")))    { options.targets.debugstringperf    = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("parse_inline_sites"))) { options.targets.parse_inline_sites = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("strip_lib_debug")))    { options.targets.strip_lib_debug    = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("mule_main")))          { options.targets.mule_main          = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("mule_module")))        { options.targets.mule_module        = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("mule_hotload")))       { options.targets.mule_hotload       = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("torture")))            { options.targets.torture            = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("dwarf_expr_test")))    { options.targets.dwarf_expr_test    = true; targetSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("mule_peb_trample")))   { options.targets.mule_peb_trample   = true; targetSpecified = true; }
			else
			{
				PrintLine_E("Unknown argument[%d]: \"%.*s\"", aIndex, StrPrint(argStr));
				handledArg = false;
			}
		}
		AddValueArray_bool(&handledArgs, handledArg);
	}
	
	bool isMsvcCompiler = StrAnyCaseEquals(options.compiler, StrLit(EXE_MSVC_CL));
	bool isClangCompiler = StrAnyCaseEquals(options.compiler, StrLit(EXE_CLANG));
	AssertMsg(isClangCompiler || !options.useDwarfFormat, "DWARF is only supported with Clang compiler!");
	AssertMsg(BUILDING_ON_WINDOWS || !isMsvcCompiler, "MSVC compiler is only available on WINDOWS!");
	if (!compilerSpecified) { PrintLine("[default compiler `%.*s`]", StrPrint(options.compiler)); }
	else { PrintLine("[%s compile]", isClangCompiler ? "clang" : "msvc"); }
	if (!buildModeSpecified) { PrintLine("[default mode `%s`]", options.releaseBuild ? "release" : "debug"); }
	else { PrintLine("[%s mode]", options.releaseBuild ? "release" : "debug"); }
	if (!targetSpecified) { WriteLine("[default target `raddbg`]"); options.targets.raddbg = true; }
}

// +--------------------------------------------------------------+
// |                         Common Args                          |
// +--------------------------------------------------------------+
void FillCompilerAndLinkerArgs()
{
	AddTaggedArg(&commonCompilerFlags,   T_MSVC_CL, CL_COMPILE);
	AddTaggedArg(&commonCompilerFlags,   T_CLANG,   CLANG_COMPILE);
	AddTaggedArg(&commonCompilerFlags,   T_MSVC_CL, CL_NO_LOGO);
	AddTaggedArg(&commonCompilerFlags,   T_MSVC_CL, CL_FULL_FILE_PATHS);
	AddTaggedArg(&commonCompilerFlags,   T_CLANG,   CLANG_FULL_FILE_PATHS);
	AddTaggedArg(&commonCompilerFlags,   T_MSVC_CL, CL_DEBUG_INFO_IN_OBJ);
	AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_ENABLE_LANG_CONFORMANCE_OPTION, "preprocessor");
	AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_INCLUDE_DIR,    SRC_FOLDER);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_INCLUDE_DIR, SRC_FOLDER);
	AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_INCLUDE_DIR,    LOCAL_FOLDER);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_INCLUDE_DIR, LOCAL_FOLDER);
	AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_OPTIMIZATION_LEVEL, options.releaseBuild ? "2" : "d");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_OPTIMIZATION_LEVEL, options.releaseBuild ? "2" : "0");
	AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_DEFINE,    options.releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_DEFINE, options.releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_DEFINE, options.releaseBuild ? "NDEBUG" : "_DEBUG");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "_USE_MATH_DEFINES");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "strdup=_strdup");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "_printf=printf");
	AddTaggedArg(&commonCompilerFlags,   T_CLANG,   "-Xclang -flto-visibility-public-std"); //TODO: What does this do?
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   "-ferror-limit=[VAL]", "10000"); //TODO: What does this do?
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_M_FLAG, "cx16"); //TODO: What does this do?
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,   CLANG_M_FLAG, "sha"); //TODO: What does this do?
	
	if (options.releaseBuild)
	{
		AddTaggedArgNt(&commonCompilerFlags, T_MSVC_CL, CL_OPTIMIZATION_LEVEL, "b1"); //Allows expansion only of functions marked "inline", "__inline", or "__forceinline"
	}
	else
	{
		// AddTaggedArg(&commonCompilerFlags, T_MSVC_CL, CL_DEBUG_INFO);
		AddTaggedArg(&commonCompilerFlags, T_CLANG, CLANG_DEBUG_INFO_DEFAULT);
		AddTaggedArgNt(&commonCompilerFlags, T_CLANG, CLANG_DEBUG_INFO, options.useDwarfFormat ? "dwarf" : "codeview");
	}
	
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_WARNING_LEVEL, "all");
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNKNOWN_WARNING_OPTION);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_BRACES);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_FUNCTION);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_PARAMETER);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VALUE);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VARIABLE);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_LOCAL_TYPEDEF);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_BUT_SET_VARIABLE);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_WRITABLE_STRINGS);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_FIELD_INITIALIZERS);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_REGISTER);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_DECLARATIONS);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_SINGLE_BIT_BITFIELD_CONVERSION);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_COMPARE_DISTINCT_POINTER_TYPES);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_INITIALIZER_OVERRIDES);
	AddTaggedArgNt(&commonCompilerFlags, T_CLANG,     CLANG_DISABLE_WARNING, CLANG_WARNING_INCOMP_PNTR_DISCARDS_QUALIFIERS);
	
	AddTaggedArg(&commonLinkerFlags,   T_MSVC_LINK, LINK_NO_LOGO);
	AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-fuse-ld=[VAL]", "lld"); //TODO: What does this do?
	AddTaggedArgNt(&commonLinkerFlags, T_MSVC_LINK,             "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-Xlinker " "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArg(&commonLinkerFlags,   T_MSVC_LINK, LINK_DISABLE_INCREMENTAL);
	// AddTaggedArgNt(&commonLinkerFlags, T_MSVC_LINK,             "/pdbaltpath:[VAL]", ""); //TODO: What should this path be?
	// AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-Xlinker " "/pdbaltpath:[VAL]", ""); //TODO: What should this path be?
	AddTaggedArgNt(&commonLinkerFlags, T_MSVC_LINK,             LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-Xlinker " LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArg(&commonLinkerFlags,   T_MSVC_LINK, LINK_NO_EXP); //TODO: What does this do?
	AddTaggedArg(&commonLinkerFlags,   T_MSVC_LINK, LINK_NO_COFF_GRP_INFO); //TODO: What does this do?
	AddTaggedArgNt(&commonLinkerFlags, T_MSVC_LINK,             LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-Xlinker " LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(&commonLinkerFlags, T_MSVC_LINK,             LINK_OPT, "icf"); //TODO: What does this do?
	AddTaggedArgNt(&commonLinkerFlags, T_CLANG,     "-Xlinker " LINK_OPT, "noicf"); //TODO: What does this do?
}
