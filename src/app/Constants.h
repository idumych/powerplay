/*
 * PowerPlay — shared names and message codes.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_CONSTANTS_H
#define POWERPLAY_CONSTANTS_H

#include <SupportDefs.h>

#define kAppName		"PowerPlay"
#define kAppSignature	"application/x-vnd.PowerPlay"
#define kDeskbarItemName "PowerPlayVU"

enum {
	// Playback commands, from the window, the Deskbar meter or scripts
	kMsgOpen			= 'Open',
	kMsgPlayPause		= 'PlPs',
	kMsgStop			= 'Stop',
	kMsgShowWindow		= 'Show',
	kMsgPlaybackEnded	= 'PEnd',	// posted by the player at end of file
										// ("backwards": bool)
	kMsgReverse			= 'Rvrs',	// play backwards
	kMsgNext			= 'Next',
	kMsgPrevious		= 'Prev',
	kMsgSetSpeed		= 'Sped',	// "speed": float, percent (-400..400)
	kMsgSeek			= 'Seek',	// "position": float, seconds, or
										// "fraction": float, 0..1
	kMsgSetVolume		= 'Volu',	// "volume": float, 0..1
	kMsgToggleRepeat	= 'Rept',
	kMsgToggleShuffle	= 'Shuf',
	kMsgPlay			= 'Play',	// forwards; restarts a playing file
	kMsgPause			= 'Paus',	// pauses, or resumes when paused
	kMsgSetBalance		= 'Bala',	// "balance": float, -1..1

	// Skins
	kMsgSetSkin			= 'Skin',	// "path": string, "" = PowerPlay's look
	kMsgOpenSkinsFolder	= 'SkFo',
	kMsgToggleDoubleSize	= 'Dubl',
	kMsgSkinDoubleSize	= 'SkDb',	// "double": bool, from the skin window
	kMsgShowEqualizer	= 'ShEq',	// shows or hides a skin's equalizer
	// "on": bool; "band": int32 (-1 = preamp) and "gain": float (dB); or
	// "bands": 10 floats and "preamp"
	kMsgSetEqualizer	= 'SeEq',
	kMsgToggleTime		= 'Time',	// elapsed / remaining

	// The playlist. The app owns it and sends the editor a copy
	// (kMsgPlaylistUpdate: "path" strings, "current" int32) after every
	// change; the editor sends back what the user did.
	kMsgShowPlaylist	= 'ShPl',
	kMsgPlaylistUpdate	= 'PlUp',
	kMsgPlaylistPlay	= 'PlPy',	// "index": int32
	kMsgPlaylistRemove	= 'PlRm',	// "index": int32, any number
	kMsgPlaylistAdd		= 'PlAd',	// "refs"; optional "index": int32
	kMsgPlaylistClear	= 'PlCl',
	kMsgPlaylistSortName = 'PlSN',
	kMsgPlaylistSortPath = 'PlSP',
	kMsgPlaylistShuffle	= 'PlSh',
	kMsgAddFiles		= 'AddF',	// opens a file panel that adds

	// The library (the Library window's look): all the songs the user has
	// added, kept apart from the playlist. The app sends the window
	// kMsgLibraryUpdate ("path" strings) after every change.
	kMsgLibraryUpdate	= 'LbUp',
	kMsgLibraryAdd		= 'LbAd',	// "refs"
	kMsgLibraryRemove	= 'LbRm',	// "path" strings
	kMsgLibraryPlay		= 'LbPl',	// "path" strings become the playlist;
										// "index": int32, the one to play
	kMsgAddToLibrary	= 'AdLb',	// opens a file panel that adds

	// The meter: a Deskbar meter subscribes with its messenger ("meter")
	// and the app sends it levels ("left", "right": deflection, 1.0 = full
	// scale, plus "playing") about 30 times a second.
	kMsgMeterSubscribe	= 'MSub',
	kMsgMeterLevels		= 'MLev',
	kMsgPublishLevels	= 'MPub',	// the app's own timer

	kMsgToggleDeskbar	= 'TDsk',
	kMsgToggleWindowMeter = 'TWMt',
	kMsgToggleLively	= 'TLiv',
	kMsgAbout			= 'Abut'
};

// How often the app sends levels: 30 per second.
static const bigtime_t kMeterInterval = 33333;

#endif	// POWERPLAY_CONSTANTS_H
