/*
File:   build_script.c
Author: Taylor Robbins
Date:   05\20\2026
*/

#define PIG_BUILD_PRINT_SYS_CMDS 0
#define SRC_FOLDER   "[ROOT]/src"
#define LOCAL_FOLDER "[ROOT]/local"

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
	StrArray buildScriptSourceFolders = MakeStrArrayVa(
		"../src/metagen/",
		"../pig_build/src",
		"../build_targets.c" //TODO: This isn't actually supported yet!
	);
	RecompileIfNeeded(&buildScriptSourceFolders);
	IF_WINDOWS(bool isMsvcInitialized = WasMsvcDevBatchRun());
	
	// Default arguments (for convenience)
	if (params->args.length == 0)
	{
		AddStrLit(&params->args, "debug"); //"debug" or "release"
		AddStrLit(&params->args, "msvc"); //"msvc" or "clang"
		AddStrLit(&params->args, "raddbg"); //"raddbg", "radlink", "radbin", "debugstringperf", "mule_module", "mule_hotload", "torture"
	}
	
	Array_Targets targets = GetTargetDefinitions();
	
	BuildOptions options = EMPTY;
	HandleCmdLineArgs(&params->args, &targets, &options);
	
	CliArgs commonCompilerFlags = EMPTY;
	CliArgs commonLinkerFlags = EMPTY;
	FillCompilerAndLinkerFlags(&options, &commonCompilerFlags, &commonLinkerFlags);
	
	Str compilerExe = options.useCompilerMsvc ? StrLit(EXE_MSVC_CL) : StrLit(EXE_CLANG);
	Str linkerExe = options.useCompilerMsvc ? StrLit(EXE_MSVC_LINK) : StrLit(EXE_CLANG);
	
	Str localFolderResolved = ResolveRootTo(StrLit(LOCAL_FOLDER), StrLit(".."));
	if (!DoesFolderExist(localFolderResolved)) { mkdir(localFolderResolved.chars, FOLDER_PERMISSIONS); }
	
	StrArray commonTags = EMPTY;
	AddTag(&commonTags, options.useCompilerMsvc ? T_MSVC_CL : T_CLANG);
	if (options.useCompilerMsvc) { AddTag(&commonTags, T_MSVC_CL_OR_LINK); }
	IF_WINDOWS(AddTag(&commonTags, T_WINDOWS));
	IF_LINUX(AddTag(&commonTags, T_LINUX));
	IF_OSX(AddTag(&commonTags, T_OSX));
	
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
		RunCliProgramAndExitOnFailure(StrLit(EXE_MSVC_RC), T_MSVC_RC T_WINDOWS, &rcArgs, StrLit("Failed to compile logo.rc into logo.res!"));
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
			if (!def->isDll) { AddTaggedArgStr(&args, T_MSVC_CL, CL_OBJ_FILE, objPath); }
			
			AddTaggedArg(&args, T_MSVC_CL, CL_LINK);
			AddArgList(&args, &commonLinkerFlags);
			AddTaggedArgStr(&args, T_MSVC_CL, LINK_OUTPUT_FILE,  outputPath);
			AddTaggedArgStr(&args, T_CLANG,   CLANG_OUTPUT_FILE, outputPath);
			
			StrArray tags = EMPTY;
			AddStrArray(&tags, &commonTags);
			AddStr(&tags, targetName);
			if (def->isDll) { AddStrNt(&tags, "dll"); }
			AddTag(&tags, def->isCpp ? T_LANG_CPP : T_LANG_C);
			
			if (options.useCompilerMsvc) { InitializeMsvcIf(StrLit(PIG_BUILD_ROOT), &isMsvcInitialized); }
			RunCliProgramTagArrayAndExitOnFailure(compilerExe, &tags, &args, FormatStr("Failed to compile %.*s!", StrPrint(outputPath)));
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
	AddTaggedArg(commonCompilerFlags,   T_MSVC_CL, CL_NO_LOGO);
	AddTaggedArg(commonCompilerFlags,   T_MSVC_CL, CL_FULL_FILE_PATHS);
	AddTaggedArg(commonCompilerFlags,   T_CLANG,   CLANG_FULL_FILE_PATHS);
	AddTaggedArg(commonCompilerFlags,   T_MSVC_CL, CL_DEBUG_INFO_IN_OBJ);
	AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_ENABLE_LANG_CONFORMANCE_OPTION, "preprocessor");
	AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_INCLUDE_DIR,    SRC_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_INCLUDE_DIR, SRC_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_INCLUDE_DIR,    LOCAL_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_INCLUDE_DIR, LOCAL_FOLDER);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "_USE_MATH_DEFINES");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "strdup=_strdup");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_DEFINE, "_printf=printf");
	AddTaggedArg(commonCompilerFlags,   T_CLANG,   "-Xclang -flto-visibility-public-std"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   "-ferror-limit=[VAL]", "10000"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_M_FLAG, "cx16"); //TODO: What does this do?
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_M_FLAG, "sha"); //TODO: What does this do?
	
	// Debug/Release Dependent Options
	AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_OPTIMIZATION_LEVEL, options->releaseBuild ? "2" : "d");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_OPTIMIZATION_LEVEL, options->releaseBuild ? "2" : "0");
	AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_DEFINE,    options->releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_DEFINE, options->releaseBuild ? "BUILD_DEBUG=0" : "BUILD_DEBUG=1");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG,   CLANG_DEFINE, options->releaseBuild ? "NDEBUG" : "_DEBUG");
	if (options->releaseBuild)
	{
		AddTaggedArgNt(commonCompilerFlags, T_MSVC_CL, CL_OPTIMIZATION_LEVEL, "b1"); //Allows expansion only of functions marked "inline", "__inline", or "__forceinline"
	}
	else
	{
		// AddTaggedArg(commonCompilerFlags,   T_MSVC_CL, CL_DEBUG_INFO);
		AddTaggedArg(commonCompilerFlags,   T_CLANG, CLANG_DEBUG_INFO_DEFAULT);
		AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DEBUG_INFO, options->useDwarfFormat ? "dwarf" : "codeview");
	}
	
	// Warnings
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_WARNING_LEVEL, "all");
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNKNOWN_WARNING_OPTION);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_BRACES);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_FUNCTION);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_PARAMETER);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VALUE);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_VARIABLE);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_LOCAL_TYPEDEF);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_UNUSED_BUT_SET_VARIABLE);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_WRITABLE_STRINGS);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_MISSING_FIELD_INITIALIZERS);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_REGISTER);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_DEPRECATED_DECLARATIONS);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_SINGLE_BIT_BITFIELD_CONVERSION);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_COMPARE_DISTINCT_POINTER_TYPES);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_INITIALIZER_OVERRIDES);
	AddTaggedArgNt(commonCompilerFlags, T_CLANG, CLANG_DISABLE_WARNING, CLANG_WARNING_INCOMP_PNTR_DISCARDS_QUALIFIERS);
	
	// Linker flags (for MSVC these have to come after the /link argument)
	AddTaggedArg(commonLinkerFlags,   T_MSVC_CL "|dll",       LINK_BUILD_DLL);
	AddTaggedArg(commonLinkerFlags,   T_CLANG   "|dll",       LINK_BUILD_DLL);
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,                "-fuse-ld=[VAL]", "lld"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_WINDOWS "|dll=false", CLI_QUOTED_ARG, "logo.res");
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL,              "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,    "-Xlinker " "/MANIFEST:[VAL]", "EMBED");
	AddTaggedArg(commonLinkerFlags,   T_MSVC_CL,  LINK_DISABLE_INCREMENTAL);
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL,              "/pdbaltpath:[VAL]", "%_PDB%");
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,    "-Xlinker " "/pdbaltpath:[VAL]", "%_PDB%");
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL,              LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,    "-Xlinker " LINK_NATVIS_PATH, SRC_FOLDER "/natvis/base.natvis");
	AddTaggedArg(commonLinkerFlags,   T_MSVC_CL,  LINK_NO_EXP); //TODO: What does this do?
	AddTaggedArg(commonLinkerFlags,   T_MSVC_CL,  LINK_NO_COFF_GRP_INFO); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL,              LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,    "-Xlinker " LINK_OPT, "ref"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL,              LINK_OPT, "icf"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_CLANG,    "-Xlinker " LINK_OPT, "noicf"); //TODO: What does this do?
	
	//radlink extra options
	AddTaggedArg(commonLinkerFlags,   T_MSVC_CL "|radlink", "/NOIMPLIB"); //TODO: What does this do?
	AddTaggedArgNt(commonLinkerFlags, T_MSVC_CL "|radlink",             LINK_NATVIS_PATH, SRC_FOLDER "/linker/linker.natvis");
	AddTaggedArgNt(commonLinkerFlags, T_CLANG   "|radlink", "-Xlinker " LINK_NATVIS_PATH, SRC_FOLDER "/linker/linker.natvis");
}

// +--------------------------------------------------------------+
// |                       Real Entry-Point                       |
// +--------------------------------------------------------------+
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
		#if 1
		Str argUtf8 = Utf16ToUtf8Str(MakeStr16Nt(argvWide[aIndex]));
		argv[aIndex] = argUtf8.chars;
		#else
		int utf8Length = WideCharToMultiByte(CP_UTF8, 0, argvWide[aIndex], -1, NULL, 0, NULL, NULL);
		argv[aIndex] = (char*)malloc(utf8Length+1);
		WideCharToMultiByte(CP_UTF8, 0, argvWide[aIndex], -1, argv[aIndex], utf8Length, NULL, NULL);
		argv[aIndex][utf8Length] = '\0';
		#endif
	}
	#endif
	
	params.argc = argc;
	params.argv = argv;
	
	for (u64 aIndex = 1; aIndex < argc; aIndex++) { AddStrNt(&params.args, argv[aIndex]); }
	
	build_main(&params);
}

