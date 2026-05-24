@echo off
if not exist pig_build (
	echo Cloning PigBuild...
	git clone https://github.com/PiggybankStudios/PigBuild pig_build
)
set PIG_BUILD_FLAGS=/I"../src"
call pig_build\shell\build.bat %*