ARGONDOWNLOADSERVER=http://files.iamnet.com.ph/argon/setup
INSTALLATIONFOLDER=.
CHECKGPIOMODE="libgpiod"
basename="argononeup"
daemonname=$basename"d"
versioninfoscript=$INSTALLATIONFOLDER/argon-versioninfo.sh
uninstallscript=$INSTALLATIONFOLDER/argon-uninstall.sh
configscript=$INSTALLATIONFOLDER/argon-config
argondashboardscript=$INSTALLATIONFOLDER/argondashboard.py
eepromconfigscript=$INSTALLATIONFOLDER/${basename}-eepromconfig.py
daemonscript=$INSTALLATIONFOLDER/$daemonname.py
daemonservice=./$daemonname.service
userdaemonservice=./${daemonname}user.service
lidconfigscript=$INSTALLATIONFOLDER/${basename}-lidconfig.sh
imagefile=argon40.png
daemonconfigfile=$daemonname.conf

wget $ARGONDOWNLOADSERVER/argononeup.sh -O argononeup.sh
wget $ARGONDOWNLOADSERVER/scripts/argononeup-lidconfig.sh -O $lidconfigscript
wget $ARGONDOWNLOADSERVER/scripts/argon-rpi-eeprom-config-psu.py -O $eepromconfigscript
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.py -O $daemonscript 
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}.service -O $daemonservice 
wget $ARGONDOWNLOADSERVER/scripts/${daemonname}user.service -O $userdaemonservice 
mkdir -p $INSTALLATIONFOLDER/ups
wget $ARGONDOWNLOADSERVER/ups/upsimg.tar.gz -O $INSTALLATIONFOLDER/ups/upsimg.tar.gz 
tar xfz $INSTALLATIONFOLDER/ups/upsimg.tar.gz -C $INSTALLATIONFOLDER/ups/
rm -Rf $INSTALLATIONFOLDER/ups/upsimg.tar.gz
wget "$ARGONDOWNLOADSERVER/scripts/argonpowerbutton-${CHECKGPIOMODE}.py" -O $INSTALLATIONFOLDER/argonpowerbutton.py 
wget $ARGONDOWNLOADSERVER/scripts/argonkeyboard.py -O $INSTALLATIONFOLDER/argonkeyboard.py 
wget $ARGONDOWNLOADSERVER/scripts/argondashboard.py -O $INSTALLATIONFOLDER/argondashboard.py 
wget $ARGONDOWNLOADSERVER/scripts/argon-versioninfo.sh -O $versioninfoscript 
wget $ARGONDOWNLOADSERVER/scripts/argonsysinfo.py -O $INSTALLATIONFOLDER/argonsysinfo.py 
wget $ARGONDOWNLOADSERVER/scripts/argonregister-v1.py -O $INSTALLATIONFOLDER/argonregister.py 
wget $ARGONDOWNLOADSERVER/scripts/argon-uninstall.sh -O $uninstallscript 
wget $ARGONDOWNLOADSERVER/argon40.png -O ./argon40.png 
