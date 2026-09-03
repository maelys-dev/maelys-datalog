from __future__ import annotations


class MaelysDatalogError(RuntimeError):
    def __init__(self, code: int, message: str = "", hint: str = ""):
        self.code = code
        self.message = message
        self.hint = hint
        detail = f"Maelys Datalog error {code}"
        if message:
            detail += f": {message}"
        if hint:
            detail += f" ({hint})"
        super().__init__(detail)


class DomainAlreadyRegisteredError(MaelysDatalogError):
    pass


class DomainRegistryFullError(MaelysDatalogError):
    pass
