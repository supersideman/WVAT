/**
 * @file    ui_version.h
 * @brief   固件版本与构建信息的唯一事实来源
 *
 * 为什么单独一个头文件：
 *   - 版本号会出现在三处：开机串口日志的版本戳、设置页的"关于"行、
 *     以及文档/标签；集中在这里改一处就全齐。
 *   - 注意和其他版本戳的关系：
 *       OV_FW_VERSION  界面/文档用的发行版号（跟着 git tag 走）
 *       [mic] fw=vNN   麦克风诊断子模块的版本戳（user_i2s.c 里）
 *     两者用途不同，不要互相替代。
 */
#ifndef UI_VERSION_H
#define UI_VERSION_H

#define OV_FW_VERSION   "0.11"        /* 发行版号：对应 git tag v0.11-link-ui */
#define OV_FW_TAG       "v0.11-link-ui"
#define OV_FW_DATE      "2026-11-21"

#endif /* UI_VERSION_H */
