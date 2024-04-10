@echo off

where /q cl || (
  echo ERROR: could not find "cl" - run the "build.bat" from the MSVC x64 native tools command prompt.
  exit /b 1
)

set warnings=/WX /W4 /wd4201 /wd4100 /wd4189 /wd4505 /wd4701 /wd4101 /wd4324
set includes=/I ../my_lib/ /I ../external/ /I ../external/D3D12/headers/d3dx12/ /I ../external/dxc/ /I ../external/D3D12/headers/ 
set linkerFlags=/OUT:DeRex12.exe /INCREMENTAL:NO /OPT:REF /CGTHREADS:6 /STACK:0x100000,0x100000
set linkerLibs=user32.lib gdi32.lib winmm.lib dxguid.lib dxgi.lib d3d12.lib dxcompiler.lib
set compilerFlags=/std:c++20 /MP /arch:AVX2 /Oi /Ob3 /EHsc /fp:fast /fp:except- /nologo /GS- /Gs999999 /GR- /FC /Z7 %includes% %warnings%
set translation_units=../source/Win32_x64_Platform.cpp ../source/RHI_D3D12.cpp

set dxcLib=/LIBPATH:../external/dxc/

if "%~1"=="-Debug" (
	echo [[ debug build ]]
	set compilerFlags=%compilerFlags% /Od /MTd /D_DEBUG
	REM /DD3DCOMPILE_DEBUG 
)
if "%~1"=="" (
	echo [[ debug build ]]
	set compilerFlags=%compilerFlags% /Od /MTd /D_DEBUG
	set rayname=d_raylib
)
if "%~1"=="-Release" (
	echo [[ release build ]]
	set compilerFlags=%compilerFlags% /O2 /MT 
)

IF NOT EXIST .\build mkdir .\build
pushd .\build
REM del *.pdb > NUL 2> NUL

REM Hot-Reload build
cl.exe %compilerFlags% ../source/App.cpp /LD /link %dxcLib% %linkerLibs% RHI_D3D12.obj /EXPORT:app_full_update
cl.exe %compilerFlags% %translation_units% /link %dxcLib% %linkerFlags% %linkerLibs%

popd