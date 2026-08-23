/* ATK Player review integration for Toon Boom Harmony 25.x.
 * Qt Script / conservative ECMAScript; no Node.js or browser APIs.
 */

var ATK_HarmonyState = {
    moviePath: "",
    previousMoviePath: "",
    sceneName: "",
    startFrame: 1,
    endFrame: 1
};

var ATK_Host = "127.0.0.1";
var ATK_DefaultPort = 45571;

function ATK_HarmonyToIndex(harmonyFrame, exportStartFrame) {
    return Math.round(Number(harmonyFrame)) - Math.round(Number(exportStartFrame));
}

function ATK_IndexToHarmony(atkFrame, exportStartFrame) {
    return Math.round(Number(exportStartFrame)) + Math.round(Number(atkFrame));
}

function ATK_RangeToIndices(startFrame, endFrame) {
    return { start: 0, end: Math.round(Number(endFrame)) - Math.round(Number(startFrame)) };
}

function ATK_Port() {
    var port = Math.round(Number(preferences.getInt("ATK_PLAYER_API_PORT", ATK_DefaultPort)));
    return port >= 1024 && port <= 65535 ? port : ATK_DefaultPort;
}

function ATK_Transport() {
    this.nextId = 1;
    this.diagnostics = false;
}

function ATK_ResponseLineCount(raw) {
    var lines = String(raw).split("\n");
    var count = 0;
    for (var index = 0; index < lines.length; ++index) {
        if (lines[index].length > 0) ++count;
    }
    return count;
}

function ATK_EscapeResponse(raw) {
    var escaped = String(raw).replace(/\\/g, "\\\\").replace(/\r/g, "\\r").replace(/\n/g, "\\n");
    if (escaped.length > 800) escaped = escaped.substring(0, 800) + "...[truncated]";
    return escaped;
}

function ATK_ResponseId(response) {
    if (!response || response.id === undefined || response.id === null) return "missing";
    return String(response.id);
}

function ATK_LogResponse(command, expectedId, actualId, raw) {
    MessageLog.trace("ATK DEBUG: command=" + command + ", expected id=" + expectedId
        + ", actual id=" + actualId + ", response lines=" + ATK_ResponseLineCount(raw)
        + ", raw=" + ATK_EscapeResponse(raw));
}

function ATK_ReceiveLine(socket, command, expectedId, timeoutMs) {
    var deadline = new Date().getTime() + (timeoutMs || 5000);
    var assembled = "";
    var receiveCalls = 0;
    while (new Date().getTime() < deadline) {
        var remaining = Math.max(1, deadline - new Date().getTime());
        ++receiveCalls;
        if (!socket.receive(remaining)) {
            System.processOneEvent();
            continue;
        }
        var latest = String(socket.lastReceived());
        if (latest.indexOf(assembled) === 0) assembled = latest;
        else assembled += latest;
        if (assembled.indexOf("\n") >= 0) return assembled;
    }
    MessageLog.trace("ATK DEBUG: incomplete response; command=" + command
        + ", expected id=" + expectedId + ", receive calls=" + receiveCalls
        + ", characters=" + assembled.length + ", raw=" + ATK_EscapeResponse(assembled));
    throw new Error("ATK Player returned an incomplete response for " + command + ".");
}

ATK_Transport.prototype.request = function(command, params, timeoutMs) {
    var socket = new RemoteCmd();
    var id = this.nextId++;
    try {
        if (!socket.connectTimeout(ATK_Host, ATK_Port(), 2000))
            throw new Error("Could not connect to ATK Player.\nStart ATK Player and enable Local API in\nPreferences > Integrations.");
        // Harmony 25 RemoteCmd.send() appends NUL. ATK is NDJSON, so include one
        // LF, then close this connection so the trailing NUL cannot affect the next request.
        var encoded = JSON.stringify({ id: id, command: command, params: params || {} }) + "\n";
        if (!socket.send(encoded)) throw new Error("ATK Player request could not be sent.");
        var raw = ATK_ReceiveLine(socket, command, id, timeoutMs || 5000);
        var newline = raw.indexOf("\n");
        var response;
        try { response = JSON.parse(raw.substring(0, newline)); }
        catch (error) {
            ATK_LogResponse(command, id, "unparseable", raw);
            throw new Error("ATK Player returned malformed JSON for " + command + " (expected id " + id + ").");
        }
        var actualId = ATK_ResponseId(response);
        if (this.diagnostics || !response || response.id !== id) ATK_LogResponse(command, id, actualId, raw);
        if (!response || response.id !== id)
            throw new Error("ATK Player returned a mismatched response for " + command
                + " (expected id " + id + ", received id " + actualId + ").");
        if (response.ok !== true) throw new Error(String(response.error || "ATK Player command failed."));
        return response.result || {};
    } finally {
        if (socket.connected()) socket.disconnect();
    }
};

