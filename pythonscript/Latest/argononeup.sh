#!/bin/bash

echo "*************"
echo " Argon Setup  "
echo "*************"


# Check time if need to 'fix'
NEEDSTIMESYNC=0
LOCALTIME=$(date -u +%s%N | cut -b1-10)
GLOBALTIME=$(curl -s 'http://worldtimeapi.org/api/ip.txt' | grep unixtime | cut -b11-20)
TIMEDIFF=$((GLOBALTIME-LOCALTIME))

# about 26hrs, max timezone difference
if [ $TIMEDIFF -gt 100000 ]
then
	NEEDSTIMESYNC=1
fi


argon_time_error() {
	echo "**********************************************"
	echo "* WARNING: Device time seems to be incorrect *"
	echo "* This may cause problems during setup.      *"
	echo "**********************************************"
	echo "Possible Network Time Protocol Server issue"
	echo "Try running the following to correct:"
    echo " curl -k http://files.iamnet.com.ph/argon/setup/tools/setntpserver.sh | bash"
}

if [ $NEEDSTIMESYNC -eq 1 ]
then
	argon_time_error
fi


# Helper variables
ARGONDOWNLOADSERVER=http://files.iamnet.com.ph/argon/setup

INSTALLATIONFOLDER=/etc/argon
pythonbin="sudo /usr/bin/python3"

versioninfoscript=$INSTALLATIONFOLDER/argon-versioninfo.sh

uninstallscript=$INSTALLATIONFOLDER/argon-uninstall.sh
configscript=$INSTALLATIONFOLDER/argon-config
unitconfigscript=$INSTALLATIONFOLDER/argon-unitconfig.sh
argondashboardscript=$INSTALLATIONFOLDER/argondashboard.py


setupmode="Setup"

if [ -f $configscript ]
then
	setupmode="Update"
	echo "Updating files"
else
	sudo mkdir $INSTALLATIONFOLDER
	sudo chmod 755 $INSTALLATIONFOLDER
fi

##########
# Start code lifted from raspi-config
# set_config_var based on raspi-config

if [ -e /boot/firmware/config.txt ] ; then
  FIRMWARE=/firmware
else
  FIRMWARE=
fi
CONFIG=/boot${FIRMWARE}/config.txt

set_config_var() {
    if ! grep -q -E "$1=$2" $3 ; then
      echo "$1=$2" | sudo tee -a $3 > /dev/null
    fi
}

# End code lifted from raspi-config
##########

# Reuse set_config_var
set_nvme_default() {
	set_config_var dtparam nvme $CONFIG
	set_config_var dtparam=pciex1_gen 3 $CONFIG
}


argon_check_pkg() {
    RESULT=$(dpkg-query -W -f='${Status}\n' "$1" 2> /dev/null | grep "installed")

    if [ "" == "$RESULT" ]; then
        echo "NG"
    else
        echo "OK"
    fi
}


CHECKDEVICE="oneup"	# Hardcoded for argononeup

CHECKGPIOMODE="libgpiod" # libgpiod or rpigpio

# Check if Raspbian, Ubuntu, others
CHECKPLATFORM="Others"
CHECKPLATFORMVERSION=""
CHECKPLATFORMVERSIONNUM=""
if [ -f "/etc/os-release" ]
then
	source /etc/os-release
	if [ "$ID" = "raspbian" ]
	then
		CHECKPLATFORM="Raspbian"
		CHECKPLATFORMVERSION=$VERSION_ID
	elif [ "$ID" = "debian" ]
	then
		# For backwards compatibility, continue using raspbian
		CHECKPLATFORM="Raspbian"
		CHECKPLATFORMVERSION=$VERSION_ID
	elif [ "$ID" = "ubuntu" ]
	then
		CHECKPLATFORM="Ubuntu"
		CHECKPLATFORMVERSION=$VERSION_ID
	fi
	echo ${CHECKPLATFORMVERSION} | grep -e "\." > /dev/null
	if [ $? -eq 0 ]
	then
		CHECKPLATFORMVERSIONNUM=`cut -d "." -f2 <<< $CHECKPLATFORMVERSION `
		CHECKPLATFORMVERSION=`cut -d "." -f1 <<< $CHECKPLATFORMVERSION `
	fi
fi

gpiopkg="python3-libgpiod"
if [ "$CHECKGPIOMODE" = "rpigpio" ]
then
	if [ "$CHECKPLATFORM" = "Raspbian" ]
	then
		gpiopkg="raspi-gpio python3-rpi.gpio"
	else
		gpiopkg="python3-rpi.gpio"
	fi
