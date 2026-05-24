/*
File:   build_script.c
Author: Taylor Robbins
Date:   05\20\2026
*/

#define PIG_BUILD_PRINT_SYS_CMDS 1
#define SRC_FOLDER   "[ROOT]/src"
#define LOCAL_FOLDER "[ROOT]/local"
#define DEFAULT_CMD_LINE_ARGS "debug clang raddbg pgo" // debug, release, msvc, clang, raddbg, radlink, radbin, debugstringperf, mule_module, mule_hotload, torture, telemetry, spall, asan, ubsan, opengl, dwarf, pgo

//TODO: We need to do some gymnastics here to get metagen to compile with us since it's
//      written to be a standalone .exe with it's own main entry point. We also conflict
//      on a few names like ArrayCount and FileIter. All of this is "solvable" with these
//      #defines and #undefs but it's quite ugly. Ideally the metagen logic would be written
//      to be included in the PigBuild script, and could use whatever it wants from PigBuild
//      so it wouldn't need to pull in conflicting names from the main codebase
#define FileIter RaddbgFileIter
#define main metagen_main
#define wWinMain metagen_wWinMain
#define wmain metagen_wmain

#include "metagen/metagen_main.c"

#undef wmain
#undef wWinMain
#undef main
#undef FileIter
#undef ArrayCount
#undef Assert

#include "pig_build.h"
#if BUILDING_ON_WINDOWS
#include <shellapi.h>
#endif

#include "build_targets.c"

typedef struct ProgramParams ProgramParams;
struct ProgramParams
{
	StrArray args;
	
	int argc;
	const char** argv;
	IF_WINDOWS(WCHAR **argvWide);
	
	#if (BUILDING_ON_WINDOWS && !BUILD_CONSOLE_INTERFACE)
	HINSTANCE hInstance;
	HINSTANCE hPrevInstance;
	LPWSTR lpCmdLine;
	int nShowCmd;
	#endif
};

typedef struct BuildOptions BuildOptions;
struct BuildOptions
{
	bool releaseBuild;
	bool useCompilerClang;
	bool useCompilerMsvc;
	bool telemetry;
	bool spall;
	bool asan;
	bool ubsan;
	bool opengl;
	bool useDwarfFormat; //else use codeview (only affects Clang)
	bool pgo;
	StrArray requestedTargets;
};

void HandleCmdLineArgs(StrArray* cmdLineArgs, Array_Targets* targets, BuildOptions* options);
void FillCompilerAndLinkerFlags(BuildOptions* options, CliArgs* commonCompilerFlags, CliArgs* commonLinkerFlags);

