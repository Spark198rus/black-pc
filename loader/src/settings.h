// Port settings remembered between launches (black.ini next to black.exe): what PC SETTINGS
// change. Command-line flags win over the file for that session. Test runs (--background) neither read nor
// write it.
#pragma once

void settings_mark_cli(const char *key); // a command-line flag set this setting: the file must not override it
void settings_load();                    // after the command line, once the root folder is current
void settings_save();
// The frame rate from --fps or black.ini: 60 for 60 and above (more is not offered: the world steps one fixed
// frame time per frame and the port paces one frame per 60 Hz vblank; above 60 says so in the log), else 30.
int fps_setting(int requested);
void fps_setting_note(); // after the log is open
