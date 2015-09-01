# Debian Start/Stop Scripts #

These are the start and stop scripts being developed for use in a Debian environment. They allow the AX.25 system to be automatically started by init.d on boot, and shut down in the proper run levels.

There are three scripts that are used.

ax25 is the actual script that goes in /etc/init.d and gets called at the proper time.

ax25-up lives in /etc/ax25 and starts up the AX.25 subsystem and all necessary processes.

ax25-down also lives in /etc/ax25 and shuts everything down cleanly.

There is still some work to do (like do a better job at checking status), but this will get you started.


---


## ax25 ##

This script lives in /etc/init.d. Copy it in to a file called ax25 and then run:

```
bash# update-rc.d ax25 defaults 80 20
```

This will start the AX.25 subsystem at 80 and shut it down at 20.

```
#! /bin/bash
# Provided by Charles S Schuman ( k4gbb1@gmail.com )
# Updated 03/09/11
### BEGIN INIT INFO
# Provides: ax25
# Required-Start: $remote_fs $syslog $network
# Required-Stop: $remote_fs $syslog $network
# Default-Start: 2 3 4 5
# Default-Stop: 0 1 6
# Short-Description: Ax.25 initialization
# Description: This script provides the control for the ax.25 Packet Radio Service.
#The fine tuning is accomplished via /etc/ax25/ax25-up and /etc/ax25/ax25-down.
#
### END INIT INFO

# /etc/init.d/ax25
#
# Kernel-Parameter "ax25=yes|no" ?

if [ "`grep -e [Aa][Xx]25=[Nn][Oo] /proc/cmdline`" != "" ] ; then
echo -e "ax25: Aborting startup on user request (kernel boot parameter)."
exit 1
fi

case "$1" in
start|-start)
echo "/etc/init.d/ax25: Starting AX25..."
/etc/ax25/ax25-up
;;

stop|-stop)
echo "Stopping AX25..."
/etc/ax25/ax25-down
;;

restart|-restart)
echo "AX25 Restart"
/etc/ax25/ax25-down
sleep 2
etc/ax25/ax25-up
;;

status|-status)
if [ ! -d /proc/sys/net/ax25 ] ; then
echo "ax25 is down"
else
echo "$(ls /proc/sys/net/ax25)" > /tmp/ax25-config.tmp
read Select < /tmp/ax25-config.tmp
i=0
while [ "$Select" != "" ]
do
let i=i+1
awk ' NR == '$i' { print $1 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
read Select < /tmp/ax25-config-tmp
if [ "$Select" != "" ]; then
/sbin/ifconfig "$Select"
fi
done
if [ -z "$(uname -r | grep kjd)" ] ;then
netstat --ax25
else
/sbin/ifconfig ipax0
cat /proc/net/ax25
fi
fi
;;

*)
echo "Usage: $0 {start|stop|restart|status}"
exit 1
;;
esac

exit 0
```


---


## ax25-up ##

This script lives in /etc/ax25. You can comment out the blocks for the services that you do not want to start.

The first section configures and creates the ax0 (ax1, ax2, etc) devices. Comment out the blocks for the ports you do not need. Same goes for the port parameters, if you don't need to change them, just leave them commented out.

This script also starts FPAC and its related daemons.