// +--------------------------------------------------------------+
// |                             Main                             |
// +--------------------------------------------------------------+
int build_main(ProgramParams* params)
{
	StrArray buildScriptSourceFolders = MakeStrArrayBySplittingLit(false, " | ", "../src/metagen | ../pig_build/src | ../build_targets.c");
	RecompileIfNeeded(buildScriptSourceFolders);
	IF_WINDOWS(bool isMsvcInitialized = WasMsvcDevBatchRun());
	
	if (params->args.length == 0) { SplitStrIntoArrayLit(&params->args, false, " ", DEFAULT_CMD_LINE_ARGS); }
	
	Array_Targets targets = GetTargetDefinitions();
	
	BuildOptions options = EMPTY;
	HandleCmdLineArgs(&params->args, &targets, &options);
	
	CliArgs commonCompilerFlags = EMPTY;
	CliArgs commonLinkerFlags = EMPTY;
	FillCompilerAndLinkerFlags(&options, &commonCompilerFlags, &commonLinkerFlags);
	
	Str compilerExe = options.useCompilerMsvc ? StrLit(EXE_MSVC_CL)   : StrLit(EXE_CLANG);
	Str linkerExe   = options.useCompilerMsvc ? StrLit(EXE_MSVC_LINK) : StrLit(EXE_CLANG);
	
	Str localFolderResolved = ResolveRootTo(StrLit(LOCAL_FOLDER), StrLit(".."));
	if (!DoesFolderExist(localFolderResolved)) { mkdir(localFolderResolved.chars, FOLDER_PERMISSIONS); }
	
	StrArray commonTags = EMPTY;
	AddTag(&commonTags, options.useCompilerMsvc ? "cl" : "clang");
	if (options.useCompilerMsvc) { AddTag(&commonTags, "ClOrLink"); }
	IF_WINDOWS(AddTag(&commonTags, "Windows"));
	IF_LINUX(AddTag(&commonTags,   "Linux"));
	IF_OSX(AddTag(&commonTags,     "OSX"));
	
	// +==============================+
	// |         Run Metagen          |
	// +==============================+
	WriteLine("[running metagen]");
	#if (BUILDING_ON_WINDOWS && BUILD_CONSOLE_INTERFACE)
	int metagenReturnCode = metagen_wmain(params->argc, params->argvWide);
	#elif BUILDING_ON_WINDOWS
	int metagenReturnCode = metagen_wWinMain(params->hInstance, params->hPrevInstance, params->lpCmdLine, params->nShowCmd);
	#else
	int metagenReturnCode = metagen_main(params->argc, params->argv);
	#endif
	if (metagenReturnCode != 0)
	{
		PrintLine_E("metagen returned: %d", metagenReturnCode);
		return metagenReturnCode;
	}
	
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
		RunCliProgramAndExitOnFailureTagsLit(StrLit(EXE_MSVC_RC), "rc|Windows", &rcArgs, StrLit("Failed to compile logo.rc into logo.res!"));
		AssertFileExist(StrLit("logo.res"), true);
	}
	#endif
	
	// +==============================+
	// |        Build Targets         |
	// +==============================+
	for (u64 tIndex = 0; tIndex < targets.length; tIndex++)
	{
		TargetDefinition* def = &targets.targets[tIndex];
		Str targetName = MakeStrNt(def->name);
		Str srcPath = MakeStrNt(def->srcPath);
		Str srcFileName = GetFileNamePart(srcPath, true);
		bool isTargetRequested = ContainsStr(&options.requestedTargets, targetName, /*ignoreCase=*/true);
		
		if (isTargetRequested)
		{
			Str objPath = JoinStrings2(targetName, StrLit(OBJ_EXT));
			Str exePath = JoinStrings2(targetName, StrLit(EXE_EXT));
			Str dllPath = JoinStrings2(targetName, StrLit(DLL_EXT));
			Str libPath = JoinStrings2(targetName, StrLit(LIB_EXT));
			Str outputPath = (def->isDll ? dllPath : exePath);
			PrintLine("[Building %.*s into %.*s...]", StrPrint(srcFileName), StrPrint(outputPath));
			
			CliArgs args = EMPTY;
			AddArgList(&args, &commonCompilerFlags);
			AddArgStr(&args, CLI_QUOTED_ARG, srcPath);
			if (!def->isDll) { AddTaggedArgStr(&args, "cl", CL_OBJ_FILE, objPath); }
			
			AddTaggedArg(&args, "cl", CL_LINK);
			AddArgList(&args, &commonLinkerFlags);
			AddTaggedArgStr(&args, "cl", LINK_OUTPUT_FILE,  outputPath);
			AddTaggedArgStr(&args, "clang",   CLANG_OUTPUT_FILE, outputPath);
			
			StrArray tags = EMPTY;
			AddStrArray(&tags, &commonTags);
			AddStr(&tags, targetName);
			if (def->isDll) { AddStrNt(&tags, "dll"); }
			AddTag(&tags, def->isCpp ? "LangCpp" : "LangC");
			
			if (options.useCompilerMsvc) { InitializeMsvcIf(StrLit(PIG_BUILD_ROOT), &isMsvcInitialized); }
			RunCliProgramAndExitOnFailureTags(compilerExe, tags, &args, FormatStr("Failed to compile %.*s!", StrPrint(outputPath)));
			AssertFileExist(outputPath, true);
			
			PrintLine("[Successfully built %.*s!]", StrPrint(exePath));
		}
	}
	
	return 0;
}

