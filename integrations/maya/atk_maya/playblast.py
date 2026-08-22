import tempfile
from pathlib import Path


def maya_frame_to_atk_index(maya_frame, playblast_start):
    """Convert an absolute Maya timeline frame to a zero-based ATK index."""
    return int(round(float(maya_frame) - float(playblast_start)))


def playblast_to_atk(player=None, start=None, end=None, width=1280, height=720,
                     percent=100, format="avi", compression="none", quality=90,
                     output_path=None, port=45571, cmds_module=None):
    if cmds_module is None:
        import maya.cmds as cmds_module
    if player is None:
        from atk_player import AtkPlayer
        player = AtkPlayer(port=port)

    start = cmds_module.playbackOptions(q=True, minTime=True) if start is None else start
    end = cmds_module.playbackOptions(q=True, maxTime=True) if end is None else end
    current = cmds_module.currentTime(q=True)
    if output_path is None:
        output_path = str(Path(tempfile.gettempdir()) / "atk_maya_playblast.avi")
    movie = cmds_module.playblast(
        filename=output_path, startTime=start, endTime=end, forceOverwrite=True,
        format=format, compression=compression, quality=quality,
        widthHeight=(width, height), percent=percent, viewer=False,
        showOrnaments=False)
    movie = str(Path(movie or output_path).absolute())
    player.open_media(movie, discard_unsaved=True)
    player.wait_until_loaded(movie)
    player.set_review_range(0, max(0, int(round(float(end) - float(start)))))
    player.set_loop_enabled(True)
    target = maya_frame_to_atk_index(current, start)
    player.seek_frame(target)
    player.wait_until_frame(target)
    player.show_window()
    return movie
