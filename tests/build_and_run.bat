@echo off
rem ============================================================
rem  AABB 碰撞单元测试 —— 一键编译并运行（便捷脚本，可选）
rem
rem  用法：
rem    直接双击本文件；或在任意终端中执行 tests\build_and_run.bat
rem
rem  内容：定位 Visual Studio -> vcvars64 -> cl 直编 -> 运行测试
rem  产物：tests\rect_test.exe（编译中间文件同样落在 tests\ 下）
rem  退出码：0 = 全部用例通过；1~4 = 脚本自身错误；其余 = 失败用例数
rem ============================================================
setlocal

rem —— 切换控制台到 UTF-8，保证中文输出正常（结束时尽量恢复） ——
for /f "tokens=2 delims=:" %%a in ('chcp') do set "OLDCP=%%a"
set "OLDCP=%OLDCP: =%"
chcp 65001 >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [错误] 未找到 vswhere.exe，无法定位 Visual Studio 安装。
    exit /b 1
)

set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [错误] vswhere 未能返回 Visual Studio 安装路径。
    exit /b 2
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (
    echo [错误] cl.exe 不可用，vcvars64 初始化失败。
    exit /b 3
)

pushd "%~dp0.."

cl /nologo /std:c++20 /utf-8 /EHsc /Fo:tests\ /I vendor\SDL-main\include tests\rect_collision_test.cpp /Fe:tests\rect_test.exe
if errorlevel 1 (
    echo [错误] 编译失败，请查看上方 cl 输出。
    popd
    exit /b 4
)

tests\rect_test.exe
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo [结果] 全部用例通过（退出码 0）。
) else (
    echo [结果] 存在失败用例，退出码 %RC% —— 详见上方 [FAIL] 行。
)

echo.
echo [提示] 按任意键关闭本窗口...
pause >nul

if defined OLDCP chcp %OLDCP% >nul
popd
endlocal & exit /b %RC%
