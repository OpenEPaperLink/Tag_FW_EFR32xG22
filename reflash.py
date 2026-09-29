import argparse
import bincopy
import os
import sys
import time

from pyocd.core.exceptions import TargetSupportError
from pyocd.core.helpers import ConnectHelper
from pyocd.core.session import Session
from pyocd.core.target import Target
from pyocd.subcommands import pack_cmd
from pyocd.flash.file_programmer import FileProgrammer

import sys

# Documentation: https://docs.silabs.com/shared-content/latest/efr32-dci-swd-programming/03-debug-challenge-interface-dci
class DCI:
  def __init__(self, session: Session) -> None:
    self.session = session

    idr = session.board.target.dp.read_ap(0x010000fc)
    if idr != 0x54770002:
      raise NotImplementedError("Is this an EFR32xG2x device with DCI?")

  def _status(self) -> int:
    self.session.board.target.dp.write_ap(0x01000004, 0x00001008)
    return self.session.board.target.dp.read_ap(0x0100000c)

  def _can_write(self) -> bool:
    return (self._status() & 0x1) == 0

  def _can_read(self) -> bool:
    return (self._status() & 0x100) != 0

  def _read(self, verbose : bool = False) -> int:
    self.session.board.target.dp.write_ap(0x01000004, 0x00001004)
    val = self.session.board.target.dp.read_ap(0x0100000c)
    if verbose:
      print(f"DCI read: {hex(val)}")
    return val

  def _write(self, data : int, verbose : bool = False) -> None:
    if verbose:
      print(f"DCI write: {hex(data)}")
    self.session.board.target.dp.write_ap(0x01000004, 0x00001000)
    self.session.board.target.dp.write_ap(0x0100000c, data)

  def execute_command(self, command_id : int, command_payload : list[int] | None = None, timeout : int = 400, verbose : bool = False) -> list[int]:
    expiry = time.time_ns() + timeout * 1000 * 1000

    cmd = [0, command_id]
    if command_payload:
      cmd.extend(command_payload)
    cmd[0] = len(cmd) * 4

    for dw in cmd:
      while not self._can_write():
        time.sleep(0.001)
        if time.time_ns() > expiry:
          raise TimeoutError("Timed out writing DCI command")
      self._write(dw, verbose=verbose)

    while not self._can_read():
      time.sleep(0.001)
      if time.time_ns() > expiry:
        raise TimeoutError("Timed out reading DCI command")
    rsp = self._read(verbose=verbose)
    extra_words = ((rsp & 0xFFFF) // 4) - 1
    rspcode = rsp >> 16
    if rspcode != 0:
      raise RuntimeError(f"DCI command returned response {rspcode}")

    rsp = []

    for w in range(extra_words):
      while not self._can_read():
        time.sleep(0.001)
        if time.time_ns() > expiry:
          raise TimeoutError("Timed out reading DCI command")
      rsp.append(self._read(verbose=verbose))

    return rsp


def get_session(device : str, detect_cores : bool, adapter : str | None, list_adapters : bool = False, verbose : bool = False) -> Session | None:
  # Start by figuring out how to connect
  probes = ConnectHelper.get_all_connected_probes(blocking=False)
  if list_adapters or verbose:
    print("Detected adapters:")
    for probe in probes:
      print(f"\tID {probe.unique_id} - {probe.description}")
    if len(probes) == 0:
      print("\tNo adapters found")

    if list_adapters:
      return None

  if len(probes) == 0:
    raise KeyError("No PyOCD adapters connected to this system")

  probe = None
  if len(probes) == 1:
    probe = probes[0]
  elif not adapter:
    raise ValueError("More than 1 adapter detected, but no adapter specified")
  else:
    for candidate in probes:
      if candidate.unique_id == adapter:
        probe = candidate

  if not probe:
    raise KeyError(f"Probe with ID {adapter} not connected")

  # Try to open a session with the target, and install pack support if needed
  options = {
    # Some APs are regarded as nonconforming by PyOCD, so tell it to stick to AP0 on error
    'adi.v5.max_invalid_ap_count': 0,
    'scan_all_aps': False,
    'target_override': device,
    'allow_no_cores': not detect_cores
  }

  if detect_cores:
    options['jlink.device'] = device
  try:
    return Session(probe, options=options)
  except TargetSupportError:
    print("Target support not found, trying to automatically install...")
    args = argparse.Namespace(
      update=True,
      patterns=["{}*".format(a.device[:9].upper())],
      verbose=0,
      quiet=0,
      clean=False,
      no_download=False
    )
    cmd = pack_cmd.PackInstallSubcommand(args)
    cmd.invoke()
    print("Retrying...")
    return Session(probe, options=options)


def reset_target(session : Session) -> None:
  session.probe.open()
  time.sleep(0.2)
  session.probe.assert_reset(True)
  time.sleep(0.1)
  session.probe.assert_reset(False)
  time.sleep(0.2)
  session.probe.close()


def main(argv):
  # Configure the argument parser
  parser = argparse.ArgumentParser(description="PyOCD-based flashing, erasing and debug-unlocking of EFR32xG2x devices")
  parser.add_argument('-s', '--status',
                      action='store_true',
                      help="Print current status of the device")
  parser.add_argument('-u', '--unlock',
                      action = 'store_true',
                      help="Perform a debug unlock (will erase everything except UD)")
  parser.add_argument('-e', '--erase',
                      action='store_true',
                      help="Erase the flash content (except UD) before writing the new firmware")
  parser.add_argument('--dump-ud',
                      action='store_true',
                      help="Read the contents of the UD area")
  parser.add_argument('-f', '--firmware',
                      type=str,
                      required=False,
                      help="Firmware file to flash (path to binary or 'latest' for the highest version in the repo)")
  parser.add_argument('--firmware-variant',
                      type=str,
                      default="SOLUM_AUTODETECT_FULL",
                      help="Firmware variant to flash when using 'latest'")
  parser.add_argument('-d', '--device',
                      type=str,
                      default="EFR32BG22C224F512IM40",
                      help="The device part number we'll be interacting with")
  parser.add_argument('-a', '--adapter',
                      type=str,
                      required=False,
                      help="Adapter serial number to use (if more than 1 connected)")
  parser.add_argument('-l', '--list-adapters',
                      action='store_true',
                      help="List all detected PyOCD-compatible adapters and exit")
  parser.add_argument('-v', '--verbose',
                      action='store_true',
                      help="Print verbose output")
  a = parser.parse_args(argv)

  session = get_session(a.device, False, a.adapter, list_adapters=a.list_adapters, verbose=a.verbose)

  if not session:
    return 0

  # Should have a session now, check whether the DCI AP is alive
  with session:
    dci = DCI(session)

    # Start with asking for status if requested
    if a.status:
      if a.verbose:
        print("Requesting status")
      s = dci.execute_command(0xFE010000, verbose=a.verbose)
      if len(s) > 5:
        s = s[4:]
      print(f"Device status: {hex(s[0])}")
      print(f"Device family: EFR32xG2{hex((s[1] >> 24)+1)[2:]}")
      print(f"SE FW version {hex(s[1] & 0xFFFFFF)}")
      dbglock = s[3]

      if dbglock == 0:
        print("No debug locks applied - flashing can proceed")
      if dbglock & 0x1:
        print("Debug lock is applied - flashing not possible without unlock")
      if dbglock & 0x2:
        print("Debug unlock is allowed")
      if dbglock & 0x4:
        print("Authenticated unlock is allowed - not supported by this script, and you'd need the key")

    if a.unlock or a.erase:
      if a.verbose:
        print("Issuing unlock & erase command")
      dci.execute_command(0x430f0000, timeout=2000, verbose=a.verbose)

  reset_target(session)

  # Create new session, now we should be able to detect the cores, otherwise we won't be able to flash
  session = get_session(a.device, True, a.adapter, verbose=a.verbose)
  with session:
    if a.dump_ud:
      ud_content = session.target.read_memory_block32(0x0fe00000, 0x100)
      print("Content of UD:")
      print("      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F")
      line = "000: "
      for i in range(len(ud_content)):
        if i > 0 and i % 4 == 0:
          print(line)
          line = "{:03x}: ".format(i * 4)
        w = ud_content[i].to_bytes(length=4, byteorder="little")
        for b in w:
          line += "{:02x} ".format(b)

    if a.firmware:
      if a.firmware == "latest":
        fwdir = os.path.join(os.path.dirname(__file__), "full_binaries")
        fwdirs = sorted(os.listdir(fwdir))
        if a.verbose:
          print(f"Content of fw folder: {fwdirs}")
        versions = []
        for d in fwdirs:
          if d.startswith('v') and os.path.isdir(os.path.join(fwdir, d)):
            versions.append(int(d[1:]))
        versions = sorted(versions)
        if a.verbose:
          print(f"Detected versions: {versions}")

        fwpath = os.path.join(fwdir, f"v{versions[-1]}", f"{a.firmware_variant}_v{versions[-1]}.s37")
      else:
        fwpath = a.firmware

      if not os.path.isfile(fwpath):
        raise FileNotFoundError(f"Couldn't find {fwpath}")
      if a.verbose:
        print(f"Flashing firmware {fwpath}")

      converted = False
      if fwpath[-4:] == ".s37":
        hexpath = fwpath[:-4] + ".hex"
        if not os.path.exists(hexpath):
          # Need to convert srec to hex for PyOCD
          content = bincopy.BinFile(fwpath)
          with open(hexpath, "w") as f:
            f.write(content.as_ihex())
        else:
          content = bincopy.BinFile(hexpath)
        min_address = content.minimum_address
        converted = True
      elif fwpath[-4:] == ".hex":
        content = bincopy.BinFile(fwpath)
        min_address = content.minimum_address
        hexpath = fwpath
      else:
        raise ValueError("Unsupported file type (only .hex or .s37 files are supported): {}".format(fwpath))

      try:
        programmer = FileProgrammer(session, no_reset=True)
        programmer.program(hexpath,
                           base_address=None,
                           skip=False,
                           file_format=None)
        time.sleep(0.1)
      finally:
        if converted:
          os.remove(hexpath)

      print(f"Wrote firmware {fwpath} at 0x{min_address:08x}")

  reset_target(session)

  return 0

# Call main if necessary
if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
from pyocd.flash.file_programmer import FileProgrammer
from pyocd.subcommands import pack_cmd


# Documentation:
# https://docs.silabs.com/shared-content/latest/efr32-dci-swd-programming/03-debug-challenge-interface-dci
class DCI:
    def __init__(self, session: Session) -> None:
        self.session = session

        idr = session.board.target.dp.read_ap(0x010000FC)
        if idr != 0x54770002:
            raise NotImplementedError(
                "Is this an EFR32xG2x device with DCI?"
            )

    def _status(self) -> int:
        self.session.board.target.dp.write_ap(0x01000004, 0x00001008)
        return self.session.board.target.dp.read_ap(0x0100000C)

    def _can_write(self) -> bool:
        return (self._status() & 0x1) == 0

    def _can_read(self) -> bool:
        return (self._status() & 0x100) != 0

    def _read(self, verbose: bool = False) -> int:
        self.session.board.target.dp.write_ap(0x01000004, 0x00001004)
        value = self.session.board.target.dp.read_ap(0x0100000C)

        if verbose:
            print(f"DCI read: {hex(value)}")

        return value

    def _write(self, data: int, verbose: bool = False) -> None:
        if verbose:
            print(f"DCI write: {hex(data)}")

        self.session.board.target.dp.write_ap(0x01000004, 0x00001000)
        self.session.board.target.dp.write_ap(0x0100000C, data)

    def execute_command(
        self,
        command_id: int,
        command_payload: list[int] | None = None,
        timeout: int = 400,
        verbose: bool = False,
    ) -> list[int]:
        expiry = time.time_ns() + timeout * 1000 * 1000

        command = [0, command_id]

        if command_payload:
            command.extend(command_payload)

        command[0] = len(command) * 4

        for word in command:
            while not self._can_write():
                time.sleep(0.001)

                if time.time_ns() > expiry:
                    raise TimeoutError("Timed out writing DCI command")

            self._write(word, verbose=verbose)

        while not self._can_read():
            time.sleep(0.001)

            if time.time_ns() > expiry:
                raise TimeoutError("Timed out reading DCI command")

        response_header = self._read(verbose=verbose)
        extra_words = ((response_header & 0xFFFF) // 4) - 1
        response_code = response_header >> 16

        if response_code != 0:
            raise RuntimeError(
                f"DCI command returned response {response_code}"
            )

        response = []

        for _ in range(extra_words):
            while not self._can_read():
                time.sleep(0.001)

                if time.time_ns() > expiry:
                    raise TimeoutError("Timed out reading DCI command")

            response.append(self._read(verbose=verbose))

        return response


def get_session(
    device: str,
    detect_cores: bool,
    adapter: str | None,
    pack: str | None = None,
    list_adapters: bool = False,
    verbose: bool = False,
) -> Session | None:
    probes = ConnectHelper.get_all_connected_probes(blocking=False)

    if list_adapters or verbose:
        print("Detected adapters:")

        for probe in probes:
            print(f"\tID {probe.unique_id} - {probe.description}")

        if len(probes) == 0:
            print("\tNo adapters found")

        if list_adapters:
            return None

    if len(probes) == 0:
        raise KeyError("No PyOCD adapters connected to this system")

    probe = None

    if len(probes) == 1:
        probe = probes[0]
    elif not adapter:
        raise ValueError(
            "More than 1 adapter detected, but no adapter specified"
        )
    else:
        for candidate in probes:
            if candidate.unique_id == adapter:
                probe = candidate
                break

    if not probe:
        raise KeyError(f"Probe with ID {adapter} not connected")

    options = {
        # Some APs are regarded as nonconforming by pyOCD.
        # Restrict error handling to AP0.
        "adi.v5.max_invalid_ap_count": 0,
        "scan_all_aps": False,
        "target_override": device,
        "allow_no_cores": not detect_cores,
    }

    # Load target support from a local CMSIS Device Family Pack.
    if pack:
        pack_path = os.path.abspath(os.path.expanduser(pack))

        if not os.path.isfile(pack_path):
            raise FileNotFoundError(
                f"CMSIS Device Family Pack not found: {pack_path}"
            )

        options["pack"] = pack_path

    if detect_cores:
        options["jlink.device"] = device

    try:
        return Session(probe, options=options)

    except TargetSupportError:
        # This may work for targets represented in the public pyOCD pack index.
        # For EFR32BG22, use --pack with a local Silicon Labs DFP if the index
        # does not provide a matching device.
        if pack:
            raise

        print("Target support not found, trying to automatically install...")

        args = argparse.Namespace(
            update=True,
            patterns=["{}*".format(device[:9].upper())],
            verbose=0,
            quiet=0,
            clean=False,
            no_download=False,
        )

        command = pack_cmd.PackInstallSubcommand(args)
        command.invoke()

        print("Retrying...")
        return Session(probe, options=options)


def reset_target(session: Session) -> None:
    session.probe.open()
    time.sleep(0.2)

    session.probe.assert_reset(True)
    time.sleep(0.1)

    session.probe.assert_reset(False)
    time.sleep(0.2)

    session.probe.close()


def words_to_bytes(words: list[int]) -> bytes:
    return b"".join(
        word.to_bytes(length=4, byteorder="little")
        for word in words
    )


def print_userdata(words: list[int]) -> None:
    print("Content of UD:")
    print("      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F")

    line = "000: "

    for index, word in enumerate(words):
        if index > 0 and index % 4 == 0:
            print(line)
            line = "{:03x}: ".format(index * 4)

        for byte in word.to_bytes(length=4, byteorder="little"):
            line += "{:02x} ".format(byte)

    # Important: print the final 16-byte line, including offset 3f0.
    print(line)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "PyOCD-based flashing, erasing and debug-unlocking "
            "of EFR32xG2x devices"
        )
    )

    parser.add_argument(
        "-s",
        "--status",
        action="store_true",
        help="Print current status of the device",
    )

    parser.add_argument(
        "-u",
        "--unlock",
        action="store_true",
        help="Perform a debug unlock (will erase everything except UD)",
    )

    parser.add_argument(
        "-e",
        "--erase",
        action="store_true",
        help="Erase the flash content (except UD) before writing new firmware",
    )

    parser.add_argument(
        "--dump-ud",
        action="store_true",
        help="Read and print the contents of the UserData area",
    )

    parser.add_argument(
        "--dump-ud-bin",
        type=str,
        required=False,
        help="Write the 1024-byte UserData area as raw binary to this file",
    )

    parser.add_argument(
        "-f",
        "--firmware",
        type=str,
        required=False,
        help="Firmware file to flash, or 'latest' for highest version in repo",
    )

    parser.add_argument(
        "--firmware-variant",
        type=str,
        default="SOLUM_AUTODETECT_FULL",
        help="Firmware variant to flash when using 'latest'",
    )

    parser.add_argument(
        "-d",
        "--device",
        type=str,
        default="EFR32BG22C224F512IM40",
        help="The device part number to interact with",
    )

    parser.add_argument(
        "--pack",
        type=str,
        required=False,
        help=(
            "Path to a local CMSIS Device Family Pack (.pack) "
            "used by pyOCD"
        ),
    )

    parser.add_argument(
        "-a",
        "--adapter",
        type=str,
        required=False,
        help="Adapter serial number to use if more than one is connected",
    )

    parser.add_argument(
        "-l",
        "--list-adapters",
        action="store_true",
        help="List all detected PyOCD-compatible adapters and exit",
    )

    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print verbose output",
    )

    args = parser.parse_args(argv)

    session = get_session(
        args.device,
        False,
        args.adapter,
        pack=args.pack,
        list_adapters=args.list_adapters,
        verbose=args.verbose,
    )

    if not session:
        return 0

    with session:
        dci = DCI(session)

        if args.status:
            if args.verbose:
                print("Requesting status")

            status = dci.execute_command(
                0xFE010000,
                verbose=args.verbose,
            )

            if len(status) > 5:
                status = status[4:]

            print(f"Device status: {hex(status[0])}")
            print(
                f"Device family: "
                f"EFR32xG2{hex((status[1] >> 24) + 1)[2:]}"
            )
            print(f"SE FW version {hex(status[1] & 0xFFFFFF)}")

            debug_lock = status[3]

            if debug_lock == 0:
                print("No debug locks applied - flashing can proceed")

            if debug_lock & 0x1:
                print(
                    "Debug lock is applied - flashing not possible "
                    "without unlock"
                )

            if debug_lock & 0x2:
                print("Debug unlock is allowed")

            if debug_lock & 0x4:
                print(
                    "Authenticated unlock is allowed - not supported "
                    "by this script, and requires the key"
                )

        if args.unlock or args.erase:
            if args.verbose:
                print("Issuing unlock & erase command")

            dci.execute_command(
                0x430F0000,
                timeout=2000,
                verbose=args.verbose,
            )

    reset_target(session)

    # Reopen the session with core detection enabled.
    session = get_session(
        args.device,
        True,
        args.adapter,
        pack=args.pack,
        verbose=args.verbose,
    )

    with session:
        # --dump-ud-bin also triggers UserData reading.
        if args.dump_ud or args.dump_ud_bin:
            userdata_words = session.target.read_memory_block32(
                0x0FE00000,
                0x100,
            )

            userdata_bytes = words_to_bytes(userdata_words)

            if len(userdata_bytes) != 1024:
                raise RuntimeError(
                    f"Unexpected UserData size: {len(userdata_bytes)} bytes"
                )

            if args.dump_ud:
                print_userdata(userdata_words)

            if args.dump_ud_bin:
                output_path = os.path.abspath(
                    os.path.expanduser(args.dump_ud_bin)
                )

                with open(output_path, "wb") as output_file:
                    output_file.write(userdata_bytes)

                print(
                    f"Wrote {len(userdata_bytes)} bytes "
                    f"to {output_path}"
                )

        if args.firmware:
            if args.firmware == "latest":
                firmware_dir = os.path.join(
                    os.path.dirname(__file__),
                    "full_binaries",
                )

                firmware_dirs = sorted(os.listdir(firmware_dir))

                if args.verbose:
                    print(f"Content of firmware folder: {firmware_dirs}")

                versions = []

                for directory in firmware_dirs:
                    directory_path = os.path.join(
                        firmware_dir,
                        directory,
                    )

                    if directory.startswith("v") and os.path.isdir(
                        directory_path
                    ):
                        versions.append(int(directory[1:]))

                versions = sorted(versions)

                firmware_path = os.path.join(
                    firmware_dir,
                    f"v{versions[-1]}",
                    (
                        f"{args.firmware_variant}_v"
                        f"{versions[-1]}.s37"
                    ),
                )
            else:
                firmware_path = args.firmware

            if not os.path.isfile(firmware_path):
                raise FileNotFoundError(
                    f"Couldn't find {firmware_path}"
                )

            if args.verbose:
                print(f"Flashing firmware {firmware_path}")

            converted = False

            if firmware_path[-4:] == ".s37":
                hex_path = firmware_path[:-4] + ".hex"

                if not os.path.exists(hex_path):
                    content = bincopy.BinFile(firmware_path)

                    with open(hex_path, "w", encoding="utf-8") as hex_file:
                        hex_file.write(content.as_ihex())
                else:
                    content = bincopy.BinFile(hex_path)

                min_address = content.minimum_address
                converted = True

            elif firmware_path[-4:] == ".hex":
                content = bincopy.BinFile(firmware_path)
                min_address = content.minimum_address
                hex_path = firmware_path

            else:
                raise ValueError(
                    "Unsupported file type "
                    "(only .hex or .s37 files are supported): "
                    f"{firmware_path}"
                )

            try:
                programmer = FileProgrammer(session, no_reset=True)

                programmer.program(
                    hex_path,
                    base_address=None,
                    skip=False,
                    file_format=None,
                )

                time.sleep(0.1)

            finally:
                if converted:
                    os.remove(hex_path)

            print(
                f"Wrote firmware {firmware_path} "
                f"at 0x{min_address:08x}"
            )

    reset_target(session)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
