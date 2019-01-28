# HPM Downloader

## Compiling

Needed library:

    - libssl-dev

To compile, just run `make` and it should run without problems. The executable can be found at `<hpm-downloader>/bin/hpm-downloader`

## Usage

In order to program a remote AMC board using HPM, one has to specify the MCH address, the board slot and the path to the binary image. For example:

    ./bin/hpm-downloader --ip <mch_ip> --slot 9 <path_to_image>

The software will use a set of default options that can be changed with other inline options (use the flag `-h` for more info).

To program multiple boards at once with the same image, just list them separated by commas in the option `--slot`, for example:

    ./bin/hpm-downloader --ip <mch_ip> --slot 2,3,4,5,6,7,8,9,10 <path_to_image>

Or just use the option `--slot all` to program all 12 slots available in the MTCA crate (if any of the board fails the programming procedure, it will be reported in stdout)


**IMPORTANT NOTE**: The default options were designed to match LNLS' AFC board information. If you wish to use this to program different board, you'll have to match the `IANA Manufacturer Code` and `Product ID` options to your hardware. They must have the same value as those reported by the command `IPMI_GET_DEVICE_ID_CMD`.
