#!/bin/sh
echo
echo 'Collecting Modules and saving to modulecollect folder'
echo
if [ -d modulecollect ]
then
  echo modulecollect folder already exists...
else
  echo Creating folder...
  mkdir modulecollect
fi
echo
echo 'Copying files now'
cp -fr drivers/net/wireless/bcm4329_204/bcm4329.ko modulecollect
cp -fr drivers/media/common/tuners/mxl5005s.ko modulecollect
cp -fr drivers/media/common/tuners/mxl5007t.ko modulecollect
cp -fr drivers/media/common/tuners/qt1010.ko modulecollect
cp -fr drivers/media/common/tuners/tda827x.ko modulecollect
cp -fr drivers/media/common/tuners/tda18271.ko modulecollect
cp -fr drivers/media/common/tuners/mt2060.ko modulecollect
cp -fr drivers/media/common/tuners/mt2131.ko modulecollect
cp -fr drivers/media/common/tuners/mt2266.ko modulecollect
echo
echo 'Done!'