
## Serial2IP based on ESP32C3 with OLED supported


### 硬件 

打淘宝搜索“天问 ESP32C3 Pro”，选择带屏的版本即可。

### 使用帮助


[使用帮助](docs/user_guide.md)


### 测试 


打开一个终端 

```sh
./test_serial.py /dev/cu.usbserial-A50285BI 460800 -S -d 10

```

打开另一个终端,执行 

```sh
./test_tcp.py 192.168.5.134 5678 -S -d 10 -r 400
```
