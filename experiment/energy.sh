#!/bin/sh

cd /data/local/tmp/build_64
rm energy.out
while true; do
	pid=`pgrep runTrainDemo`
    if [ "$pid" -ne 0 ]; then
        usb_current=`cat /sys/class/power_supply/usb/current_now`
        usb_voltag=`cat /sys/class/power_supply/usb/voltage_now`
        batt_current=`cat /sys/class/power_supply/battery/current_now`
        batt_voltage=`cat /sys/class/power_supply/battery/voltage_now`
        echo "${pid}\t${usb_current}\t${usb_voltag}\t${batt_current}\t${batt_voltage}" | tee -a energy.out
    fi
	usleep 100
done