```
#!/bin/bash

# Set Sysctl values
sysctl -w kernel.panic=30
sysctl -w kernel.panic_on_oops=30

########Initialize AX.25 Ports and attach devices########

# What IP are you using for this node's AX.25 devices?
amprIP=44.23.2.1

# Where are we going to log:
fpaclog=/var/log/fpac.log
ax25ipdlog=/var/log/ax25ipd.log

# Ensure IP forwarding is enabled
echo 1 > /proc/sys/net/ipv4/ip_forward

# Start ax25ipd and initialize master end of pseudo tty
echo "Installing ax25ipd Unix98 master pseudo tty"
/usr/sbin/ax25ipd &>$ax25ipdlog
awk '/\/dev\/pts/ {print $1}' $ax25ipdlog > /tmp/unix98
pidof ax25ipd > /var/run/ax25ipd.pid
export AXUDP=`tail -1 /tmp/unix98`
rm -f /tmp/unix98
echo -n "Starting ax25ipd          "
if [ -z "ps x | grep -c $(cat /var/run/ax25ipd.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

# Initialize ax0, attached to port axudp and the slave end of the
# pseudo tty for ax25ipd
/usr/sbin/kissattach $AXUDP axudp $amprIP >/tmp/ax25-config.tmp
awk '/device/ { print $7 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
read Device < /tmp/ax25-config-tmp
rm -f /tmp/ax25-config.tmp
rm -f /tmp/ax25-config-tmp
# Check for Device
if [ -d /proc/sys/net/ax25/$Device ]
  then echo "Port axudp attached to $Device"
  cd /proc/sys/net/ax25/$Device/
  ifconfig $Device netmask 255.255.255.255
  ifconfig $Device broadcast 0.0.0.0
  # Parms for axudp - Kernels > 2.6.17
#  echo 3100 > t1_timeout # (Frack) /100 = seconds ( 1 - 30) seconds
#  echo 1000 > t2_timeout # (RESPtime) /100 = .001 seconds (1 . 20)second
#  echo 300000 > t3_timeout # (Check) /600 = min (0 - 3600) seconds
#  echo 600000 > idle_timeout # Disconnect when idle /600 = min (0 or greater)
#  echo 0 > ax25_default_mode # AX25 Default Mode 0 = Normal, 1 = Extended (0)
#  echo 0 > ip_default_mode # IP Default Mode 0 = Standard (mod 7),1 = Extended (mod 127)(0)
#  echo 0 > backoff_type # Backoff 0 = Original, 1 = linear, 2 = exponential (1)
#  echo 256 > maximum_packet_length # Frame Size 1 - 512 (256) - overrides setting in axports
#  echo 2 > connect_mode # Connect Mode 0 = None, 1 = Network, 2 = All (2)
#  echo 8 > maximum_retry_count # N2 (1 -31)
#  echo 180000 > dama_slave_timeout
#  echo 32 > extended_window_size
#  echo 2 > standard_window_size # Max frames - overrides setting in axports
#  echo 0 > protocol
else
  echo "** Error setting $Device parms **"
fi

sleep 1
# Initialize ax1, attached to port vhfdrop and serial port ttyS0
/usr/sbin/kissattach /dev/ttyS0 vhfdrop $amprIP >/tmp/ax25-config.tmp
awk '/device/ { print $7 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
read Device < /tmp/ax25-config-tmp
rm -f /tmp/ax25-config.tmp
rm -f /tmp/ax25-config-tmp
# Check for Device
if [ -d /proc/sys/net/ax25/$Device ]
  then echo "Port vhfdrop attached to $Device"
  cd /proc/sys/net/ax25/$Device/
  ifconfig $Device netmask 255.255.255.255
  ifconfig $Device broadcast 0.0.0.0
  # Parms for vhfdrop - Kernels > 2.6.17
#  echo 3100 > t1_timeout # (Frack) /100 = seconds ( 1 - 30) seconds
#  echo 1000 > t2_timeout # (RESPtime) /100 = .001 seconds (1 . 20)second
#  echo 300000 > t3_timeout # (Check) /600 = min (0 - 3600) seconds
#  echo 600000 > idle_timeout # Disconnect when idle /600 = min (0 or greater)
#  echo 0 > ax25_default_mode # AX25 Default Mode 0 = Normal, 1 = Extended (0)
#  echo 0 > ip_default_mode # IP Default Mode 0 = Standard (mod 7),1 = Extended (mod 127)(0)
#  echo 0 > backoff_type # Backoff 0 = Original, 1 = linear, 2 = exponential (1)
#  echo 256 > maximum_packet_length # Frame Size 1 - 512 (256) - overrides setting in axports
#  echo 2 > connect_mode # Connect Mode 0 = None, 1 = Network, 2 = All (2)
#  echo 8 > maximum_retry_count # N2 (1 -31)
#  echo 180000 > dama_slave_timeout
#  echo 32 > extended_window_size
#  echo 2 > standard_window_size # Max frames - overrides setting in axports
#  echo 0 > protocol
else
  echo "** Error setting $Device parms **"
fi
# Adjust KISS Parameters: Persistence=128, slot=10, TX-Delay=250
#/usr/sbin/kissparms -p vhfdrop -r 228 -s 10 -l 20 -t 250

#sleep 1
## Initialize ax2, attached to port uhfdrop and serial port ttyS1
#/usr/sbin/kissattach /dev/ttyS1 uhfdrop $amprIP >/tmp/ax25-config.tmp
#awk '/device/ { print $7 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
#read Device < /tmp/ax25-config-tmp
#rm -f /tmp/ax25-config.tmp
#rm -f /tmp/ax25-config-tmp
## Check for Device
#if [ -d /proc/sys/net/ax25/$Device ]
#  then echo "Port axudp attached to $Device"
#  cd /proc/sys/net/ax25/$Device/
#  ifconfig $Device netmask 255.255.255.255
#  ifconfig $Device broadcast 0.0.0.0
#  # Parms for uhfdrop - Kernels > 2.6.17
##  echo 3100 > t1_timeout # (Frack) /100 = seconds ( 1 - 30) seconds
##  echo 1000 > t2_timeout # (RESPtime) /100 = .001 seconds (1 . 20)second
##  echo 300000 > t3_timeout # (Check) /600 = min (0 - 3600) seconds
##  echo 600000 > idle_timeout # Disconnect when idle /600 = min (0 or greater)
##  echo 0 > ax25_default_mode # AX25 Default Mode 0 = Normal, 1 = Extended (0)
##  echo 0 > ip_default_mode # IP Default Mode 0 = Standard (mod 7),1 = Extended (mod 127)(0)
##  echo 0 > backoff_type # Backoff 0 = Original, 1 = linear, 2 = exponential (1)
##  echo 256 > maximum_packet_length # Frame Size 1 - 512 (256) - overrides setting in axports
##  echo 2 > connect_mode # Connect Mode 0 = None, 1 = Network, 2 = All (2)
##  echo 8 > maximum_retry_count # N2 (1 -31)
##  echo 180000 > dama_slave_timeout
##  echo 32 > extended_window_size
##  echo 2 > standard_window_size # Max frames - overrides setting in axports
##  echo 0 > protocol
#else
#  echo "** Error setting $Device parms **"
#fi
## Adjust KISS Parameters: Persistence=128, slot=10, TX-Delay=250
##/usr/sbin/kissparms -p uhfdrop -r 228 -s 10 -l 20 -t 250
#
#sleep 1
## Initialize ax3, attached to port west and serial port ttyS2
#/usr/sbin/kissattach /dev/ttyS2 west $amprIP >/tmp/ax25-config.tmp
#awk '/device/ { print $7 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
#read Device < /tmp/ax25-config-tmp
#rm -f /tmp/ax25-config.tmp
#rm -f /tmp/ax25-config-tmp
## Check for Device
#if [ -d /proc/sys/net/ax25/$Device ]
#  then echo "Port axudp attached to $Device"
#  cd /proc/sys/net/ax25/$Device/
#  ifconfig $Device netmask 255.255.255.255
#  ifconfig $Device broadcast 0.0.0.0
#  # Parms for west - Kernels > 2.6.17
##  echo 3100 > t1_timeout # (Frack) /100 = seconds ( 1 - 30) seconds
##  echo 1000 > t2_timeout # (RESPtime) /100 = .001 seconds (1 . 20)second
##  echo 300000 > t3_timeout # (Check) /600 = min (0 - 3600) seconds
##  echo 600000 > idle_timeout # Disconnect when idle /600 = min (0 or greater)
##  echo 0 > ax25_default_mode # AX25 Default Mode 0 = Normal, 1 = Extended (0)
##  echo 0 > ip_default_mode # IP Default Mode 0 = Standard (mod 7),1 = Extended (mod 127)(0)
##  echo 0 > backoff_type # Backoff 0 = Original, 1 = linear, 2 = exponential (1)
##  echo 256 > maximum_packet_length # Frame Size 1 - 512 (256) - overrides setting in axports
##  echo 2 > connect_mode # Connect Mode 0 = None, 1 = Network, 2 = All (2)
##  echo 8 > maximum_retry_count # N2 (1 -31)
##  echo 180000 > dama_slave_timeout
##  echo 32 > extended_window_size
##  echo 2 > standard_window_size # Max frames - overrides setting in axports
##  echo 0 > protocol
#else
#  echo "** Error setting $Device parms **"
#fi
## Adjust KISS Parameters: Persistence=128, slot=10, TX-Delay=250
##/usr/sbin/kissparms -p west -r 228 -s 10 -l 20 -t 250
#
#sleep 1
## Initialize ax4, attached to port east and serial port ttyS3
#/usr/sbin/kissattach /dev/ttyS3 east $amprIP >/tmp/ax25-config.tmp
#awk '/device/ { print $7 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
#read Device < /tmp/ax25-config-tmp
#rm -f /tmp/ax25-config.tmp
#rm -f /tmp/ax25-config-tmp
## Check for Device
#if [ -d /proc/sys/net/ax25/$Device ]
#  then echo "Port axudp attached to $Device"
#  cd /proc/sys/net/ax25/$Device/
#  ifconfig $Device netmask 255.255.255.255
#  ifconfig $Device broadcast 0.0.0.0
#  # Parms for east - Kernels > 2.6.17
##  echo 3100 > t1_timeout # (Frack) /100 = seconds ( 1 - 30) seconds
##  echo 1000 > t2_timeout # (RESPtime) /100 = .001 seconds (1 . 20)second
##  echo 300000 > t3_timeout # (Check) /600 = min (0 - 3600) seconds
##  echo 600000 > idle_timeout # Disconnect when idle /600 = min (0 or greater)
##  echo 0 > ax25_default_mode # AX25 Default Mode 0 = Normal, 1 = Extended (0)
##  echo 0 > ip_default_mode # IP Default Mode 0 = Standard (mod 7),1 = Extended (mod 127)(0)
##  echo 0 > backoff_type # Backoff 0 = Original, 1 = linear, 2 = exponential (1)
##  echo 256 > maximum_packet_length # Frame Size 1 - 512 (256) - overrides setting in axports
##  echo 2 > connect_mode # Connect Mode 0 = None, 1 = Network, 2 = All (2)
##  echo 8 > maximum_retry_count # N2 (1 -31)
##  echo 180000 > dama_slave_timeout
##  echo 32 > extended_window_size
##  echo 2 > standard_window_size # Max frames - overrides setting in axports
##  echo 0 > protocol
#else
#  echo "** Error setting $Device parms **"
#fi
## Adjust KISS Parameters: Persistence=128, slot=10, TX-Delay=250
##/usr/sbin/kissparms -p east -r 228 -s 10 -l 20 -t 250

######## Finished initializing AX.25 ports and attaching devices ########


######## Start NET/ROM and FLEXd ########

# Start NET/ROM
/usr/sbin/netromd &
pidof netromd > /var/run/netromd.pid
echo -n "Starting netromd          "
if [ -z "ps x | grep -c $(cat /var/run/netromd.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

# Start FLEXd
/usr/sbin/flexd
pidof flexd > /var/run/flexd.pid
echo -n "Starting flexd            "
if [ -z "ps x | grep -c $(cat /var/run/flexd.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi
######## Finished starting NET/ROM and FLEXd ########


######## Start daemons ########

# Start ax25d daemon to enable external logons
#/usr/sbin/ax25d &
#pidof ax25d > /var/run/ax25d.pid
#echo -n "Starting ax25d            "
#if [ -z "ps x | grep -c $(cat /var/run/ax25d.pid)" ]
#  then
#     echo -e "[failed]\n"
#     exit
#  else
#     echo -e "[ok]\n"
#fi

# Start Mheard daemon
/usr/sbin/mheardd
pidof mheardd > /var/run/mheardd.pid
echo -n "Starting mheardd          "
if [ -z "ps x | grep -c $(cat /var/run/mheardd.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

# Start FPACsoftware
echo "Starting up on:" >> $fpaclog
/bin/date >> $fpaclog
/usr/sbin/fpad >>$fpaclog 2>&1
pidof fpad > /var/run/fpad.pid
echo -n "Starting fpad             "
if [ -z "ps x | grep -c $(cat /var/run/fpad.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

/usr/sbin/fpacwpd >>$fpaclog 2>&1
pidof fpacwpd > /var/run/fpacwpd.pid
echo -n "Starting fpacwpd          "
if [ -z "ps x | grep -c $(cat /var/run/fpacwpd.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

/usr/sbin/fpacstat >>$fpaclog 2>&1
pidof fpacstat > /var/run/fpacstat.pid
echo -n "Starting fpacstat         "
if [ -z "ps x | grep -c $(cat /var/run/fpacstat.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

/usr/sbin/fpacroute >>$fpaclog 2>&1
pidof fpacroute > /var/run/fpacroute.pid
echo -n "Starting fpacroute        "
if [ -z "ps x | grep -c $(cat /var/run/fpacroute.pid)" ]
  then
     echo -e "[failed]\n"
     exit
  else
     echo -e "[ok]\n"
fi

######## End ########
```


