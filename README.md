# Kitzune - the world's most MP3 player

Hopefully, it even works!

Dependencies:
* the latest version of the esp-idf v5.3.x series - tested with https://github.com/espressif/esp-idf/releases/tag/v5.3.1
  * Checkout using the steps in the release document - `git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.3.1`
  * Install this as per the instructions here - https://docs.espressif.com/projects/esp-idf/en/v5.3.1/get-started/index.html - but in my case this simply involved entering the esp-idf-v5.3.1 directory and running `./install.sh all`
* the esp-adf, v2.7 - https://github.com/espressif/esp-adf/releases/tag/v2.7
  * Checkout using this command - `git clone -b v2.7 --recursive https://github.com/espressif/esp-adf.git esp-adf-v2.7`
  * Do not use/install the esp-idf submodule included with the ADF

Once you have installed the dependencies above, you will need to patch the IDF:
```
cd /path/to/esp-idf-v5.3.1
git apply /path/to/esp-adf-v2.7/idf_patches/idf_v5.3_freertos.patch
git apply /path/to/kitzune/esp-idf-v5.3.patch
```

You will also need to patch the ADF:
```
cd /path/to/esp-adf-v2.7
git apply /path/to/kitzune/esp-adf-v2.7.patch
```

Now everything should be setup! Now you should be able to run:
```
source /path/to/esp-idf-v5.3.1/export.sh
export ADF_PATH=/path/to/esp-adf-v2.7
cd /path/to/kitzune/
idf.py build
```

And if all went well, you should get the expected binary!

Flash it with:
```
idf.py flash
```
