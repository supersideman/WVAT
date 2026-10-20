@echo off
rem 双击这个文件查看提交历史（gitk 是 Git 自带的图形历史浏览器，不用安装）
rem 注意：要在「资源管理器」里双击，在聊天界面里点它只会显示源码
cd /d "%~dp0"
gitk --all
if errorlevel 1 (
  echo.
  echo gitk 启动失败，把上面的报错发给我
  pause >nul
)
