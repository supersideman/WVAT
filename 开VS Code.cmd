@echo off
rem 启动工作区自带的便携版 VS Code（免安装，不写系统目录）
rem 必须清掉 ELECTRON_RUN_AS_NODE，否则 Code.exe 会当成 node 运行
set "ELECTRON_RUN_AS_NODE="
cd /d "%~dp0"
start "" "%~dp0tools\vscode-portable\Code.exe" "%~dp0"
