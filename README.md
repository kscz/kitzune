# Kitzune - the world's most MP3 player

Hopefully, it even works!

Dependencies:
* the latest version of the esp-idf v5.1.x series - tested with https://github.com/espressif/esp-idf/releases/tag/v5.1.4
  * Checkout using the steps in the release document - `git clone -b v5.1.4 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.1.4`
  * Install this as per the instructions here - https://docs.espressif.com/projects/esp-idf/en/v5.1.4/get-started/index.html - but in my case this simply involved entering the esp-idf-v5.1.4 directory and running `./install.sh all`
* the esp-adf, v2.6 - https://github.com/espressif/esp-adf/releases/tag/v2.6
  * Checkout using this command - `git clone -b v2.6 --recursive https://github.com/espressif/esp-adf.git esp-adf-v2.6`
  * Do not use/install the esp-idf-v4.4 submodule included with the ADF

Once you have installed the dependencies above, you will need to patch the IDF:
```
cd /path/to/esp-idf-v5.1.4
git apply /path/to/esp-adf-v2.6/idf_patches/idf_v5.1_freertos.patch
git apply /path/to/kitzune/esp-idf-v5.1.patch
```

Now everything should be setup! Now you should be able to run:
```
source /path/to/esp-idf-v5.1.4/export.sh
export ADF_PATH=/path/to/esp-adf-v2.6
cd /path/to/kitzune/
idf.py build
```

And if all went well, you should get the expected binary!