// +--------------------------------------------------------------+
// |                    Command-Line Arguments                    |
// +--------------------------------------------------------------+
void HandleCmdLineArgs(StrArray* cmdLineArgs, Array_Targets* targets, BuildOptions* options)
{
	memset(options, 0x00, sizeof(BuildOptions));
	
	bool compilerSpecified = false;
	bool buildModeSpecified = false;
	for (int aIndex = 0; aIndex < cmdLineArgs->length; aIndex++)
	{
		Str argStr = cmdLineArgs->strings[aIndex];
		if (!IsEmptyStr(argStr))
		{
			if      (StrAnyCaseEquals(argStr, StrLit("msvc")))               { options->useCompilerMsvc  =  true; AssertMsg(compilerSpecified  == false, "Conflicting compiler args!"); compilerSpecified  = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("clang")))              { options->useCompilerClang =  true; AssertMsg(compilerSpecified  == false, "Conflicting compiler args!"); compilerSpecified  = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("release")))            { options->releaseBuild     =  true; AssertMsg(buildModeSpecified == false, "Conflicting build-mode args!"); buildModeSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("debug")))              { options->releaseBuild     = false; AssertMsg(buildModeSpecified == false, "Conflicting build-mode args!"); buildModeSpecified = true; }
			else if (StrAnyCaseEquals(argStr, StrLit("telemetry")))          { options->telemetry        =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("spall")))              { options->spall            =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("asan")))               { options->asan             =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("ubsan")))              { options->ubsan            =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("opengl")))             { options->opengl           =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("dwarf")))              { options->useDwarfFormat   =  true; }
			else if (StrAnyCaseEquals(argStr, StrLit("pgo")))                { options->pgo              =  true; }
			else
			{
				bool targetRecognized = false;
				for (u64 tIndex = 0; tIndex < targets->length; tIndex++)
				{
					TargetDefinition* def = &targets->targets[tIndex];
					if (StrAnyCaseEquals(argStr, MakeStrNt(def->name))) { targetRecognized = true; break; }
				}
				if (!targetRecognized)
				{
					PrintLine_E("Unknown target or option: \"%.*s\"", StrPrint(argStr));
					continue;
				}
				
				AddStr(&options->requestedTargets, argStr);
			}
		}
	}
	
	if (options->requestedTargets.length == 0)
	{
		WriteLine_E("[WARNING] No valid build target specified; must use build target names as arguments to this script, like `builder raddbg` or `builder rdi_from_pdb`.");
		AddStrLit(&options->requestedTargets, "raddbg");
	}
	
	if (!compilerSpecified)
	{
		options->useCompilerMsvc = BUILDING_ON_WINDOWS;
		options->useCompilerClang = !BUILDING_ON_WINDOWS;
	}
	AssertMsg(BUILDING_ON_WINDOWS || !options->useCompilerMsvc, "MSVC compiler is only available on WINDOWS!");
	AssertMsg(options->useCompilerClang || !options->useDwarfFormat, "DWARF is only supported with Clang compiler!");
	PrintLine("[%scompiler `%s`]", (compilerSpecified ? "" : "default "), options->useCompilerClang ? "clang" : "msvc");
	
	if (!buildModeSpecified) { PrintLine("[default mode `%s`]", options->releaseBuild ? "release" : "debug"); }
	else { PrintLine("[%s mode]", options->releaseBuild ? "release" : "debug"); }
}

