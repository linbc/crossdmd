#!/bin/sh

killall crossdmd
killall crossdmd_robot

#bin/sandboxsvr 监听端口
bin/crossdmd 10023 &

for i in $`seq 1`; do 
	#bin/crossdmd_robot 机器人数量 服务器1地址 服务器1端口 [服务器2地址 服务器2端口] [服务器3地址 服务器3端口] .....
	#bin/crossdmd_robot 1000 s0.9.game2.com.cn 843 s0.9.game2.com.cn 844; 
	#bin/crossdmd_robot 200 s0.9.game2.com.cn 843; 
	#bin/crossdmd_robot 5000 127.0.0.1 10023 127.0.0.1 10024 127.0.0.1 10025 127.0.0.1 10026; 
	bin/crossdmd_robot 10000 127.0.0.1 10023; 
	#bin/crossdmd_robot 5000 192.168.3.4 843; 
done;

exit