---


## ax25-down ##

Finally we come to the ax25-down script. It too lives in /etc/ax25. You shouldn't need to do much editing to this one, as it will just skip over the stuff that isn't running.

```
#!/bin/bash

# Stop FPAC if it is running
if [ -e /var/run/fpacroute.pid ]; then
  kill $(cat /var/run/fpacroute.pid) >/dev/null 2>&1
  rm -f /var/run/fpacroute.pid
  pidof fpacroute >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "FPACRoute stopped successfully"
  else
    echo "FPACRoute failed to stop - check manually"
  fi
fi

if [ -e /var/run/fpacstat.pid ]; then
  kill $(cat /var/run/fpacstat.pid) >/dev/null 2>&1
  rm -f /var/run/fpacstat.pid
  pidof fpacstat >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "FPACStat stopped successfully"
  else
    echo "FPACStat failed to stop - check manually"
  fi
fi

if [ -e /var/run/fpacwpd.pid ]; then
  kill $(cat /var/run/fpacwpd.pid) >/dev/null 2>&1
  rm -f /var/run/fpacwpd.pid
  pidof fpacwpd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "FPACWpd stopped successfully"
  else
    echo "FPACWpd failed to stop - check manually"
  fi
fi

if [ -e /var/run/fpad.pid ]; then
  kill $(cat /var/run/fpad.pid) >/dev/null 2>&1
  rm -f /var/run/fpad.pid
  pidof fpad >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "FPAD stopped successfully"
  else
    echo "FPAD failed to stop - check manually"
  fi
fi

# Stop FLEXd if it is running
if [ -e /var/run/flexd.pid ]; then
  kill $(cat /var/run/flexd.pid) >/dev/null 2>&1
  rm -f /var/run/flexd.pid
  pidof flexd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "FLEXD stopped successfully"
  else
    echo "FLEXD failed to stop - check manually"
  fi
fi

# Stop beacon if it is running
if [ -e /var/run/beacon.pid ]; then
  kill $(cat /var/run/beacon.pid) >/dev/null 2>&1
  rm -f /var/run/beacon.pid
  pidof beacon >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "Beacon stopped successfully"
  else
    echo "Beacon failed to stop - check manually"
  fi
fi

# Stop netromd if it is running
if [ -e /var/run/netromd.pid ]; then
  kill $(cat /var/run/netromd.pid) >/dev/null 2>&1
  rm -f /var/run/netromd.pid
  pidof netromd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "Netromd stopped successfully"
  else
    echo "Netromd failed to stop - check manually"
  fi
fi

# Stop mheardd if it is running
if [ -e /var/run/mheardd.pid ]; then
  kill $(cat /var/run/mheardd.pid) >/dev/null 2>&1
  rm -f /var/run/mheardd.pid
  pidof mheardd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "Mheardd stopped successfully"
  else
    echo "Mheardd failed to stop - check manually"
  fi
fi

# Stop ax25d if it is running
if [ -e /var/run/ax25d.pid ]; then
  kill $(cat /var/run/ax25d.pid) >/dev/null 2>&1
  rm -f /var/run/ax25d.pid
  pidof ax25d >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "AX25d stopped successfully"
  else
    echo "AX25d failed to stop - check manually"
  fi
fi

# Stop ax25ipd if it is running
if [ -e /var/run/ax25ipd.pid ]; then
  kill $(cat /var/run/ax25ipd.pid) >/dev/null 2>&1
  rm -f /var/run/ax25ipd.pid
  pidof ax25ipd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "AX25ipd stopped successfully"
  else
    echo "AX25ipd failed to stop - check manually"
  fi
fi

# Stop ax25rtd if it is running
if [ -e /var/run/ax25rtd.pid ]; then
  kill $(cat /var/run/ax25rtd.pid) >/dev/null 2>&1
  rm -f /var/run/ax25rtd.pid
  pidof ax25rtd >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "AX25rtd stopped successfully"
  else
    echo "AX25rtd failed to stop - check manually"
  fi
fi

# Stop Monitor program
if [ -e /var/run/listen.pid ]; then
  kill $(cat /var/run/listen.pid) >/dev/null 2>&1
  rm -f /var/run/listen.pid
  pidof listen >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "Listen stopped successfully"
  else
    echo "Listen failed to stop - check manually"
  fi
fi

# Detach ROSE Devices
echo "Detaching ROSE devices:"
awk '/rose/{print $1}' /etc/ax25/rsports >> /tmp/ax25-config.tmp
read Select < /tmp/ax25-config.tmp
i=0
while [ "$Select" != "" ]; do
  let i=i+1
  awk ' NR == '$i' { print $1 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
  read Select < /tmp/ax25-config-tmp
  if [ "$Select" != "" ]; then
    ifconfig "$Select" down
    echo " $Select Stopped"
  fi
done
rm -f /tmp/ax25-config.tmp
rm -f /tmp/ax25-config-tmp
sleep 1

# Detach AX, and Netrom Devices
echo "Detaching AX and NR Devices:"
echo "$(ls /proc/sys/net/ax25)" > /tmp/ax25-config.tmp
#awk '/rose/{print $1}' /etc/ax25/rsports >> /tmp/ax25-config.tmp
read Select < /tmp/ax25-config.tmp
i=0
while [ "$Select" != "" ]; do
  let i=i+1
  awk ' NR == '$i' { print $1 }' /tmp/ax25-config.tmp > /tmp/ax25-config-tmp
  read Select < /tmp/ax25-config-tmp
  if [ "$Select" != "" ]; then
    ifconfig "$Select" down
    echo " $Select Stopped"
  fi
done
rm -f /tmp/ax25-config.tmp
rm -f /tmp/ax25-config-tmp

# Stop Kissattach
kill $(pidof kissattach) >/dev/null 2>&1
sleep 1
pidof kissattach >/dev/null
  RETVAL=$?
  if [ $RETVAL -eq 1 ]; then
    echo "Kisattach stopped successfully"
  else
    echo "Kissattach failed to stop - check manually"
  fi

echo "Ax25 Stopped"
# End of ax25-down
```