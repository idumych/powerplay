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

	// The meter: a Deskbar meter subscribes with its messenger ("meter")
	// and the app sends it levels ("left", "right": deflection, 1.0 = full
	// scale, plus "playing") about 30 times a second.
	kMsgMeterSubscribe	= 'MSub',
	kMsgMeterLevels		= 'MLev',
	kMsgPublishLevels	= 'MPub',	// the app's own timer

	kMsgToggleDeskbar	= 'TDsk',
	kMsgAbout			= 'Abut'
};

// How often the app sends levels: 30 per second.
static const bigtime_t kMeterInterval = 33333;

#endif	// POWERPLAY_CONSTANTS_H
