"""Select the wheel's installed native module; never compile or fall back."""
from importlib import import_module
from ._wheel_profile import PROFILE

_native = import_module(f"maelys_datalog._{PROFILE}._maelys_cffi")
ffi, lib = _native.ffi, _native.lib
