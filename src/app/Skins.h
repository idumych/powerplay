/*
 * PowerPlay — where skins live, and the Skins menu.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_SKINS_H
#define POWERPLAY_SKINS_H

#include <Path.h>

class BMenu;

// The folder PowerPlay looks for skins in (.wsz files or folders), in its
// settings: ~/config/settings/PowerPlay/Skins on Haiku.
status_t skins_folder(BPath& path, bool create = false);

// A menu of the built-in look and the skins found, the one in use marked:
// each item sends kMsgSetSkin with "path" ("" for PowerPlay's own look).
BMenu* new_skins_menu(const char* currentSkin);
// The same, filling an existing menu (rebuilt each time it opens).
void fill_skins_menu(BMenu* menu, const char* currentSkin);

// Copies a skin file into the skins folder (if it isn't there already);
// the copy's path.
status_t install_skin(const entry_ref& ref, BPath& installed);

bool is_skin_file(const entry_ref& ref);

#endif	// POWERPLAY_SKINS_H
