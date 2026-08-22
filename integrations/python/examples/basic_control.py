from atk_player import AtkPlayer


with AtkPlayer() as atk:
    print(atk.api_info())
    print(atk.status())
    # atk.open_media(r"C:\shots\shot010_playblast.avi")
    # atk.wait_until_loaded(r"C:\shots\shot010_playblast.avi")
    # atk.seek_frame(24)  # API indices are zero-based.
