# UDP网络质量探测

## 目标
网络中物理主机进行UDP通信时，有两个重要的质量指标需要监控：
- 延时，即UDP数据报往返时间
- 丢包率

该工具通过周期性发送不同大小的UDP数据报来收集通信时延和丢包率，为实时监控UDP通信质量服务。

# 介绍
该工程包括两部分：
- udpprobecli
  - 探测客户端，负责周期性向所有目标地址发送不同大小的UDP数据报，并收集时延和丢包率
  - 运行相关参数通过xml文件配置
  - 探测结果输出到日志文件，后续可通过脚本进行二次分析和可视化
  
- udpechosvr
  - 探测服务器端，实际上就是个简单的echo服务

## 开发环境
- ubuntu 18.04.2, amd64
- boost, version 1.70.0
- g++ version 4.8.5
- cmake version 3.10.2

上面是作者开发时的调试环境，如果各项版本不一致应该也可以正常构建

## 构建
在udpprobe目录下：
1. mkdir build
2. cd build
3. cmake ../src
4. make

## 安装
build完成后，再udpprobe/build目录下执行：
make install

## 配置
conf/udpprobeconf.xml配置文件可配置日志文件地址，各项探测参数

## 运行
切换到bin目录下
- 启动服务器端
./udpechosvr port
- 启动客户端
./udpprobecli --conf-file=../conf/udpprobeconf.xml

## 其它
- 探测结果实时输出到日志文件，需要使用脚本进一步分析，及可视化