function ATK_VerifyApi(transport) {
    var info = transport.request("get_api_info", {}, 5000);
    if (info.application !== "ATK Player" || Number(info.protocolVersion) !== 1 || Number(info.frameIndexBase) !== 0)
        throw new Error("The application on this port is not a compatible ATK Player Local API.");
    return info;
}

function ATK_SafeSceneName() {
    var name = String(scene.currentScene() || "HarmonyScene");
    name = name.replace(/[\\\/:*?\"<>|]/g, "_");
    return name.length ? name : "HarmonyScene";
}

function ATK_ExportMovie(startFrame, endFrame) {
    var directoryPath = String(specialFolders.temp) + "/ATK_Player/Harmony";
    var directory = new Dir(directoryPath);
    if (!directory.exists) {
        directory.mkdirs();
        directory = new Dir(directoryPath);
        if (!directory.exists) throw new Error("Could not create the ATK Harmony review folder.");
    }
    var path = directoryPath + "/" + ATK_SafeSceneName() + "_atk_review_" + String(new Date().getTime()) + ".mov";
    exporter.exportMovie({
        codec: "openH264",
        startFrame: startFrame,
        lastFrame: endFrame,
        dstPath: path,
        withSound: true,
        resX: scene.currentResolutionX(),
        resY: scene.currentResolutionY()
    });
    var exportedMovie = new File(path);
    if (!exportedMovie.exists) throw new Error("Harmony did not create the review movie.");
    return path;
}

function ATK_WaitForLoaded(transport, moviePath) {
    var deadline = new Date().getTime() + 20000;
    while (new Date().getTime() < deadline) {
        var status = transport.request("get_status", {}, 1000);
        if (status.hasMedia === true && String(status.path).replace(/\\/g, "/") === moviePath.replace(/\\/g, "/")
            && status.state !== "loading" && status.state !== "error") return status;
        System.processOneEvent();
    }
    throw new Error("ATK Player timed out while loading the Harmony preview.");
}

function ATK_WaitForFrame(transport, target) {
    var deadline = new Date().getTime() + 8000;
    while (new Date().getTime() < deadline) {
        var status = transport.request("get_status", {}, 1000);
        if (Number(status.currentFrame) === target) return status;
        System.processOneEvent();
    }
    throw new Error("ATK Player timed out while seeking the review frame.");
}

function ATK_ReviewRangeInPlayer(startFrame, endFrame) {
    var current = Math.round(Number(frame.current()));
    var start = Math.round(Number(startFrame));
    var end = Math.round(Number(endFrame));
    if (start < 1 || end < start || end > frame.numberOf())
        throw new Error("Harmony review range must be within the scene and start at frame 1 or later.");
    var target = ATK_HarmonyToIndex(current, start);
    target = Math.max(0, Math.min(end - start, target));
    var moviePath = ATK_ExportMovie(start, end);
    var transport = new ATK_Transport();
    ATK_VerifyApi(transport);
    transport.request("open_media", { path: moviePath, discardUnsaved: true }, 5000);
    ATK_WaitForLoaded(transport, moviePath);
    transport.request("set_loop_range", { start: 0, end: end - start }, 5000);
    transport.request("set_loop_enabled", { enabled: true }, 5000);
    transport.request("seek_frame", { frame: target }, 5000);
    ATK_WaitForFrame(transport, target);
    transport.request("show_window", {}, 5000);
    ATK_HarmonyState.previousMoviePath = ATK_HarmonyState.moviePath;
    ATK_HarmonyState.moviePath = moviePath;
    ATK_HarmonyState.sceneName = String(scene.currentScene());
    ATK_HarmonyState.startFrame = start;
    ATK_HarmonyState.endFrame = end;
    preferences.setString("ATK_HARMONY_MOVIE_PATH", moviePath);
    preferences.setString("ATK_HARMONY_SCENE_NAME", ATK_HarmonyState.sceneName);
    preferences.setInt("ATK_HARMONY_START_FRAME", start);
    preferences.setInt("ATK_HARMONY_END_FRAME", end);
    if (ATK_HarmonyState.previousMoviePath && ATK_HarmonyState.previousMoviePath !== moviePath) {
        var previous = new File(ATK_HarmonyState.previousMoviePath);
        if (previous.exists) previous.remove();
    }
    MessageLog.trace("ATK Player: reviewing Harmony frames " + start + "-" + end + " at ATK frame " + target + ".");
    return moviePath;
}

function ATK_ReviewInPlayer() {
    try { return ATK_ReviewRangeInPlayer(1, frame.numberOf()); }
    catch (error) { MessageLog.error("ATK Player: " + error.message); MessageBox.critical(String(error.message)); return ""; }
}

function ATK_TestConnection() {
    var transport = new ATK_Transport();
    try {
        var info = ATK_VerifyApi(transport);
        MessageLog.trace("ATK Player: connected; protocol " + info.protocolVersion + ", frame base " + info.frameIndexBase + ".");
        return true;
    } catch (error) {
        MessageLog.error("ATK Player: " + error.message); MessageBox.critical(String(error.message)); return false;
    }
}

function ATK_TestSequentialRequests() {
    var transport = new ATK_Transport();
    transport.diagnostics = true;
    try {
        ATK_VerifyApi(transport);
        transport.request("get_status", {}, 5000);
        ATK_VerifyApi(transport);
        MessageLog.trace("ATK DEBUG: sequential request test completed successfully.");
        return true;
    } catch (error) {
        MessageLog.error("ATK Player: " + error.message); MessageBox.critical(String(error.message)); return false;
    }
}

function ATK_JumpToReviewFrame() {
    if (!ATK_HarmonyState.moviePath) {
        ATK_HarmonyState.moviePath = String(preferences.getString("ATK_HARMONY_MOVIE_PATH", ""));
        ATK_HarmonyState.sceneName = String(preferences.getString("ATK_HARMONY_SCENE_NAME", ""));
        ATK_HarmonyState.startFrame = Math.round(Number(preferences.getInt("ATK_HARMONY_START_FRAME", 1)));
        ATK_HarmonyState.endFrame = Math.round(Number(preferences.getInt("ATK_HARMONY_END_FRAME", 1)));
    }
    if (!ATK_HarmonyState.moviePath || ATK_HarmonyState.sceneName !== String(scene.currentScene())) {
        MessageBox.critical("No ATK review mapping is available for this Harmony scene."); return false;
    }
    var transport = new ATK_Transport();
    try {
        ATK_VerifyApi(transport);
        var status = transport.request("get_status", {}, 5000);
        var destination = ATK_IndexToHarmony(Number(status.currentFrame), ATK_HarmonyState.startFrame);
        if (destination < ATK_HarmonyState.startFrame || destination > ATK_HarmonyState.endFrame || destination > frame.numberOf())
            throw new Error("ATK's current frame is outside the saved Harmony review range.");
        frame.setCurrent(Math.round(destination));
        MessageLog.trace("ATK Player: jumped Harmony to frame " + destination + ".");
        return true;
    } catch (error) {
        MessageLog.error("ATK Player: " + error.message); MessageBox.critical(String(error.message)); return false;
    }
}

function ATK_Settings() {
    var current = ATK_Port();
    var value = Math.round(Number(Input.getNumber("Local API port", current, 0, 1024, 65535, "ATK Player")));
    if (value >= 1024 && value <= 65535) preferences.setInt("ATK_PLAYER_API_PORT", value);
}