fi

pkglist=($gpiopkg python3-smbus i2c-tools python3-evdev ddcutil)


for curpkg in ${pkglist[@]}; do
	sudo apt-get install -y $curpkg
	RESULT=$(argon_check_pkg "$curpkg")
	if [ "NG" == "$RESULT" ]
	then
		echo "********************************************************************"
		echo "Please also connect device to the internet and restart installation."
		echo "********************************************************************"
		exit
	fi
done



# Ubuntu Mate for RPi has raspi-config too
command -v raspi-config &> /dev/null
if [ $? -eq 0 ]
then
	# Enable i2c
	sudo raspi-config nonint do_i2c 0
fi

# Added to enabled NVMe for pi5
set_nvme_default

# Fan Setup
basename="argononeup"
daemonname=$basename"d"
eepromrpiscript="/usr/bin/rpi-eeprom-config"
eepromconfigscript=$INSTALLATIONFOLDER/${basename}-eepromconfig.py
daemonscript=$INSTALLATIONFOLDER/$daemonname.py
daemonservice=/lib/systemd/system/$daemonname.service

if [ -f "$eepromrpiscript" ]
then
	# EEPROM Config Script
	sudo wget $ARGONDOWNLOADSERVER/scripts/argon-rpi-eeprom-config-psu.py -O $eepromconfigscript --quiet
	sudo chmod 755 $eepromconfigscript
fi

# Daemon/Service Files
sudo wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.py -O $daemonscript --quiet
sudo wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.service -O $daemonservice --quiet
sudo chmod 644 $daemonservice

# Battery Images
if [ ! -d "$INSTALLATIONFOLDER/ups" ]
then
	sudo mkdir $INSTALLATIONFOLDER/ups
fi
sudo wget $ARGONDOWNLOADSERVER/ups/upsimg.tar.gz -O $INSTALLATIONFOLDER/ups/upsimg.tar.gz --quiet
sudo tar xfz $INSTALLATIONFOLDER/ups/upsimg.tar.gz -C $INSTALLATIONFOLDER/ups/
sudo rm -Rf $INSTALLATIONFOLDER/ups/upsimg.tar.gz


# Other utility scripts
sudo wget $ARGONDOWNLOADSERVER/scripts/argondashboard.py -O $INSTALLATIONFOLDER/argondashboard.py --quiet
sudo wget $ARGONDOWNLOADSERVER/scripts/argonpowerbutton.py -O $INSTALLATIONFOLDER/argonpowerbutton.py --quiet

sudo wget $ARGONDOWNLOADSERVER/scripts/argon-versioninfo.sh -O $versioninfoscript --quiet
sudo chmod 755 $versioninfoscript

sudo wget $ARGONDOWNLOADSERVER/scripts/argonsysinfo.py -O $INSTALLATIONFOLDER/argonsysinfo.py --quiet

sudo wget $ARGONDOWNLOADSERVER/scripts/argonregister-v1.py -O $INSTALLATIONFOLDER/argonregister.py --quiet


# Argon Uninstall Script
sudo wget $ARGONDOWNLOADSERVER/scripts/argon-uninstall.sh -O $uninstallscript --quiet
sudo chmod 755 $uninstallscript

# Argon Config Script
if [ -f $configscript ]; then
	sudo rm $configscript
fi
sudo touch $configscript

# To ensure we can write the following lines
sudo chmod 666 $configscript

echo '#!/bin/bash' >> $configscript

echo 'echo "--------------------------"' >> $configscript
echo 'echo "Argon Configuration Tool"' >> $configscript
echo "$versioninfoscript simple" >> $configscript
echo 'echo "--------------------------"' >> $configscript

echo 'get_number () {' >> $configscript
echo '	read curnumber' >> $configscript
echo '	if [ -z "$curnumber" ]' >> $configscript
echo '	then' >> $configscript
echo '		echo "-2"' >> $configscript
echo '		return' >> $configscript
echo '	elif [[ $curnumber =~ ^[+-]?[0-9]+$ ]]' >> $configscript
echo '	then' >> $configscript
echo '		if [ $curnumber -lt 0 ]' >> $configscript
echo '		then' >> $configscript
echo '			echo "-1"' >> $configscript
echo '			return' >> $configscript
echo '		elif [ $curnumber -gt 100 ]' >> $configscript
echo '		then' >> $configscript
echo '			echo "-1"' >> $configscript
echo '			return' >> $configscript
echo '		fi	' >> $configscript
echo '		echo $curnumber' >> $configscript
echo '		return' >> $configscript
echo '	fi' >> $configscript
echo '	echo "-1"' >> $configscript
echo '	return' >> $configscript
echo '}' >> $configscript
echo '' >> $configscript

