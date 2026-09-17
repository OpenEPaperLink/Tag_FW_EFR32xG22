import argparse
import bincopy
import time
import os

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
