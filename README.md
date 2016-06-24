# mosquitto-optimized
mosquitto is optimized by using linux epoll


mosquitto version 1.4.8 (build date 2016-05-01 10:33:22+0800)

mosquitto is an MQTT v3.1 broker.

Usage: mosquitto [-c config_file] [-d] [-h] [-p port]

-c : specify the broker config file.
-d : put the broker into the background after starting.
-h : display this help.
-p : start the broker listening on the specified port.
     Not recommended in conjunction with the -c option.
-v : verbose mode - enable all logging types. This overrides
     any logging options given in the config file.
-ep : epoll mode - enable linux epoll. This overrides

See http://mosquitto.org/ for more information.

using linux epoll mode
mosquitto is optimization based on version 1.4.8
mosquitto -ep -c xxxx.conf -d -p 1883
