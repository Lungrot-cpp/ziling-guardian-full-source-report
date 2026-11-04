# 智龄卫士：室内照护与跌倒预警终端

## 工程内容

- board：Ti60F225I3 FPGA工程、RTL/IP/引脚约束，RISC-V/TinyML软件、SDK、实际部署模型、测试源码、启动加载器源码，BIT/ELF/BIN及配套Flash镜像。
- pc/qq-bridge：QQ与DeepSeek会话桥接、固定报警通知、依赖锁文件和空白配置范例。
- pc/event-assistant：真实房间活动采集、SQLite按日汇总、DeepSeek只读工具及离线示例。
- report：与本包代码对应的技术报告PDF、LaTeX源文件和全部实际引用图片。
- docs：实际引脚配置、接线、启动和功能核对说明。
- verification：本轮离线检查结果和文件SHA256清单。

常规监测以纯视觉为主；近距离且完成配准、热观测有效时，红外辅助筛选人物照片。远距离热像分辨能力有限，辅助条件不足时保留视觉候选，独立视觉预警继续运行。

## 环境

原机环境为Windows、Efinity 2026.1、riscv-none-elf GCC 13.4.0、Python 3.13、Node.js >=22.13。Efinity及编译器需自行安装；压缩包包含SDK与已有厂商静态库，不包含工具安装程序。其他系统的构建未验证。

Python电脑端依赖见requirements-pc.txt，离线规则检查额外安装requirements-checks.txt。QQ桥接在pc/qq-bridge运行npm ci，需联网；按该目录README安装DSH/OneBot环境。复制config.example.json为config.json并填写自己的账号授权配置；发布包没有真实账号、访问令牌或聊天记录。

## 启动顺序

1. 完成docs中的接线；电脑有线网卡设置192.168.0.3/24，板卡固定192.168.0.2。
2. 先校验本包：python verify_submission.py。
3. 板端软件编译：设置RISCV_BIN为本机RISC-V编译器bin目录，在board/recognition/software运行python build_local.py。FPGA全量编译：初始化本机Efinity环境并设置EFINITY_HOME，在board/recognition/fpga运行python build.py，或直接打开v6_person.xml。
启动ROM可运行python board/bootloader/build.py重建，脚本核对结果与FPGA现有ROM逐字节一致；不自动更新FPGA或烧录。

4. 本包board/flash/v12_network_video_autoboot.hex为BIT与本轮BIN配对的离线Flash镜像。若仅验证程序，可使用本机调试工具加载ELF。写Flash应按厂商工具确认目标器件与地址；本轮没有执行物理烧录，不能把本轮离线prepare当作已上板验收。
5. 电脑启动事件接收：python board/tools/event_receiver.py；启动预览：python board/tools/ethernet_video.py。程序选项见--help；启动后打开板端网页，完成真实地面四点标定，选定要观察的主目标。
6. 启动活动采集：python pc/event-assistant/activity_history.py。它持续采集位置；关闭后不能记录活动，也不能追补过去的数据。仅运行一个采集实例。
7. QQ桥接依照自身README启动；固定报警另外运行node scripts/board-watch.mjs。首次运行前在qq-bridge根目录创建空state目录。
8. 接入活动问答：安装QQ桥接依赖后，运行node pc/event-assistant/connect-dsh.mjs。默认使用相邻pc/qq-bridge及当前用户.dsh/profiles/web，可通过QQ_BRIDGE_DIR、DSH_PROFILE、CARE_PYTHON指定位置。此步骤配置三个受限只读工具，按DSH要求重新加载配置后生效，不会发送测试消息。
9. 查看真实活动汇总：python pc/event-assistant/activity_history.py --summary today。无有效记录时返回available=false，不能将其解释为老人没有活动。

USB语音桥接见docs/USB语音报警使用说明.md；确认实际串口与1.8V/3.3V接口电平，不能把预留J28语音脚当作本次USB语音连接。

## 部署模型

板端实际部署模型由board/recognition/software/src/model/yolo_person_detect_model_data.h编译进入程序，报告中的110,236参数与该部署工程对应。模型目录与所需厂商库已打包。另行研发的99K训练项目不是此部署模型，本包未混入该研究工程。

## 离线检查与边界

红外回退及风险回归：python board/recognition/tests/check_infrared_assist.py（该脚本原机编译器默认D:/toolchain/bin）。活动与问答：在pc/event-assistant运行python -m unittest -v test_activity_history.py test_assistant.py。配准拟合：在board/tools运行python -m unittest -v test_calibrate_fusion.py。QQ报警离线检查：在pc/qq-bridge运行node scripts/test-board-watch.mjs。这些检查不会给真实QQ发消息或驱动板卡。

本包不附真实RGB/IR配准参数，FUSION_CALIBRATED保持false；近距离照片筛选效果需补真实配准与实物复测。配准流程见docs/红外近距离辅助与视觉回退.md。新活动数据通路已通过离线检查，本轮板卡HTTP不可达，尚未完成实板记录到QQ模型回复的端到端验证。原演示视频保持不变，须与实测结果分别核对；历史视频不能证明本轮修改已经加载到板卡。

原视频单独提交，不重复塞入代码压缩包。没有给出网站单包大小限制，本包未假定额外限制。
