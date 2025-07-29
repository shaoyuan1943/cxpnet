@echo off
setlocal

:: 脚本使用方法:
:: build_windows.bat clean            # 清理并重新生成 Visual Studio 工程
:: build_windows.bat all              # 编译所有示例
:: build_windows.bat example_name     # 编译特定示例

set BUILD_DIR=build
set EXAMPLES_DIR=examples

if "%1"=="" (
    echo 错误: 请提供操作参数。
    echo 用法: %0 {clean^|all^|example_name}
    exit /b 1
)

set ACTION=%1

:: --- CMake 生成函数 ---
:ensure_cmake_project
if not exist "%BUILD_DIR%\build.ninja" (
    if not exist "%BUILD_DIR%\ALL_BUILD.vcxproj" (
        echo Build directory or project files not found. Generating cmake project...
        if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
        mkdir "%BUILD_DIR%"
        echo Generating project with Ninja...
        cmake -S . -B "%BUILD_DIR%" -G "Ninja"
        if errorlevel 1 (
            echo Ninja not found, trying Visual Studio...
            cmake -S . -B "%BUILD_DIR%"
        )
    )
)
goto :eof


:: --- 主逻辑 ---
if /i "%ACTION%"=="clean" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    mkdir "%BUILD_DIR%"
    echo Generating cmake project...
    cmake -S . -B "%BUILD_DIR%"
    echo Clean and cmake generation completed.
    goto :eof
)

if /i "%ACTION%"=="all" (
    call :ensure_cmake_project
    
    echo Building all examples...
    :: 使用 --build 参数，CMake会自动调用正确的构建工具 (Ninja, MSBuild, etc.)
    cmake --build "%BUILD_DIR%" --config Release
    echo All examples built successfully.

    echo Copying executables...
    for /r "%BUILD_DIR%\%EXAMPLES_DIR%" %%f in (*.exe) do (
        set "exe_path=%%~f"
        set "exe_name=%%~nxf"
        for %%d in ("%EXAMPLES_DIR%\%%~nF") do (
            if exist "%%~d" (
                echo   - Copying !exe_name! to %%~d\
                copy "!exe_path!" "%%~d\"
            )
        )
    )
    echo Copying completed.
    goto :eof
)

:: 编译特定示例
set EXAMPLE_NAME=%ACTION%

if not exist "%EXAMPLES_DIR%\%EXAMPLE_NAME%" (
    echo 错误: Example '%EXAMPLE_NAME%' not found in %EXAMPLES_DIR%\
    exit /b 1
)

call :ensure_cmake_project

echo Building example: %EXAMPLE_NAME%
cmake --build "%BUILD_DIR%" --target %EXAMPLE_NAME% --config Release
echo Example '%EXAMPLE_NAME%' built successfully.

set "SOURCE_EXE=%BUILD_DIR%\%EXAMPLES_DIR%\%EXAMPLE_NAME%\Release\%EXAMPLE_NAME%.exe"
if not exist "%SOURCE_EXE%" set "SOURCE_EXE=%BUILD_DIR%\%EXAMPLES_DIR%\%EXAMPLE_NAME%\%EXAMPLE_NAME%.exe"
set "DEST_DIR=%EXAMPLES_DIR%\%EXAMPLE_NAME%\"

echo Copying %EXAMPLE_NAME%.exe to %DEST_DIR%...
copy "%SOURCE_EXE%" "%DEST_DIR%"
echo Copying completed.

endlocal