echo 'mainloopflag=1' >> $configscript
echo 'while [ $mainloopflag -eq 1 ]' >> $configscript
echo 'do' >> $configscript
echo '	echo' >> $configscript
echo '	echo "Choose Option:"' >> $configscript


echo '	echo "  1. Get Battery Status"' >> $configscript
uninstalloption="3"

statusoption=$(($uninstalloption-1))
echo "	echo \"  $statusoption. Dashboard\"" >> $configscript

echo "	echo \"  $uninstalloption. Uninstall\"" >> $configscript
echo '	echo ""' >> $configscript
echo '	echo "  0. Exit"' >> $configscript
echo "	echo -n \"Enter Number (0-$uninstalloption):\"" >> $configscript
echo '	newmode=$( get_number )' >> $configscript



echo '	if [ $newmode -eq 0 ]' >> $configscript
echo '	then' >> $configscript
echo '		echo "Thank you."' >> $configscript
echo '		mainloopflag=0' >> $configscript
echo '	elif [ $newmode -eq 1 ]' >> $configscript
echo '	then' >> $configscript

# Option 1
echo "		$pythonbin $daemonscript GETBATTERY" >> $configscript

# Standard options
echo "	elif [ \$newmode -eq $statusoption ]" >> $configscript
echo '	then' >> $configscript
echo "		$pythonbin $argondashboardscript" >> $configscript

echo "	elif [ \$newmode -eq $uninstalloption ]" >> $configscript
echo '	then' >> $configscript
echo "		$uninstallscript" >> $configscript
echo '		mainloopflag=0' >> $configscript
echo '	fi' >> $configscript
echo 'done' >> $configscript

sudo chmod 755 $configscript

# Desktop Icon
destfoldername=$USERNAME
if [ -z "$destfoldername" ]
then
	destfoldername=$USER
fi
if [ -z "$destfoldername" ]
then
	destfoldername="pi"
fi

shortcutfile="/home/$destfoldername/Desktop/argononeup.desktop"
if [ -d "/home/$destfoldername/Desktop" ]
then
	terminalcmd="lxterminal --working-directory=/home/$destfoldername/ -t"
	if  [ -f "/home/$destfoldername/.twisteros.twid" ]
	then
		terminalcmd="xfce4-terminal --default-working-directory=/home/$destfoldername/ -T"
	fi
	imagefile=argon40.png
	sudo wget http://files.iamnet.com.ph/argon/setup/$imagefile -O /etc/argon/$imagefile --quiet
	if [ -f $shortcutfile ]; then
		sudo rm $shortcutfile
	fi

	# Create Shortcuts
	echo "[Desktop Entry]" > $shortcutfile
	echo "Name=Argon Configuration" >> $shortcutfile
	echo "Comment=Argon Configuration" >> $shortcutfile
	echo "Icon=/etc/argon/$imagefile" >> $shortcutfile
	echo 'Exec='$terminalcmd' "Argon Configuration" -e '$configscript >> $shortcutfile
	echo "Type=Application" >> $shortcutfile
	echo "Encoding=UTF-8" >> $shortcutfile
	echo "Terminal=false" >> $shortcutfile
	echo "Categories=None;" >> $shortcutfile
	chmod 755 $shortcutfile
fi

configcmd="$(basename -- $configscript)"

if [ "$setupmode" = "Setup" ]
then
	if [ -f "/usr/bin/$configcmd" ]
	then
		sudo rm /usr/bin/$configcmd
	fi
	sudo ln -s $configscript /usr/bin/$configcmd

	# Enable and Start Service(s)
	sudo systemctl daemon-reload
	sudo systemctl enable argononeupd.service
	sudo systemctl start argononeupd.service
else
	sudo systemctl daemon-reload
	sudo systemctl restart argononeupd.service
fi

if [ "$CHECKPLATFORM" = "Raspbian" ]
then
	if [ -f "$eepromrpiscript" ]
	then
		sudo apt-get update && sudo apt-get upgrade -y
		sudo rpi-eeprom-update
		# EEPROM Config Script
		sudo $eepromconfigscript
	fi
else
	echo "WARNING: EEPROM not updated.  Please run this under Raspberry Pi OS"
fi


echo "*********************"
echo "  $setupmode Completed "
echo "*********************"
$versioninfoscript
echo
echo "Use '$configcmd' to configure device"
echo



if [ $NEEDSTIMESYNC -eq 1 ]
then
	argon_time_error
fi

