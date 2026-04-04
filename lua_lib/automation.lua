-- Automation helper functions
-- Loaded automatically before any song script

function fadeOut(track, duration, easing)
    local current = getTrackGain(track)
    return interpolate(current, 0.0, duration, function(v)
        setTrackGain(track, v)
    end, easing or "cosine")
end

function fadeIn(track, duration, easing)
    local current = getTrackGain(track)
    return interpolate(current, 1.0, duration, function(v)
        setTrackGain(track, v)
    end, easing or "cosine")
end

function fadeTo(track, target, duration, easing)
    local current = getTrackGain(track)
    return interpolate(current, target, duration, function(v)
        setTrackGain(track, v)
    end, easing or "cosine")
end

function crossfade(fromTrack, toTrack, duration, easing)
    fadeOut(fromTrack, duration, easing)
    fadeIn(toTrack, duration, easing)
end

function paramSweep(track, param, from, to, duration, easing)
    return interpolate(from, to, duration, function(v)
        setParam(track, param, v)
    end, easing)
end
