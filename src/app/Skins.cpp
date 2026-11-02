/*
 * PowerPlay — where skins live, and the Skins menu.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Skins.h"

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Resources.h>

#include <algorithm>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>

#include "Constants.h"
#include "WinampSkin.h"


const char* const kBuiltInSkin = "built-in:Power Station";
static const char* const kBuiltInResource = "Power Station";


status_t
skins_folder(BPath& path, bool create)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, create);
	if (status == B_OK)
		status = path.Append("PowerPlay/Skins");
	if (status == B_OK && create)
		status = create_directory(path.Path(), 0755);
	return status;
}


bool
is_skin_file(const entry_ref& ref)
{
	const char* dot = strrchr(ref.name, '.');
	return dot != NULL && strcasecmp(dot, ".wsz") == 0;
}


BMenu*
new_skins_menu(const char* currentSkin)
{
	BMenu* menu = new BMenu("Skins");
	fill_skins_menu(menu, currentSkin);
	return menu;
}


void
fill_skins_menu(BMenu* menu, const char* currentSkin)
{
	menu->RemoveItems(0, menu->CountItems(), true);
	menu->SetRadioMode(false);

	BMessage* builtIn = new BMessage(kMsgSetSkin);
	builtIn->AddString("path", "");
	BMenuItem* item = new BMenuItem("PowerPlay window", builtIn);
	item->SetMarked(currentSkin == NULL || currentSkin[0] == '\0');
	menu->AddItem(item);

	BMessage* power = new BMessage(kMsgSetSkin);
	power->AddString("path", kBuiltInSkin);
	item = new BMenuItem(kBuiltInResource, power);
	item->SetMarked(currentSkin != NULL
		&& strcmp(currentSkin, kBuiltInSkin) == 0);
	menu->AddItem(item);

	// The skins in the folder: .wsz files, and folders holding a skin
	std::vector<std::pair<std::string, std::string>> skins;
	BPath folder;
	if (skins_folder(folder) == B_OK) {
		BDirectory directory(folder.Path());
		BEntry entry;
		while (directory.GetNextEntry(&entry, true) == B_OK) {
			entry_ref ref;
			BPath path;
			if (entry.GetRef(&ref) != B_OK || entry.GetPath(&path) != B_OK)
				continue;
			std::string name = ref.name;
			if (is_skin_file(ref))
				name.erase(name.size() - 4);
			else if (!entry.IsDirectory())
				continue;
			skins.emplace_back(name, path.Path());
		}
	}
	std::sort(skins.begin(), skins.end(),
		[](const auto& a, const auto& b) {
			return strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
		});
	if (!skins.empty())
		menu->AddSeparatorItem();
	for (const auto& skin : skins) {
		BMessage* choose = new BMessage(kMsgSetSkin);
		choose->AddString("path", skin.second.c_str());
		item = new BMenuItem(skin.first.c_str(), choose);
		item->SetMarked(currentSkin != NULL
			&& skin.second == currentSkin);
		menu->AddItem(item);
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Open skins folder",
		new BMessage(kMsgOpenSkinsFolder)));
	menu->SetTargetForItems(be_app);
}


status_t
install_skin(const entry_ref& ref, BPath& installed)
{
	BPath folder;
	status_t status = skins_folder(folder, true);
	if (status != B_OK)
		return status;
	BPath source(&ref);
	BPath parent;
	source.GetParent(&parent);
	installed = folder;
	installed.Append(ref.name);
	if (parent == folder || strcmp(source.Path(), installed.Path()) == 0)
		return B_OK;	// already there

	BFile in(&ref, B_READ_ONLY);
	BFile out(installed.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if ((status = in.InitCheck()) != B_OK || (status = out.InitCheck()) != B_OK)
		return status;
	char buffer[65536];
	ssize_t bytes;
	while ((bytes = in.Read(buffer, sizeof(buffer))) > 0) {
		if (out.Write(buffer, bytes) != bytes)
			return B_IO_ERROR;
	}
	return bytes < 0 ? (status_t)bytes : B_OK;
}


bool
load_skin(skin::WinampSkin& skin, const char* path)
{
	if (strcmp(path, kBuiltInSkin) != 0)
		return skin.Load(path);
	BResources* resources = BApplication::AppResources();
	size_t size = 0;
	const uint8* data = resources != NULL
		? (const uint8*)resources->LoadResource(B_RAW_TYPE, kBuiltInResource,
			&size)
		: NULL;
	skin.SetName(kBuiltInResource);
	return data != NULL && skin.LoadArchive(std::vector<uint8_t>(data,
		data + size));
}
