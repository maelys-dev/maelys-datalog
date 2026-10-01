"""Wheel-only profile selection; fixed for the lifetime of this interpreter."""
import os

PROFILE = os.environ.get("MAELYS_DATALOG_PROFILE", "small")
if PROFILE not in ("small", "large"):
    raise ImportError("MAELYS_DATALOG_PROFILE must be 'small' or 'large' before importing maelys_datalog")
