class AtkError(Exception):
    """Base error for the ATK Player client."""


class AtkConnectionError(AtkError):
    pass


class AtkProtocolError(AtkError):
    pass


class AtkCommandError(AtkError):
    pass


class AtkTimeoutError(AtkConnectionError):
    pass
