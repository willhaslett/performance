-- Akai MPK mini 3 control map
-- MIDI device name: "MPK mini 3"

mpk = {
    name = "MPK mini 3",
    channel = 10,

    -- Pads (CC mode, momentary: 127 on press, 0 on release)
    -- Top row, left to right
    pad1 = { type = "cc", channel = 10, number = 20 },
    pad2 = { type = "cc", channel = 10, number = 21 },
    pad3 = { type = "cc", channel = 10, number = 22 },
    pad4 = { type = "cc", channel = 10, number = 23 },
    -- Bottom row, left to right
    pad5 = { type = "cc", channel = 10, number = 16 },
    pad6 = { type = "cc", channel = 10, number = 17 },
    pad7 = { type = "cc", channel = 10, number = 18 },
    pad8 = { type = "cc", channel = 10, number = 19 },

    -- Keys: channel 1, standard note events
    keys = { channel = 1 },

    -- TODO: knobs, joystick, transport buttons
}
