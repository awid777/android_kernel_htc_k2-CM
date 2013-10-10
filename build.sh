#!/bin/bash
make -j4 zImage;
make -j4 modules;
make modules_install;
MOD_DIR=zmodules_ready;
if [ ! -f $MOD_DIR ]; then
mkdir zmodules_ready
fi
mkdir tempo;
cp -r /lib64/modules/3.4.49.-ragarda/kernel tempo/;
rm -rf /lib64/modules/3.4.49.-ragarda;
find tempo/kernel/ -name '*.ko' -exec cp --target-directory=zmodules_ready {} \;
rm -rf tempo;
cd zmodules_ready;
arm-eabi-strip --strip-unneeded *.ko;