// +--------------------------------------------------------------+
// |                  Compiler and Linker Flags                   |
// +--------------------------------------------------------------+
void FillCompilerAndLinkerFlags(BuildOptions* options, CliArgs* commonCompilerFlags, CliArgs* commonLinkerFlags)
{
	AddTaggedArg(commonCompilerFlags,   "cl",    CL_NO_LOGO);
	AddTaggedArg(commonCompilerFlags,   "cl",    CL_FULL_FILE_PATHS);
	AddTaggedArg(commonCompilerFlags,   "clang", CLANG_FULL_FILE_PATHS);
	AddTaggedArg(commonCompilerFlags,   "cl",    CL_DEBUG_INFO_IN_OBJ);
	AddTaggedArgNt(commonCompilerFlags, "cl",    CL_ENABLE_LANG_CONFORMANCE_OPTION, "preprocessor");
	AddTaggedArgNt(commonCompilerFlags, "cl",    CL_INCLUDE_DIR,    SRC_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_INCLUDE_DIR, SRC_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, "cl",    CL_INCLUDE_DIR,    LOCAL_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_INCLUDE_DIR, LOCAL_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "_USE_MATH_DEFINES");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "strdup=_strdup");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "_printf=printf");
	AddTaggedArg(commonCompilerFlags,   "clang", "-Xclang -flto-visibility-public-std"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, "clang", "-ferror-limit=[VAL]", "10000"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_M_FLAG, "cx16"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_M_FLAG, "sha"); //TODO: What does this do?
	
	// Debug/Release Dependent Options
	AddTaggedArgNt(commonCompilerFlags, "cl",    CL_OPTIMIZATION_LEVEL, options->releaseBuild ? "2" : "d");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_OPTIMIZATION_LEVEL, options->releaseBuild ? "2" : "0");
	AddTaggedArgNt(commonCompilerFlags, "cl",    CL_DEFINE,    options->releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, options->releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, options->releaseBuild ? "NDEBUG" : "_DEBUG");
	if (options->releaseBuild)
	{
		AddTaggedArgNt(commonCompilerFlags, "cl", CL_OPTIMIZATION_LEVEL, "b1"); //Allows expansion only of functions marked "inline", "__inline", or "__forceinline"
	}
	else
	{
		// AddTaggedArg(commonCompilerFlags,   "cl", CL_DEBUG_INFO);
		AddTaggedArg(commonCompilerFlags,   "clang", CLANG_DEBUG_INFO_DEFAULT);
		AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEBUG_INFO, options->useDwarfFormat ? "dwarf" : "codeview");
	}
	
	// Warnings
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_WARNING_LEVEL, "all");
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNKNOWN_WARNING_OPTION);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_BRACES);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_FUNCTION);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_PARAMETER);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VALUE);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VARIABLE);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_LOCAL_TYPEDEF);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_BUT_SET_VARIABLE);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_WRITABLE_STRINGS);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_FIELD_INITIALIZERS);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_REGISTER);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_DECLARATIONS);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_SINGLE_BIT_BITFIELD_CONVERSION);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_COMPARE_DISTINCT_POINTER_TYPES);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_INITIALIZER_OVERRIDES);
	AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DISABLE_WARNING, CLANG_WARNING_INCOMP_PNTR_DISCARDS_QUALIFIERS);
	
	// Linker flags (for MSVC these have to come after the /link argument)
	AddTaggedArg(commonLinkerFlags,   "cl|dll",            LINK_BUILD_DLL);
	AddTaggedArg(commonLinkerFlags,   "clang|dll",         LINK_BUILD_DLL);
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-fuse-ld=[VAL]", "lld"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "Windows|dll=false", CLI_QUOTED_ARG, "logo.res");
	AddTaggedArgNt(commonLinkerFlags, "cl",                "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-Xlinker " "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArg(commonLinkerFlags,   "cl",                LINK_DISABLE_INCREMENTAL);
	AddTaggedArgNt(commonLinkerFlags, "cl",                "/pdbaltpath:[VAL]", "%_PDB%");
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-Xlinker " "/pdbaltpath:[VAL]", "%_PDB%");
	AddTaggedArgNt(commonLinkerFlags, "cl",                LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-Xlinker " LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArg(commonLinkerFlags,   "cl",                LINK_NO_EXP); //TODO: What does this do?
	AddTaggedArg(commonLinkerFlags,   "cl",                LINK_NO_COFF_GRP_INFO); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "cl",                LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-Xlinker " LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "cl",                LINK_OPT, "icf"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "clang",             "-Xlinker " LINK_OPT, "noicf"); //TODO: What does this do?
	
	//radlink extra options
	AddTaggedArg(commonLinkerFlags,   "cl|radlink", "/NOIMPLIB"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, "cl|radlink",                LINK_NATVIS_PATH, SRC_FOLDER "/linker/linker.natvis");
	AddTaggedArgNt(commonLinkerFlags, "clang|radlink", "-Xlinker " LINK_NATVIS_PATH, SRC_FOLDER "/linker/linker.natvis");
	
	if (options->telemetry)
	{
		WriteLine("[telemetry profiling enabled]");
		AddTaggedArgNt(commonCompilerFlags, "cl",    CL_DEFINE,    "PROFILE_TELEMETRY=1");
		AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "PROFILE_TELEMETRY=1");
	}
	if (options->spall)
	{
		WriteLine("[spall profiling enabled]");
		AddTaggedArgNt(commonCompilerFlags, "cl",    CL_DEFINE,    "PROFILE_SPALL=1");
		AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "PROFILE_SPALL=1");
	}
	if (options->asan)
	{
		if (options->useCompilerClang) { WriteLine("[asan enabled]"); }
		else { WriteLine("WARNING: asan option ignored for msvc compiler"); }
		AddTaggedArg(commonCompilerFlags, "clang", "-fsanitize=address"); //TODO: Add a #define for this
	}
	if (options->ubsan)
	{
		if (options->useCompilerClang) { WriteLine("[ubsan enabled]"); }
		else { WriteLine("WARNING: ubsan option ignored for msvc compiler"); }
		AddTaggedArg(commonCompilerFlags, "clang", "-fsanitize=undefined"); //TODO: Add a #define for this
	}
	if (options->opengl)
	{
		WriteLine("[opengl render backend]");
		AddTaggedArgNt(commonCompilerFlags, "cl",    CL_DEFINE,    "R_BACKEND=R_BACKEND_OPENGL");
		AddTaggedArgNt(commonCompilerFlags, "clang", CLANG_DEFINE, "R_BACKEND=R_BACKEND_OPENGL");
	}
	if (options->pgo)
	{
		if (options->useCompilerClang)
		{
			CliArgs whereArgs = EMPTY;
			AddArg(&whereArgs, "llvm-profdata");
			AddArg(&whereArgs, CLI_DISABLE_STDOUT_AND_STDERR);
			RunCliProgramAndExitOnFailure(StrLit("where"), &whereArgs, StrLit("llvm-profdata is not in the PATH"));
			
			Str profRawPath = StrLit("build.profraw");
			Str profDataPath = StrLit("build.profdata");
			if (DoesFileExist(profRawPath))
			{
				CliArgs profDataArgs = EMPTY;
				AddArg(&profDataArgs, "merge");
				AddArgStr(&profDataArgs, CLI_QUOTED_ARG, profRawPath);
				AddArgStr(&profDataArgs, "-output=\"[VAL]\"", profDataPath);
				RunCliProgramAndExitOnFailure(StrLit("llvm-profdata"), &profDataArgs, StrLit("llvm-profdata ran into a problem!"));
				AssertFileExist(profDataPath, true);
				
				AddTaggedArgStr(commonCompilerFlags, "clang", "-fprofile-use=\"[VAL]\"", profDataPath);
			}
			else
			{
				WriteLine("[pgo enabled]");
				//TODO: This doesn't seem to be generating the build.profraw file that I expect
				AddTaggedArg(commonCompilerFlags,   "clang", "-fprofile-generate");
				AddTaggedArg(commonCompilerFlags,   "clang", "-mllvm");
				AddTaggedArgNt(commonCompilerFlags, "clang", "-vp-counters-per-site=[VAL]", "5");
			}
		}
		else
		{
			WriteLine_E("ERROR: PGO build is not supported with current compiler");
			exit(1);
		}
	}
}

