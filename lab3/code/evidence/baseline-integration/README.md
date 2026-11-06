# 原始基线整合验证（2026-11-06）

当前源码以提供的lab3_origin/lab3_new为基准。最终结论与工具版本见summary.json。

- grade.log：原始注入器及评分测试，最终100/100。
- .qemu.GRADE_LAB3_*.out：对应三项评分的完整串口记录（隐藏文件）。
- qemu-normal.log：常规镜像启动，First-Fit自检、10行100 ticks后正常关机；命令进程退出0。
- supplemental/：First-Fit下的真实异常、PMM中断保护、编码边界及关机补充验证。
- best-fit/：同样验证Best-Fit，并执行原给定best_fit_check，6/6分。
- source-comparison.json：原始56文件和当前文件的SHA-256，无缺失，46文件原样；另记新增验证工具哈希。
- initial-loader-failure/：原始设备加载器参数在本机的首次失败记录，固件Next Address=0；之后使用-kernel修复启动交接。

课程grade.sh仅更换启动参数，不改断言、分值和时限；inject.py、test_lab3.c/.h、test_lab3_context.S保持原样。补充验证仅对临时副本接入测试，不改当前源码或常规镜像。原始源码中已有的行尾空白随基线保留，git diff --check会报告这些格式提示。

旧版阶段日志位于上层p1、p2、p3，不能视作本轮源码的运行证据。替换前完整备份位于仓库外OS/lab3/backups/20261106-151429/。
