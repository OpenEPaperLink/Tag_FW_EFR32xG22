#!/usr/bin/python3

"""
From the compiled hardware variants bootloaders,
generate device-specific bootloaders.
"""

import argparse, bincopy, os, binascii

variant_map = [
    {
        "variant_name": "BRD4402B_WSTK",
        "variant_base": "BRD4402B_SPIF_BTL.s37",
        "variant_idbyte": "0x01"
    },
    {
        "variant_name": "BRD4402B_WSTK_EPD",
        "variant_base": "BRD4402B_SPIF_BTL.s37",
        "variant_idbyte": "0x02"
    },
    {
        "variant_name": "SOLUM_AUTODETECT",
        "variant_base": "Solum_SPIF_BTL.s37",
        "variant_idbyte": "0x03"
    },
    {
        "variant_name": "DISPLAYDATA_SUFIFT24PL4A",
        "variant_base": "Displaydata_SPIF_BTL.s37",
        "variant_idbyte": "0x04"
    },
    {
        "variant_name": "DISPLAYDATA_SUFIFT27PL4A",
        "variant_base": "Displaydata_SPIF_BTL.s37",
        "variant_idbyte": "0x05"
    },
    {
        "variant_name": "CUSTOM_9_7",
        "variant_base": "Custom_SPIF_BTL.s37",
        "variant_idbyte": "0x06",
        "optional": 1
    },
    {
        "variant_name": "MODCHIP_HD150",
        "variant_base": "Modchip_SPIF_BTL.s37",
        "variant_idbyte": "0x07"
    },
    {
        "variant_name": "DEVELOPMENT_0",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF0",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_1",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF1",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_2",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF2",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_3",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF3",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_4",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF4",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_5",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF5",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_6",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF6",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_7",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF7",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_8",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF8",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_9",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xF9",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_A",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFA",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_B",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFB",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_C",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFC",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_D",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFD",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_E",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFE",
        "optional": 1
    },
    {
        "variant_name": "DEVELOPMENT_F",
        "variant_base": "Development_SPIF_BTL.s37",
        "variant_idbyte": "0xFF",
        "optional": 1
    }
]

def patch_idbyte(binfile, idbyte):
    image_base = int(binfile.minimum_address)
    app_properties_p = int.from_bytes(binfile[image_base+13*4:image_base+14*4], byteorder="little")

    if app_properties_p < binfile.minimum_address or app_properties_p > binfile.maximum_address - 40:
        print("Error: bootloader info struct pointer out of bounds")
        exit(-1)
    
    magic1 = int.from_bytes(binfile[app_properties_p:app_properties_p+4], byteorder="little")
    if magic1 != 0xfa79b713:
        print("Error: magic word mismatch - this isn't an info struct")
        exit(-1)
    
    structver = int.from_bytes(binfile[app_properties_p + 16:app_properties_p + 20], byteorder="little")
    if structver != 0x0201:
        print("Error: info struct version mismatch - check layout and add support for rev {}".format(hex(structver)))
        exit(-1)

    apptype = int.from_bytes(binfile[app_properties_p + 28:app_properties_p + 32], byteorder="little")
    if apptype & 0xFF != 0x40:
        print("Error: application type {} does not match a bootloader (0x40)".format(hex(apptype)))

    btlver = int.from_bytes(binfile[app_properties_p + 32:app_properties_p + 36], byteorder="little")
    btlver_patched = (btlver & 0xFFFFFF00) + int(idbyte, base=16)
    print("Patching version {} to {}".format(hex(btlver), hex(btlver_patched)))

    binfile.add_binary(btlver_patched.to_bytes(4, byteorder="little"), address=app_properties_p + 32, overwrite=True)
    return binfile

def main(args):
    if args.list:
        for variant in variant_map:
            print("{} ({})".format(variant["variant_name"], variant["variant_idbyte"]))
        exit(0)
    
    variants_to_generate = []
    if ',' in args.targets:
        print("comma-separated list detected")
        variants_to_generate = args.targets.split(',')
    elif args.targets == "all":
        print("generating all known variants")
        for variant in variant_map:
            variants_to_generate.append(str(variant["variant_name"]))
    else:
        print("generating single target")
        variants_to_generate.append(str(args.targets))
    
    if not os.path.isdir(args.indir):
        print("Error: couldn't find input directory")
        exit(-1)

    if not os.path.isdir(args.outdir):
        os.mkdir(args.outdir)
        
    for variant in variants_to_generate:
        found = False
        for candidate in variant_map:
            if candidate["variant_name"] == variant:
                found = True

                if not os.path.isfile(os.path.join(args.indir, candidate["variant_base"])):
                    if "optional" not in candidate.keys():
                        print("Error: couldn't find input file")
                        exit(-1)
                    else:
                        print("Skipping {} (no input file found)".format(variant))
                        continue

                infile = bincopy.BinFile(os.path.join(args.indir, candidate["variant_base"]))
                if not infile:
                    if "optional" not in candidate.keys():
                        print("Error: couldn't open input file")
                        exit(-1)
                
                print("Generating file for {}".format(variant))
                outfile = patch_idbyte(infile, candidate["variant_idbyte"])
                f = open(os.path.join(args.outdir, variant + ".s37"), "w")
                f.write(outfile.as_srec())
                f.close()

                break
        if not found:
            print("Error: requested variant {} unknown".format(variant))
            exit(-1)

if __name__ == "__main__":
    """ This is executed when run from the command line """
    parser = argparse.ArgumentParser()

    parser.add_argument("-l", "--list", action="store_true", default=False, help="List the supported targets")
    parser.add_argument("-g", "--generate", action="store", dest="targets", default="all", help="comma-separated list of variants to generate. Defaults to 'all'")
    parser.add_argument("-o", "--outdir", action="store", dest="outdir", default="./variant_binaries", help="Output directory for the generated variant bootloaders")
    parser.add_argument("-i", "--indir", action="store", dest="indir", default="./base_binaries", help="Input directory for the compiled hardware variant bootloaders")

    args = parser.parse_args()
    main(args)