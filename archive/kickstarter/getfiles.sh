#!/bin/bash

ARGONDOWNLOADSERVER=https://download.argon40.com

INSTALLATIONFOLDER=.
versioninfoscript=$INSTALLATIONFOLDER/argon-verioninfo.sh
uninstallscript=$INSTALLATIONFOLDER/argon-uninstall.sh
configscript=$INSTALLATIONFOLDER/argon-config
argondashboardscript=$INSTALLATIONFOLDER/argondashboard.py

basename="argononeup"
daemonname=$basename"d"
eepromrpiscript="/usr/bin/rpi-eeprom-config"
eepromconfigscript=$INSTALLATIONFOLDER/${basename}-eepromconfig.py
daemonscript=$INSTALLATIONFOLDER/$daemonname.py
daemonservice=$INSTALLATIONFOLDER/$daemonname.service
userdaemonservice=$INSTALLATIONFOLDER/${daemonname}user.service
daemonconfigfile=$INSTALLATIONFOLDER/$daemonname.conf

lidconfigscript=$INSTALLATIONFOLDER/${basename}-lidconfig.sh
imagefile=argon40.png


wget $ARGONDOWNLOADSERVER/scripts/argononeup-lidconfig.sh -O $lidconfigscript --quiet
wget $ARGONDOWNLOADSERVER/scripts/argon-rpi-eeprom-config-psu.py -O $eepromconfigscript --quiet
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.py -O $daemonscript --quiet
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.service -O $daemonservice --quiet
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}user.service -O $userdaemonservice --quiet
wget $ARGONDOWNLOADSERVER/ups/upsimg.tar.gz -O $INSTALLATIONFOLDER/ups/upsimg.tar.gz --quiet
tar xfz $INSTALLATIONFOLDER/ups/upsimg.tar.gz -C $INSTALLATIONFOLDER/ups/
rm -Rf $INSTALLATIONFOLDER/ups/upsimg.tar.gz
wget "$ARGONDOWNLOADSERVER/scripts/argonpowerbutton-rpigpio.py" -O $INSTALLATIONFOLDER/argonpowerbutton_rpigpio.py --quiet
wget "$ARGONDOWNLOADSERVER/scripts/argonpowerbutton-libgpiod.py" -O $INSTALLATIONFOLDER/argonpowerbutton_libgpiod.py --quiet
wget $ARGONDOWNLOADSERVER/scripts/argonkeyboard.py -O $INSTALLATIONFOLDER/argonkeyboard.py --quiet
wget $ARGONDOWNLOADSERVER/scripts/argondashboard.py -O $INSTALLATIONFOLDER/argondashboard.py --quiet
wget $ARGONDOWNLOADSERVER/scripts/argon-versioninfo.sh -O $versioninfoscript --quiet
wget $ARGONDOWNLOADSERVER/scripts/argonsysinfo.py -O $INSTALLATIONFOLDER/argonsysinfo.py --quiet
wget $ARGONDOWNLOADSERVER/scripts/argonregister-v1.py -O $INSTALLATIONFOLDER/argonregister.py --quiet
wget $ARGONDOWNLOADSERVER/scripts/argon-uninstall.sh -O $uninstallscript --quiet
wget https://download.argon40.com/$imagefile -O /etc/argon/$imagefile --quiet