// +--------------------------------------------------------------+
// |                       Real Entry-Point                       |
// +--------------------------------------------------------------+
// NOTE: win32_base.c defines either a "wmain" or a "wWinMain" entry point, whereas linux_base.c just does regular "main".
//       In order to call metagen code we need to pass it all the arguments it would expect from being the real main entrypoint.
//       Thus we need to replicate this choice here so we can pass them in build_main().
//       To make our life easier we also convert all cmd-line arguments into UTF-8 strings in a StrArray in params.args.
#if (BUILDING_ON_WINDOWS && BUILD_CONSOLE_INTERFACE)
int wmain(int argc, WCHAR **argvWide)
#elif BUILDING_ON_WINDOWS
int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
#else
int main(int argc, const char* argv[])
#endif
{
	ProgramParams params = EMPTY;
	
	#if BUILDING_ON_WINDOWS && !BUILD_CONSOLE_INTERFACE
	int argc;
	LPWSTR *argvWide = CommandLineToArgvW(GetCommandLineW(), &argc);
	params.hInstance = hInstance;
	params.hPrevInstance = hPrevInstance;
	params.lpCmdLine = lpCmdLine;
	params.nShowCmd = nShowCmd;
	#endif
	
	#if BUILDING_ON_WINDOWS
	params.argvWide = argvWide;
	char** argv = (char**)malloc(sizeof(char*) * argc);
	for (int aIndex = 0; aIndex < argc; aIndex++)
	{
		Str argUtf8 = Utf16ToUtf8Str(MakeStr16Nt(argvWide[aIndex]));
		argv[aIndex] = argUtf8.chars;
	}
	#endif
	
	params.argc = argc;
	params.argv = argv;
	
	for (u64 aIndex = 1; aIndex < argc; aIndex++) { AddStrNt(&params.args, argv[aIndex]); }
	
	build_main(&params);
}

